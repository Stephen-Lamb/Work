/**
 * @file
 * Declares the SrvSocketObj class.
 */

#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include "inc/comlib/comlib.h"
#include "netobj.h"

/**
 * Represents a TCP socket that listens for connections on a local IP address
 * and port.
 */
class SrvSocketObj : public NetObj
{
public:
    // Inherited from NetObj
    virtual WSAEVENT netEvent() const;
    virtual void onNetEvent();

    /**
     * Creates a server socket object that is listening on the given local
     * IP address and port.
     *
     * @param ipAddr the IP address that the server socket object will listen
     * on.
     * @param port the port that the server socket object will listen on.
     * @param conPendingFn this will be called when the server socket object
     * has a connection request from a client pending.
     * @param srvSocketClosedFn this will be called when the server socket
     * object has closed.
     * @param conBacklog the maximum number of entries that the server socket
     * object can have in it's queue of pending connections at any particular
     * time.
     * @param srvArg this will be passed back as is in any of the server socket
     * object's callback functions.
     * @param pSrvSktObj if the method was successful this will be set to point
     * to the server socket object that was created.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int create(const char* ipAddr, unsigned short port,
        CLPConPendingFn conPendingFn, CLPSrvSocketClosedFn srvSocketClosedFn,
        int conBacklog, void* srvArg, SrvSocketObj** pSrvSktObj);

    virtual ~SrvSocketObj();

    /**
     * Accepts a connection from a client if one is pending.
     *
     * @param pAcceptedSocket if the method was successful this will be set to
     * point to the accepted client socket.
     * @param clientIpAddr if the method was successful and this is not NULL
     * this will be filled in with the IP address of the client.
     * @param clientIpAddrLen the length of the given client IP address buffer.
     * @param pClientPort if the method was successful and this is not NULL the
     * variable pointed to will be set to the port the client connected from.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int acceptConnection(SOCKET* pAcceptedSocket, char* clientIpAddr,
        int clientIpAddrLen, unsigned short* pClientPort);

    /**
     * Closes this server socket object, which stops listening so afterwards
     * connections can no longer be accepted.
     */
    void close();

private:
    /**
     * Resolves the given IP address and port into a sockaddr structure
     * suitable for passing to the winsock bind() function.
     *
     * @param ipAddr the IP address to resolve.
     * @param port the port to resolve.
     * @param pAddrInfo if the method was successful this will be set to point
     * to an address information structure containing the needed information.
     * Note that since the address information is allocated dynamically, the
     * caller will need to use the winsock function freeaddrinfo() to delete
     * the address information once finished with it.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int resolveIpAddr(const char* ipAddr, unsigned short port,
        ADDRINFOA** pAddrInfo);

    /**
     * The first stage of construction.
     *
     * @param conPendingFn this will be called when the server socket object
     * has a connection request from a client pending.
     * @param srvSocketClosedFn this will be called when the server socket
     * object has closed.
     * @param conBacklog the maximum number of entries that the server socket
     * object can have in it's queue of pending connections at any particular
     * time.
     * @param srvArg this will be passed back as is in any of the server socket
     * object's callback functions.
     */
    SrvSocketObj(CLPConPendingFn conPendingFn,
        CLPSrvSocketClosedFn srvSocketClosedFn, int conBacklog, void* srvArg);

    /**
     * The second stage of construction.
     *
     * @param ipAddr the IP address that the server socket object will listen
     * on.
     * @param port the port that the server socket object will listen on.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int construct(const char* ipAddr, unsigned short port);

    /**
     * Creates the network event for this server socket object.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int createNetEvent();

    /** Handles the FD_ACCEPT network event. */
    void onFdAccept();

    /**
     * Handles the FD_CLOSE network event.
     *
     * @param fdCloseErr the error code that was given when the network close
     * notification was received.
     */
    void onFdClose(int fdCloseErr);

    /** Synchronizes access to this object. */
    boost::mutex m_mutex;

    /**
     * This will be called when the server socket object has a connection
     * request from a client pending.
     */
    CLPConPendingFn m_conPendingFn;

    /** This will be called when the server socket object has closed. */
    CLPSrvSocketClosedFn m_srvSocketClosedFn;

    /**
     * The maximum number of entries that the server socket object can have in
     * it's queue of pending connections at any particular time.
     */
    int m_conBacklog;

    /**
     * This will be passed back as is in any of the server socket object's
     * callback functions.
     */
    void* m_srvArg;

    /** The network event for this object. */
    WSAEVENT m_netEvent;

    /** The socket for this object. */
    SOCKET m_socket;

    /** This is used when accepting a connection from a client. */
    SOCKADDR* m_clientAddr;

    /** The length of the structure pointed to by m_clientAddr. */
    int m_clientAddrLen;
};

/** A shared pointer to a server socket object. */
typedef boost::shared_ptr<SrvSocketObj> SrvSocketObjSPtr;
