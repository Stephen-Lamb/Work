/**
 * @file
 * Declares the SocketObj class.
 */

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/mutex.hpp>
#include <string>
#include <vector>
#include "inc/comlib/comlib.h"
#include "netobj.h"

/**
 * Represents a TCP socket that connects to another TCP socket listening on a
 * local or remote IP address and port to be able to send and receive data.
 */
class SocketObj : public NetObj
{
private:
    /** The type used for the length prefix. */
    typedef WORD PrefixType;

    /**
     * The number of bytes prefixed to data sent and received. This holds the
     * length of data following in network byte order format.
     */
    static const int PREFIX_LEN = sizeof(PrefixType);

public:
    // Inherited from NetObj
    virtual WSAEVENT netEvent() const;
    virtual void onNetEvent();

    /** The maximum length of data that can be sent and received. */
    static const int DATA_MAX_LEN = static_cast<PrefixType>(~0);

    /**
     * Creates a socket object that is connected to the given host address and
     * port.
     *
     * @param hostAddr the host address to connect to.
     * @param hostPort the host port to connect to.
     * @param dataRecvFn this will be called when the socket object has
     * received data.
     * @param socketClosedFn this will be called when the socket object has
     * closed.
     * @param arg this will be passed back as is in any of the socket object's
     * callback functions.
     * @param pSktObj if the method was successful this will be set to point to
     * the socket object that was created.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int create(const char* hostAddr, unsigned short hostPort,
        CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn, void* arg,
        SocketObj** pSktObj);

    /**
     * Creates a socket object that connects asynchronously to the given host
     * address and port.
     *
     * @param hostAddr the host address to connect to.
     * @param hostPort the host port to connect to.
     * @param conCompletedFn this will be called when the connection attempt
     * has completed.
     * @param dataRecvFn this will be called when the socket object has
     * received data.
     * @param socketClosedFn this will be called when the socket object has
     * closed.
     * @param arg this will be passed back as is in any of the socket object's
     * callback functions.
     * @param pSktObj if the method was successful this will be set to point to
     * the socket object that was created.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int createAsync(const char* hostAddr,
        unsigned short hostPort, CLPConCompletedFn conCompletedFn,
        CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn, void* arg,
        SocketObj** pSktObj);

    /**
     * Creates a socket object given an already accepted connection. Note that
     * this method takes ownership of the given socket and is guaranteed to
     * close it regardless of whether of not the method call was successful.
     *
     * @param clientSocket the already accepted connection.
     * @param dataRecvFn this will be called when the client socket has
     * received data.
     * @param socketClosedFn this will be called when the client socket has
     * closed.
     * @param arg this will be passed back as is in any of the client socket's
     * callback functions.
     * @param pSktObj if the method was successful this will be set to point to
     * the socket object that was created.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int createAccepted(SOCKET clientSocket, CLPDataRecvFn dataRecvFn,
        CLPSocketClosedFn socketClosedFn, void* arg, SocketObj** pSktObj);

    virtual ~SocketObj();

    /**
     * Sends the given data over the connection.
     *
     * @param buf the data to send.
     * @param len the length of data to send.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int sendData(const char* buf, int len);

    /**
     * Closes this socket object, which closes the connection so afterwards
     * data can no longer be sent and received.
     */
    void close();

private:
    /**
     * Resolves the given host address and port into one or more sockaddr
     * structures (since a host address can map to more than one IP address),
     * each suitable for passing to the winsock connect() function.
     *
     * @param hostAddr the host address to resolve.
     * @param hostPort the host port to resolve.
     * @param pAddrInfo if the method was successful this will be set to point
     * to a linked list of address information structures containing the needed
     * information. Note that since the address information is allocated
     * dynamically, the caller will need to use the winsock function
     * freeaddrinfo() to delete the address information once finished with it.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int resolveHostAddr(const char* hostAddr, unsigned short hostPort,
        ADDRINFOA** pAddrInfo);

    /**
     * The thread procedure for the host address resolver thread, which
     * resolves a host address and port in the background when connecting
     * asynchronously.
     *
     * @param hostAddr the host address to resolve.
     * @param hostPort the host port to resolve.
     * @param self a pointer to this socket object.
     */
    static void resolveHostAddrThreadProc(std::string hostAddr,
        unsigned short hostPort, SocketObj* self);

    /**
     * The first stage of construction for synchronous connection.
     *
     * @param dataRecvFn this will be called when the socket object has
     * received data.
     * @param socketClosedFn this will be called when the socket object has
     * closed.
     * @param arg this will be passed back as is in any of the socket object's
     * callback functions.
     */
    SocketObj(CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn,
        void* arg);

    /**
     * The first stage of construction for asynchronous connection.
     *
     * @param conCompletedFn this will be called when the connection attempt
     * has completed.
     * @param dataRecvFn this will be called when the socket object has
     * received data.
     * @param socketClosedFn this will be called when the socket object has
     * closed.
     * @param arg this will be passed back as is in any of the socket object's
     * callback functions.
     */
    SocketObj(CLPConCompletedFn conCompletedFn, CLPDataRecvFn dataRecvFn,
        CLPSocketClosedFn socketClosedFn, void* arg);

    /**
     * The first stage of construction for an already accepted connection.
     *
     * @param clientSocket the already accepted connection.
     * @param dataRecvFn this will be called when the client socket has
     * received data.
     * @param socketClosedFn this will be called when the client socket has
     * closed.
     * @param arg this will be passed back as is in any of the client socket's
     * callback functions.
     */
    SocketObj(SOCKET clientSocket, CLPDataRecvFn dataRecvFn,
        CLPSocketClosedFn socketClosedFn, void* arg);

    /**
     * The second stage of construction for synchronous connection.
     *
     * @param hostAddr the host address to connect to.
     * @param hostPort the host port to connect to.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int construct(const char* hostAddr, unsigned short hostPort);

    /**
     * The second stage of construction for asynchronous connection.
     *
     * @param hostAddr the host address to connect to.
     * @param hostPort the host port to connect to.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int constructAsync(const char* hostAddr, unsigned short hostPort);

    /**
     * The second stage of construction for an already accepted connection.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int constructAccepted();

    /**
     * Creates the network event for this socket object.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int createNetEvent();

    /**
     * Switches the socket to non-blocking mode.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int SetNonBlockingMode();

    /**
     * Switches the socket to blocking mode.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int SetBlockingMode();

    /**
     * This is called by the host address resolver thread once the resolve has
     * completed.
     *
     * @param addrInfo if the error code indicates the resolve was successful,
     * this will point to a linked list of address information structures
     * containing the information needed to connect. Note that since the
     * address information is allocated dynamically, this socket object will
     * need to use the winsock function freeaddrinfo() to delete the address
     * information once finished with it.
     * @param hostAddrResolvedErr this indicates whether or not the host
     * address and port was resolved successfully. If the error code
     * equals CL_ERR_OK then the resolve was successful; any other value
     * indicates the resolve failed.
     */
    void onHostAddrResolved(ADDRINFOA* addrInfo, int hostAddrResolvedErr);

    /**
     * Attempts to connect synchronously to the first address in the given
     * linked list of address information structures. If the connection attempt
     * failed then the next address in the list is tried, and so on until the
     * connection attempt succeeds.
     *
     * @param pCrntAddrInfo points to the linked list of address information
     * structures to attempt connection to. Afterwards, this will be set to
     * point to the address information structure in the list after the one
     * that connected successfully.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int doConnect(ADDRINFOA** pCrntAddrInfo);

    /**
     * Attempts to connect asynchronously to the first address in the given
     * linked list of address information structures. If the connection
     * attempt failed then the next address in the list is tried, and so on
     * until the connection attempt succeeds.
     *
     * @param pCrntAddrInfo points to the linked list of address information
     * structures to attempt connection to. Afterwards, this will be set to
     * point to the address information structure in the list after the one
     * that connected successfully.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int doConnectAsync(ADDRINFOA** pCrntAddrInfo);

    /**
     * Handles the FD_CONNECT network event.
     *
     * @param fdConnectErr the error code that was given when the network
     * connect notification was received.
     */
    void onFdConnect(int fdConnectErr);

    /** Handles the FD_READ network event. */
    void onFdRead();

    /**
     * Handles the FD_CLOSE network event.
     *
     * @param fdCloseErr the error code that was given when the network close
     * notification was received.
     */
    void onFdClose(int fdCloseErr);

    /**
     * Sends as much data as possible from the given buffer.
     *
     * @param buf the data to send.
     * @param len the length of data to send.
     * @param bytesSent this will be set to the number of bytes sent.
     * @return CL_ERR_OK if the complete contents of the buffer could be sent,
     * any other value otherwise.
     */
    int sendAll(const char* buf, int len, int& bytesSent);

    /** Synchronizes access to this object. */
    boost::mutex m_mutex;

    /**
     * This will be called when the asynchronous connection attempt has
     * completed.
     */
    CLPConCompletedFn m_conCompletedFn;

    /** This will be called when the socket object has received data. */
    CLPDataRecvFn m_dataRecvFn;

    /** This will be called when the socket object has closed. */
    CLPSocketClosedFn m_socketClosedFn;

    /**
     * This will be passed back as is in any of the socket object's callback
     * functions.
     */
    void* m_arg;

    /** The network event for this object. */
    WSAEVENT m_netEvent;

    /** The socket for this object. */
    SOCKET m_socket;

    /** This is set when close() has been called. */
    bool m_closeCalled;

    /**
     * A linked list of address information structures, each containing
     * information needed to connect. Only used when connecting asynchronously.
     */
    ADDRINFOA* m_addrInfo;

    /**
     * The address information structure in m_addrInfo to use to connect. Only
     * used when connecting asynchronously.
     */
    ADDRINFOA* m_crntAddrInfo;

    /** This is set when the host address resolver thread has completed. */
    bool m_resolveAsyncCompleted;

    /**
     * This is notified when the host address resolver thread has completed.
     */
    boost::condition_variable m_resolveAsyncCompletedCondVar;

    /**
     * This is set when the data stream sent to the remote host has corrupted.
     */
    bool m_dataStreamCorrupted;

    /** A buffer for data received. */
    std::vector<char> m_DataRecvBuf;

    /** The length of data in the data received buffer. */
    int m_DataRecvLen;
};

/** A shared pointer to a socket object. */
typedef boost::shared_ptr<SocketObj> SocketObjSPtr;
