/**
 * @file
 * Declarations for the "comlib" communication library.
 */

/**
 * @mainpage
 * comlib is a Windows DLL that simplifies using TCP/IP for communication. It
 * tries to do all the hard work for you. For example, it imposes a packet
 * scheme on TCP for you (though this does make the library unsuitable if you
 * require data to be streamed). The library supports IPv4, IPv6 on systems
 * that have it installed, and will also resolve host names to IP addresses for
 * you (via DNS lookup or a "hosts" file).
 *
 * The library is thread-safe and is suitable for clients and servers that
 * receive a moderate number of connections. The file comlib.h contains all
 * declarations needed to use the library.
 *
 * comlib.dll is dependent on the following DLL's and requires them to be in
 * the DLL search path if you wish to use the library:
 *
 * For Debug builds:
 *   - boost_date_time-vc80-mt-gd-1_37.dll
 *   - boost_thread-vc80-mt-gd-1_37.dll
 *
 * For Release builds:
 *   - boost_date_time-vc80-mt-1_37.dll
 *   - boost_thread-vc80-mt-1_37.dll
 *
 * Also, as comlib.dll is built with the multithread- and DLL-specific version
 * of the Microsoft Visual C++ run-time library, either Microsoft Visual Studio
 * 2005 SP1 needs to be installed on the deployment machine or "Microsoft
 * Visual C++ 2005 SP1 Redistributable Package (x86)", available from
 * Microsoft's website, needs to be installed.
 *
 * comlib.dll works on Windows 2000/XP and later versions of Windows, earlier
 * versions are not supported.
 */

#ifndef COMLIB_H
#define COMLIB_H

#if _MSC_VER > 1000
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef COMLIB_EXPORTS
#define COMLIB_LIBSPEC __declspec(dllexport)
#else
#define COMLIB_LIBSPEC __declspec(dllimport)
#endif

/** This is returned when a function call was successful. */
#define CL_ERR_OK 0
/**
 * This is returned when the given buffer is too long. There is a limit of
 * 65535 bytes per request.
 */
#define CL_ERR_BUF_TOO_BIG -1
/**
 * This is returned when the data stream sent to the remote host has corrupted.
 * The only way to recover is to delete then recreate the socket and try
 * resending the data.
 */
#define CL_ERR_DATA_STREAM_CORRUPTED -2
/** This is returned when one of the given arguments has an illegal value. */
#define CL_ERR_ILLEGAL_ARG -3
/** This is returned when the library has not been initialized. */
#define CL_ERR_NOT_INITIALIZED -4
/** This is returned when the given socket or server socket was not found. */
#define CL_ERR_SOCKET_NOT_FOUND -5

struct CLSrvSocket__;
/** Represents a server socket. */
typedef struct CLSrvSocket__* CLSrvSocket;

struct CLSocket__;
/** Represents a socket. */
typedef struct CLSocket__* CLSocket;

/**
 * This will be called when a client connection is pending for the specified
 * server socket. The function CLAcceptCon() can then be called to accept the
 * connection.
 *
 * @param srvSkt the server socket that has a client connection pending.
 * @param srvArg an optional argument that was specified when the server socket
 * was created.
 */
typedef void (__cdecl *CLPConPendingFn)(CLSrvSocket srvSkt, void* srvArg);
/**
 * This will be called when the specified server socket has been closed.
 *
 * @param srvSkt the server socket that has been closed.
 * @param err the error code that was given when the network close notification
 * was received.
 * @param srvArg an optional argument that was specified when the server socket
 * was created.
 */
typedef void (__cdecl *CLPSrvSocketClosedFn)(CLSrvSocket srvSkt, int err,
                                             void* srvArg);
/**
 * This will be called when the specified socket was created via
 * CLCreateSocketAsync() and the connection attempt has completed.
 *
 * @param skt the socket whose connection attempt has completed.
 * @param err this specifies whether or not the connection attempt was
 * successful. If the error code equals CL_ERR_OK then the connection attempt
 * was successful; any other value indicates the connection attempt failed.
 * @param arg an optional argument that was specified when the socket was
 * created.
 */
typedef void (__cdecl *CLPConCompletedFn)(CLSocket skt, int err, void* arg);
/**
 * This will be called when the specified socket received data. Note that the
 * buffer will contain all of the data following any length prefix (for more
 * information, see the documentation for CLSendData()).
 *
 * @param skt the socket that received data.
 * @param buf the data that was received.
 * @param len the length of data that was received.
 * @param arg an optional argument that was specified when the socket was
 * created.
 */
typedef void (__cdecl *CLPDataRecvFn)(CLSocket skt, const char* buf, int len,
                                      void* arg);
/**
 * This will be called when the specified socket has been closed, usually by
 * the remote host.
 *
 * @param skt the socket that has been closed.
 * @param err the error code that was given when the network close notification
 * was received.
 * @param arg an optional argument that was specified when the socket was
 * created.
 */
typedef void (__cdecl *CLPSocketClosedFn)(CLSocket skt, int err, void* arg);

/**
 * Initializes the communication library. This function needs to be called
 * before any of the other library functions. It is okay to call this function
 * more than once, however, for every successful call to this function (return
 * value CL_ERR_OK) there must be a corresponding call to CLCleanup() when the
 * library is no longer needed.
 *
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLStartup(void);

/**
 * Uninitializes the communication library, which closes and deletes any open
 * sockets and server sockets and frees all other resources.
 */
COMLIB_LIBSPEC void __cdecl CLCleanup(void);

/**
 * Creates a TCP server socket that is listening on the given local IP address
 * and port.
 *
 * @param ipAddr the IP address (both IPv4 and IPv6 are supported) that the
 * server socket will listen on.
 * @param port the port that the server socket will listen on.
 * @param conPendingFn a pointer to a function that will be called when the
 * server socket has a connection request from a client pending.
 * @param srvSocketClosedFn a pointer to a function that will be called when
 * the server socket has closed.
 * @param conBacklog the maximum number of entries that the server socket can
 * have in it's queue of pending connections at any particular time. A
 * reasonable value to pick would be something in the 5 to 200 range.
 * @param srvArg an optional argument that will be passed back as is in any of
 * the server socket's callback functions.
 * @param pSrvSkt if the function was successful this will be set to point to
 * the server socket that was created.
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLCreateSrvSocket(const char* ipAddr,
    unsigned short port, CLPConPendingFn conPendingFn,
    CLPSrvSocketClosedFn srvSocketClosedFn, int conBacklog, void* srvArg,
    CLSrvSocket* pSrvSkt);

/**
 * Accepts a connection from a client to the specified TCP server socket if one
 * is pending.
 *
 * @param srvSkt the server socket that will accept the connection.
 * @param dataRecvFn a pointer to a function that will be called when the
 * client socket has received data.
 * @param socketClosedFn a pointer to a function that will be called when the
 * client socket has closed.
 * @param arg an optional argument that will be passed back as is in any of the
 * client socket's callback functions.
 * @param pClientSkt if the function was successful this will be set to point
 * to the accepted client socket.
 * @param clientIpAddr if the function was successful this will be filled in
 * with the IP address of the client. This must be long enough to hold the
 * entire IP address (IPv4 or IPv6 depending on the server socket) otherwise an
 * error will be returned. If the IP address is not required then this may be
 * NULL.
 * @param clientIpAddrLen the length of the given client IP address buffer.
 * @param pClientPort if the function was successful the variable pointed to
 * will be set to the port the client connected from. If the port is not
 * required then this may be NULL.
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLAcceptCon(CLSrvSocket srvSkt,
    CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn, void* arg,
    CLSocket* pClientSkt, char* clientIpAddr, int clientIpAddrLen,
    unsigned short* pClientPort);

/**
 * Closes the specified server socket and frees any resources allocated to it.
 *
 * @param srvSkt the server socket to be deleted.
 */
COMLIB_LIBSPEC void __cdecl CLDeleteSrvSocket(CLSrvSocket srvSkt);

/**
 * Creates a TCP socket that is connected to the given host address and port.
 * Note that the connection attempt is synchronous, therefore this function may
 * take a minute or more to return. If this is a problem use
 * CLCreateSocketAsync().
 *
 * @param hostAddr the host address to connect to. This may be either a host
 * name (in which case it will be resolved to an IP address via DNS lookup or a
 * "hosts" file) or an IPv4 or IPv6 address.
 * @param hostPort the host port to connect to.
 * @param dataRecvFn a pointer to a function that will be called when the
 * socket has received data.
 * @param socketClosedFn a pointer to a function that will be called when the
 * socket has closed.
 * @param arg an optional argument that will be passed back as is in any of the
 * socket's callback functions.
 * @param pSkt if the function was successful this will be set to point to
 * the socket that was created.
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLCreateSocket(const char* hostAddr,
    unsigned short hostPort, CLPDataRecvFn dataRecvFn,
    CLPSocketClosedFn socketClosedFn, void* arg, CLSocket* pSkt);

/**
 * Creates a TCP socket that connects asynchronously to the given host address
 * and port. Note that the connection attempt is asynchronous, therefore this
 * function will return before the connection is complete. If this function was
 * successful, the function pointed to by conCompletedFn will be called when
 * the connection attempt has completed.
 *
 * @param hostAddr the host address to connect to. This may be either a host
 * name (in which case it will be resolved to an IP address via DNS lookup or a
 * "hosts" file) or an IPv4 or IPv6 address.
 * @param hostPort the host port to connect to.
 * @param conCompletedFn a pointer to a function that will be called when the
 * connection attempt has completed (either successfully or unsuccessfully).
 * @param dataRecvFn a pointer to a function that will be called when the
 * socket has received data.
 * @param socketClosedFn a pointer to a function that will be called when the
 * socket has closed.
 * @param arg an optional argument that will be passed back as is in any of the
 * socket's callback functions.
 * @param pSkt if the function was successful this will be set to point to
 * the socket that was created.
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLCreateSocketAsync(const char* hostAddr,
    unsigned short hostPort, CLPConCompletedFn conCompletedFn,
    CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn, void* arg,
    CLSocket* pSkt);

/**
 * Sends data using the specified socket.
 *
 * Note that the data sent is prefixed with an unsigned 2-byte value in network
 * byte order to indicate the length of data following. This implies that the
 * buffer length specified must be less than or equal to 65535 bytes otherwise
 * an error will be returned.
 *
 * @param skt the socket to use to send the data.
 * @param buf the data to send.
 * @param len the length of data to send.
 * @return CL_ERR_OK if the function was successful, any other value otherwise.
 */
COMLIB_LIBSPEC int __cdecl CLSendData(CLSocket skt, const char* buf, int len);

/**
 * Closes the specified socket and frees any resources allocated to it.
 *
 * @param skt the socket to be deleted.
 */
COMLIB_LIBSPEC void __cdecl CLDeleteSocket(CLSocket skt);

#undef COMLIB_LIBSPEC

#ifdef __cplusplus
}
#endif

#endif
