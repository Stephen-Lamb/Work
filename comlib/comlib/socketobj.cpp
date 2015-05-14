/**
 * @file
 * Defines the SocketObj class.
 */

#include "socketobj.h"
#include <boost/thread/thread.hpp>
#include "debug.h"
#include "socketregistry.h"

WSAEVENT SocketObj::netEvent() const
{
    return m_netEvent;
}

void SocketObj::onNetEvent()
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

    if ((wsaNetworkEvents.lNetworkEvents & FD_CONNECT) != 0)
    {
        int err = CL_ERR_OK;
        if (wsaNetworkEvents.iErrorCode[FD_CONNECT_BIT] != 0)
        {
            err = wsaNetworkEvents.iErrorCode[FD_CONNECT_BIT];
        }

        onFdConnect(err);
    }

    if ((wsaNetworkEvents.lNetworkEvents & FD_READ) != 0)
    {
        if (wsaNetworkEvents.iErrorCode[FD_READ_BIT] == 0)
        {
            onFdRead();
        }
        else
        {
            OUTPUT_FMT_DEBUG_STRING("FD_READ failed, err=" <<
                wsaNetworkEvents.iErrorCode[FD_READ_BIT]);
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

int SocketObj::create(const char* hostAddr, unsigned short hostPort,
                      CLPDataRecvFn dataRecvFn,
                      CLPSocketClosedFn socketClosedFn, void* arg,
                      SocketObj** pSktObj)
{
    SocketObj* self = new SocketObj(dataRecvFn, socketClosedFn, arg);
    int err = self->construct(hostAddr, hostPort);
    if (err == CL_ERR_OK)
    {
        *pSktObj = self;
    }
    else
    {
        delete self;
    }
    return err;
}

int SocketObj::createAsync(const char* hostAddr, unsigned short hostPort,
                           CLPConCompletedFn conCompletedFn,
                           CLPDataRecvFn dataRecvFn,
                           CLPSocketClosedFn socketClosedFn, void* arg,
                           SocketObj** pSktObj)
{
    SocketObj* self = new SocketObj(conCompletedFn, dataRecvFn, socketClosedFn,
        arg);
    int err = self->constructAsync(hostAddr, hostPort);
    if (err == CL_ERR_OK)
    {
        *pSktObj = self;
    }
    else
    {
        delete self;
    }
    return err;
}

int SocketObj::createAccepted(SOCKET clientSocket, CLPDataRecvFn dataRecvFn,
                              CLPSocketClosedFn socketClosedFn, void* arg,
                              SocketObj** pSktObj)
{
    SocketObj* self = new SocketObj(clientSocket, dataRecvFn, socketClosedFn,
        arg);
    int err = self->constructAccepted();
    if (err == CL_ERR_OK)
    {
        *pSktObj = self;
    }
    else
    {
        delete self;
    }
    return err;
}

SocketObj::~SocketObj()
{
    freeaddrinfo(m_addrInfo);

    if (m_netEvent != WSA_INVALID_EVENT)
    {
        WSACloseEvent(m_netEvent);
    }
}

int SocketObj::sendData(const char* buf, int len)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    if (m_dataStreamCorrupted)
    {
        return CL_ERR_DATA_STREAM_CORRUPTED;
    }

    // Switch the socket to blocking mode then back to non-blocking mode when
    // we are finished
    int err = SetBlockingMode();
    if (err != CL_ERR_OK)
    {
        return err;
    }

    // Fill out the length prefix array in network byte format
    char prefix[PREFIX_LEN];
    *(reinterpret_cast<PrefixType*>(prefix)) =
        htons(static_cast<PrefixType>(len));

    // Send the length prefix then the supplied buffer
    int bytesSent = 0;
    err = sendAll(prefix, PREFIX_LEN, bytesSent);
    if (err == CL_ERR_OK)
    {
        // The prefix was sent. Send the supplied buffer
        err = sendAll(buf, len, bytesSent);
        if (err != CL_ERR_OK)
        {
            m_dataStreamCorrupted = true;
        }
    }
    else
    {
        if (bytesSent > 0)
        {
            m_dataStreamCorrupted = true;
        }
    }

    int setNonBlockingModeErr = SetNonBlockingMode();
    if (err == CL_ERR_OK)
    {
        err = setNonBlockingModeErr;
    }

    return err;
}

void SocketObj::close()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    m_closeCalled = true;

    // Wait for the host address resolver thread to complete
    while (!m_resolveAsyncCompleted)
    {
        m_resolveAsyncCompletedCondVar.wait(lock);
    }

    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

int SocketObj::resolveHostAddr(const char* hostAddr, unsigned short hostPort,
                               ADDRINFOA** pAddrInfo)
{
    assert(pAddrInfo != 0);

    int err = CL_ERR_OK;

    char hostPortStr[NI_MAXSERV];
    _ultoa_s(hostPort, hostPortStr, 10);

    ADDRINFOA hints = {};
    // hints.ai_flags is left as 0 so both host names and IP addresses will
    // be resolved
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOA* addrInfo = NULL;
    int getAddrInfoErr = getaddrinfo(hostAddr, hostPortStr, &hints, &addrInfo);
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

void SocketObj::resolveHostAddrThreadProc(std::string hostAddr,
    unsigned short hostPort, SocketObj* self)
{
    ADDRINFOA* addrInfo = NULL;
    int err = resolveHostAddr(hostAddr.c_str(), hostPort, &addrInfo);
    self->onHostAddrResolved(addrInfo, err);
}

SocketObj::SocketObj(CLPDataRecvFn dataRecvFn,
                     CLPSocketClosedFn socketClosedFn, void* arg) :
m_conCompletedFn(0), m_dataRecvFn(dataRecvFn),
m_socketClosedFn(socketClosedFn), m_arg(arg), m_netEvent(WSA_INVALID_EVENT),
m_socket(INVALID_SOCKET), m_closeCalled(false), m_addrInfo(NULL),
m_crntAddrInfo(NULL), m_resolveAsyncCompleted(true),
m_dataStreamCorrupted(false), m_DataRecvLen(0)
{
}

SocketObj::SocketObj(CLPConCompletedFn conCompletedFn,
                     CLPDataRecvFn dataRecvFn,
                     CLPSocketClosedFn socketClosedFn, void* arg) :
m_conCompletedFn(conCompletedFn), m_dataRecvFn(dataRecvFn),
m_socketClosedFn(socketClosedFn), m_arg(arg), m_netEvent(WSA_INVALID_EVENT),
m_socket(INVALID_SOCKET), m_closeCalled(false), m_addrInfo(NULL),
m_crntAddrInfo(NULL), m_resolveAsyncCompleted(true),
m_dataStreamCorrupted(false), m_DataRecvLen(0)
{
}

SocketObj::SocketObj(SOCKET clientSocket, CLPDataRecvFn dataRecvFn,
                     CLPSocketClosedFn socketClosedFn, void* arg) :
m_conCompletedFn(0), m_dataRecvFn(dataRecvFn),
m_socketClosedFn(socketClosedFn), m_arg(arg), m_netEvent(WSA_INVALID_EVENT),
m_socket(clientSocket), m_closeCalled(false), m_addrInfo(NULL),
m_crntAddrInfo(NULL), m_resolveAsyncCompleted(true),
m_dataStreamCorrupted(false), m_DataRecvLen(0)
{
}

int SocketObj::construct(const char* hostAddr, unsigned short hostPort)
{
    int err = createNetEvent();

    ADDRINFOA* addrInfo = NULL;
    if (err == CL_ERR_OK)
    {
        err = resolveHostAddr(hostAddr, hostPort, &addrInfo);
    }

    if (err == CL_ERR_OK)
    {
        ADDRINFOA* crntAddrInfo = addrInfo;
        err = doConnect(&crntAddrInfo);
    }

    // Free the address info now that it is no longer needed
    if (addrInfo != NULL)
    {
        freeaddrinfo(addrInfo);
        addrInfo = NULL;
    }

    if (err == CL_ERR_OK)
    {
        // Associate the event object with the socket and select what network
        // events we want to be notified about
        err = SetNonBlockingMode();
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

int SocketObj::constructAsync(const char* hostAddr, unsigned short hostPort)
{
    int err = createNetEvent();

    if (err == CL_ERR_OK)
    {
        // Resolve the host address asynchronously then connect
        assert(hostAddr != 0);
        std::string hostAddrStr(hostAddr);
        m_resolveAsyncCompleted = false;
        boost::thread aThread(resolveHostAddrThreadProc, hostAddrStr,
            hostPort, this);
    }

    return err;
}

int SocketObj::constructAccepted()
{
    int err = createNetEvent();

    if (err == CL_ERR_OK)
    {
        // Associate the event object with the socket and select what network
        // events we want to be notified about
        err = SetNonBlockingMode();
    }

    // If construction failed, close the socket
    if (err != CL_ERR_OK)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    return err;
}

int SocketObj::createNetEvent()
{
    int err = CL_ERR_OK;
    m_netEvent = WSACreateEvent();
    if (m_netEvent == WSA_INVALID_EVENT)
    {
        err = WSAGetLastError();
    }
    return err;
}

int SocketObj::SetNonBlockingMode()
{
    int err = CL_ERR_OK;

    long networkEvents = FD_READ | FD_CLOSE;
    if (m_conCompletedFn != 0)
    {
        // We are required to connect asynchronously
        networkEvents |= FD_CONNECT;
    }

    if (WSAEventSelect(m_socket, m_netEvent, networkEvents) == SOCKET_ERROR)
    {
        err = WSAGetLastError();
    }

    return err;
}

int SocketObj::SetBlockingMode()
{
    int err = CL_ERR_OK;

    if (WSAEventSelect(m_socket, NULL, 0) != SOCKET_ERROR)
    {
        u_long nonblockingMode = 0;
        if (ioctlsocket(m_socket, FIONBIO, &nonblockingMode) == SOCKET_ERROR)
        {
            err = WSAGetLastError();
        }
    }
    else
    {
        err = WSAGetLastError();
    }

    return err;
}

void SocketObj::onHostAddrResolved(ADDRINFOA* addrInfo,
                                   int hostAddrResolvedErr)
{
    int err = hostAddrResolvedErr;
    CLPConCompletedFn conCompletedFn;
    CLSocket sktObjHandle;
    void* arg;

    {
        boost::lock_guard<boost::mutex> lock(m_mutex);

        assert(m_conCompletedFn != 0);
        conCompletedFn = m_conCompletedFn;
        sktObjHandle = SocketRegistry::toHandle(this);
        arg = m_arg;

        m_addrInfo = addrInfo;

        if (err == CL_ERR_OK && !m_closeCalled)
        {
            m_crntAddrInfo = m_addrInfo;
            err = doConnectAsync(&m_crntAddrInfo);
        }

        // Signal that the host address resolver thread has completed and
        // unlock the mutex before we call the callback function to avoid
        // possible deadlocks. Also, don't access any member data after we have
        // unlocked the mutex as the object may have been deleted (admittedly
        // rather unlikely)
        m_resolveAsyncCompleted = true;
        m_resolveAsyncCompletedCondVar.notify_all();
    }

    if (err != CL_ERR_OK)
    {
        conCompletedFn(sktObjHandle, err, arg);
    }
}

int SocketObj::doConnect(ADDRINFOA** pCrntAddrInfo)
{
    assert(pCrntAddrInfo != 0);
    assert(*pCrntAddrInfo != NULL);

    int err = CL_ERR_OK;

    // Create the socket and connect synchronously to the first address in the
    // given linked list of addresses. If the connection attempt fails then try
    // the next address in the list and so on until we reach the end of the
    // list
    do
    {
        err = CL_ERR_OK;

        // Close the socket if it has already been created
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
        }

        // Create the socket
        m_socket = socket((*pCrntAddrInfo)->ai_family,
            (*pCrntAddrInfo)->ai_socktype, (*pCrntAddrInfo)->ai_protocol);
        if (m_socket != INVALID_SOCKET)
        {
            // Connect to the given host. Note that this blocks until the
            // connection attempt completes, as the given socket is in
            // blocking mode by default
            if (connect(m_socket, (*pCrntAddrInfo)->ai_addr,
                static_cast<int>((*pCrntAddrInfo)->ai_addrlen)) ==
                    SOCKET_ERROR)
            {
                err = WSAGetLastError();
            }
        }
        else
        {
            err = WSAGetLastError();
        }

        *pCrntAddrInfo = (*pCrntAddrInfo)->ai_next;
    } while (err != CL_ERR_OK && *pCrntAddrInfo != NULL);

    return err;
}

int SocketObj::doConnectAsync(ADDRINFOA** pCrntAddrInfo)
{
    assert(pCrntAddrInfo != 0);
    assert(*pCrntAddrInfo != NULL);

    int err = CL_ERR_OK;

    // Create the socket and connect asynchronously to the first address in the
    // given linked list of addresses. If the connection attempt fails then try
    // the next address in the list and so on until we reach the end of the
    // list
    do
    {
        err = CL_ERR_OK;

        // Close the socket if it has already been created
        if (m_socket != INVALID_SOCKET)
        {
            closesocket(m_socket);
        }

        // Create the socket
        m_socket = socket((*pCrntAddrInfo)->ai_family,
            (*pCrntAddrInfo)->ai_socktype, (*pCrntAddrInfo)->ai_protocol);
        if (m_socket != INVALID_SOCKET)
        {
            // Associate the event object with the socket and select what
            // network events we want to be notified about
            err = SetNonBlockingMode();

            if (err == CL_ERR_OK)
            {
                // Connect to the given host. Note that this returns
                // immediately, as the socket is in non-blocking mode
                if (connect(m_socket, (*pCrntAddrInfo)->ai_addr,
                    static_cast<int>((*pCrntAddrInfo)->ai_addrlen)) ==
                        SOCKET_ERROR)
                {
                    if (WSAGetLastError() != WSAEWOULDBLOCK)
                    {
                        err = WSAGetLastError();
                    }
                }
            }
        }
        else
        {
            err = WSAGetLastError();
        }

        *pCrntAddrInfo = (*pCrntAddrInfo)->ai_next;
    } while (err != CL_ERR_OK && *pCrntAddrInfo != NULL);

    return err;
}

void SocketObj::onFdConnect(int fdConnectErr)
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    assert(m_conCompletedFn != 0);

    if (m_socket == INVALID_SOCKET)
    {
        // Socket closed
        return;
    }

    int err = fdConnectErr;

    if (err != CL_ERR_OK && m_crntAddrInfo != NULL)
    {
        // Try connecting to the next address in the linked list 
        err = doConnectAsync(&m_crntAddrInfo);

        if (err != CL_ERR_OK)
        {
            // Unlock the mutex because we do not want this object to be locked
            // when we call the callback function
            lock.unlock();

            m_conCompletedFn(SocketRegistry::toHandle(this), err, m_arg);
        }
    }
    else
    {
        // Unlock the mutex because we do not want this object to be locked
        // when we call the callback function
        lock.unlock();

        m_conCompletedFn(SocketRegistry::toHandle(this), err, m_arg);
    }
}

void SocketObj::onFdRead()
{
    boost::unique_lock<boost::mutex> lock(m_mutex);

    if (m_socket == INVALID_SOCKET)
    {
        // Socket closed
        return;
    }

    if (m_DataRecvLen < PREFIX_LEN)
    {
        // Read the length prefix so we know how much data follows
        m_DataRecvBuf.resize(PREFIX_LEN);
        int recvRetVal = recv(m_socket, &m_DataRecvBuf[m_DataRecvLen],
            static_cast<int>(m_DataRecvBuf.size()) - m_DataRecvLen, 0);
        if (recvRetVal != SOCKET_ERROR )
        {
            m_DataRecvLen += recvRetVal;
        }
        else if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            OUTPUT_FMT_DEBUG_STRING("recv failed, err=" << WSAGetLastError());
        }
    }

    if (m_DataRecvLen >= PREFIX_LEN)
    {
        // Calculate the value of the length prefix - note that it is in
        // network byte format
        int prefixValue =
            ntohs(*(reinterpret_cast<PrefixType*>(&m_DataRecvBuf[0])));

        if (prefixValue > 0)
        {
            // Read data following the length prefix
            m_DataRecvBuf.resize(PREFIX_LEN + prefixValue);
            int recvRetVal = recv(m_socket, &m_DataRecvBuf[m_DataRecvLen],
                static_cast<int>(m_DataRecvBuf.size()) - m_DataRecvLen, 0);
            if (recvRetVal != SOCKET_ERROR )
            {
                m_DataRecvLen += recvRetVal;
            }
            else if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                OUTPUT_FMT_DEBUG_STRING("recv failed, err=" <<
                    WSAGetLastError());
            }
        }

        if (m_DataRecvLen == PREFIX_LEN + prefixValue)
        {
            // We have received all data, so notify the caller
            std::vector<char> localDataRecvBuf;
            localDataRecvBuf.swap(m_DataRecvBuf);
                // N.B. using swap() in this way sets vector capacity of
                // m_DataRecvBuf back to default
            m_DataRecvLen = 0;

            if (prefixValue > 0)
            {
                // Unlock the mutex because we do not want this object to be
                // locked when we call the callback function
                lock.unlock();

                m_dataRecvFn(SocketRegistry::toHandle(this),
                    &localDataRecvBuf[PREFIX_LEN], prefixValue, m_arg);
            }
        }
    }
}

void SocketObj::onFdClose(int fdCloseErr)
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

    m_socketClosedFn(SocketRegistry::toHandle(this), fdCloseErr, m_arg);
}

int SocketObj::sendAll(const char* buf, int len, int& bytesSent)
{
    bytesSent = 0;
    int err = CL_ERR_OK;

    while (bytesSent < len && err == CL_ERR_OK)
    {
        int sendRetVal = send(m_socket, buf + bytesSent, len - bytesSent, 0);

        if (sendRetVal != SOCKET_ERROR)
        {
            bytesSent += sendRetVal;
        }
        else
        {
            err = WSAGetLastError();
        }
    }

    return err;
}
