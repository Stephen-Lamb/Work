/**
 * @file
 * Defines the SrvSocketObj class.
 */

#include "srvsocketobj.h"
#include "debug.h"
#include "socketregistry.h"

WSAEVENT SrvSocketObj::netEvent() const
{
    return m_netEvent;
}

void SrvSocketObj::onNetEvent()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    if (m_socket == INVALID_SOCKET)
    {
        // Socket closed
        return;
    }

    WSANETWORKEVENTS wsaNetworkEvents;
    int wsaEnumNetworkEventsErr = WSAEnumNetworkEvents(m_socket, m_netEvent,
        &wsaNetworkEvents);
    if (wsaEnumNetworkEventsErr == SOCKET_ERROR)
    {
        OUTPUT_FMT_DEBUG_STRING("WSAEnumNetworkEvents failed, err=" <<
            WSAGetLastError());
        return;
    }

    // Unlock the mutex because we do not want this object to be locked when we
    // call any of the callback functions
    lock.unlock();

    if ((wsaNetworkEvents.lNetworkEvents & FD_ACCEPT) != 0)
    {
        if (wsaNetworkEvents.iErrorCode[FD_ACCEPT_BIT] == 0)
        {
            onFdAccept();
        }
        else
        {
            OUTPUT_FMT_DEBUG_STRING("FD_ACCEPT failed, err=" <<
                wsaNetworkEvents.iErrorCode[FD_ACCEPT_BIT]);
        }
    }

    if ((wsaNetworkEvents.lNetworkEvents & FD_CLOSE) != 0)
    {
        int err = CL_ERR_OK;
        if (wsaNetworkEvents.iErrorCode[FD_CLOSE_BIT] != 0)
        {
            err = wsaNetworkEvents.iErrorCode[FD_CLOSE_BIT];
        }

        onFdClose(err);
    }
}

int SrvSocketObj::create(const char* ipAddr, unsigned short port,
                         CLPConPendingFn conPendingFn,
                         CLPSrvSocketClosedFn srvSocketClosedFn,
                         int conBacklog, void* srvArg,
                         SrvSocketObj** pSrvSktObj)
{
    SrvSocketObj* self = new SrvSocketObj(conPendingFn, srvSocketClosedFn,
        conBacklog, srvArg);
    int err = self->construct(ipAddr, port);
    if (err == CL_ERR_OK)
    {
        *pSrvSktObj = self;
    }
    else
    {
        delete self;
    }
    return err;
}

SrvSocketObj::~SrvSocketObj()
{
    delete[] m_clientAddr;

    if (m_netEvent != WSA_INVALID_EVENT)
    {
        WSACloseEvent(m_netEvent);
    }
}

int SrvSocketObj::acceptConnection(SOCKET* pAcceptedSocket,
                                   char* clientIpAddr, int clientIpAddrLen,
                                   unsigned short* pClientPort)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    // Accept the connection and get the IP address and port of the client
    // socket if required
    int err = CL_ERR_OK;
    assert(m_clientAddr != 0); // Already created
    int clientAddrLen = m_clientAddrLen;
        // Using a copy as accept can modify the value

    *pAcceptedSocket = accept(m_socket, m_clientAddr, &clientAddrLen);
    if (*pAcceptedSocket != INVALID_SOCKET)
    {
        char clientPortStr[NI_MAXSERV];
        DWORD clientPortStrLen = sizeof(clientPortStr);

        int getNameInfoErr = getnameinfo(m_clientAddr, clientAddrLen,
            clientIpAddr, clientIpAddrLen,
            ((pClientPort != NULL) ? clientPortStr : NULL),
            ((pClientPort != NULL) ? clientPortStrLen : 0),
            NI_NUMERICHOST | NI_NUMERICSERV);
        if (getNameInfoErr == 0)
        {
            // Get name info succeeded
            if (pClientPort != NULL)
            {
                *pClientPort = static_cast<unsigned short>(
                    strtoul(clientPortStr, NULL, 10));
            }
        }
        else
        {
            err = WSAGetLastError();

            closesocket(*pAcceptedSocket);
            *pAcceptedSocket = INVALID_SOCKET;
        }
    }
    else
    {
        err = WSAGetLastError();
    }

    return err;
}

void SrvSocketObj::close()
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

int SrvSocketObj::resolveIpAddr(const char* ipAddr, unsigned short port,
                                ADDRINFOA** pAddrInfo)
{
    assert(pAddrInfo != 0);

    int err = CL_ERR_OK;

    char portStr[NI_MAXSERV];
    _ultoa_s(port, portStr, 10);

    ADDRINFOA hints = {};
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOA* addrInfo = NULL;
    int getAddrInfoErr = getaddrinfo(ipAddr, portStr, &hints, &addrInfo);
    if (getAddrInfoErr == 0)
    {
        *pAddrInfo = addrInfo;
    }
    else
    {
        err = getAddrInfoErr;
    }

    return err;
}

SrvSocketObj::SrvSocketObj(CLPConPendingFn conPendingFn,
                           CLPSrvSocketClosedFn srvSocketClosedFn,
                           int conBacklog, void* srvArg) :
m_conPendingFn(conPendingFn), m_srvSocketClosedFn(srvSocketClosedFn),
m_conBacklog(conBacklog), m_srvArg(srvArg), m_netEvent(WSA_INVALID_EVENT),
m_socket(INVALID_SOCKET), m_clientAddr(0), m_clientAddrLen(0)
{
}

int SrvSocketObj::construct(const char* ipAddr, unsigned short port)
{
    int err = createNetEvent();

    ADDRINFOA* addrInfo = NULL;
    if (err == CL_ERR_OK)
    {
        err = resolveIpAddr(ipAddr, port, &addrInfo);
    }

    if (err == CL_ERR_OK)
    {
        // Create the socket
        assert(addrInfo != NULL);
        m_socket = socket(addrInfo->ai_family, addrInfo->ai_socktype,
            addrInfo->ai_protocol);
        if (m_socket == INVALID_SOCKET)
        {
            err = WSAGetLastError();
        }
    }

    if (err == CL_ERR_OK)
    {
        // Associate the event object with the socket and select what network
        // events we want to be notified about. Note that this switches the
        // socket to non-blocking mode
        if (WSAEventSelect(m_socket, m_netEvent, FD_ACCEPT | FD_CLOSE) ==
            SOCKET_ERROR)
        {
            err = WSAGetLastError();
        }
    }

    if (err == CL_ERR_OK)
    {
        // Bind the socket to the IP address and port
        if (bind(m_socket, addrInfo->ai_addr,
            static_cast<int>(addrInfo->ai_addrlen)) == SOCKET_ERROR)
        {
            err = WSAGetLastError();
        }
    }

    if (err == CL_ERR_OK)
    {
        // Start listening on the socket
        if (listen(m_socket, m_conBacklog) != SOCKET_ERROR)
        {
            // listen succeeded. Create the address structure used for accepted
            // client connections
            m_clientAddr = reinterpret_cast<SOCKADDR*>(
                new char[addrInfo->ai_addrlen]);
            m_clientAddrLen = static_cast<int>(addrInfo->ai_addrlen);
        }
        else
        {
            err = WSAGetLastError();
        }
    }

    // Free the address info now that it is no longer needed
    if (addrInfo != NULL)
    {
        freeaddrinfo(addrInfo);
        addrInfo = NULL;
    }

    // If construction failed, close the socket if it is open
    if (err != CL_ERR_OK)
    {
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    return err;
}

int SrvSocketObj::createNetEvent()
{
    int err = CL_ERR_OK;
    m_netEvent = WSACreateEvent();
    if (m_netEvent == WSA_INVALID_EVENT)
    {
        err = WSAGetLastError();
    }
    return err;
}

void SrvSocketObj::onFdAccept()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    if (m_socket == INVALID_SOCKET)
    {
        // Socket closed
        return;
    }

    // Unlock the mutex because we do not want this object to be locked when we
    // call the callback function
    lock.unlock();

    m_conPendingFn(SrvSocketRegistry::toHandle(this), m_srvArg);
}

void SrvSocketObj::onFdClose(int fdCloseErr)
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    if (m_socket == INVALID_SOCKET)
    {
        // Socket closed
        return;
    }

    // Unlock the mutex because we do not want this object to be locked when we
    // call the callback function
    lock.unlock();

    m_srvSocketClosedFn(SrvSocketRegistry::toHandle(this), fdCloseErr,
        m_srvArg);
}
