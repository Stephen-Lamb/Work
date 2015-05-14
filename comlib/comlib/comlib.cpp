/**
 * @file
 * Definitions for the "comlib" communication library.
 */

#include "inc/comlib/comlib.h"
#include <winsock2.h>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "debug.h"
#include "netthreadpool.h"
#include "socketobj.h"
#include "socketregistry.h"
#include "srvsocketobj.h"

// Provides exclusive or shared access to the library
static boost::shared_mutex s_libMutex;
// The number of times the library has been started up
static int s_startupCount = 0;
// A registry for server socket objects
static SrvSocketRegistry s_srvSocketRegistry;
// A registry for socket objects
static SocketRegistry s_socketRegistry;
// The library's pool of network threads
static NetThreadPool s_netThreadPool;
// Are we currently uninitializing the library?
static bool s_uninitializing = false;
// This will be notified when the library is no longer being uninitialized
static boost::condition_variable_any s_uninitializingCondVar;

int finishCreateSrvSocketObj(SrvSocketObj* rawSrvSktObj, CLSrvSocket* pSrvSkt)
{
    assert(rawSrvSktObj != 0);

    SrvSocketObjSPtr srvSktObj(rawSrvSktObj);

    // Add server socket object to registry
    CLSrvSocket srvSkt = SrvSocketRegistry::toHandle(rawSrvSktObj);
    s_srvSocketRegistry.addSocketObj(srvSkt, srvSktObj);

    // Add server socket object to network thread pool
    int err = s_netThreadPool.addNetObj(srvSktObj);
    if (err == CL_ERR_OK)
    {
        *pSrvSkt = srvSkt;
    }
    else
    {
        s_srvSocketRegistry.removeSocketObj(srvSkt);
        srvSktObj->close();
    }

    return err;
}

void closeSrvSocketObj(const SrvSocketObjSPtr& srvSktObj)
{
    assert(srvSktObj.get() != 0);

    // Remove server socket object from network thread pool
    s_netThreadPool.removeNetObj(srvSktObj);
    // Close server socket object
    srvSktObj->close();
}

int finishCreateSocketObj(SocketObj* rawSktObj, CLSocket* pSkt)
{
    assert(rawSktObj != 0);

    SocketObjSPtr sktObj(rawSktObj);

    // Add socket object to registry
    CLSocket skt = SocketRegistry::toHandle(rawSktObj);
    s_socketRegistry.addSocketObj(skt, sktObj);

    // Add socket object to network thread pool
    int err = s_netThreadPool.addNetObj(sktObj);
    if (err == CL_ERR_OK)
    {
        *pSkt = skt;
    }
    else
    {
        s_socketRegistry.removeSocketObj(skt);
        sktObj->close();
    }

    return err;
}

void closeSocketObj(const SocketObjSPtr& sktObj)
{
    assert(sktObj.get() != 0);

    // Remove socket object from network thread pool
    s_netThreadPool.removeNetObj(sktObj);
    // Close socket object
    sktObj->close();
}

extern "C" __declspec(dllexport) int __cdecl CLStartup(void)
{
    // Gain exclusive access to the library
    boost::unique_lock<boost::shared_mutex> lock(s_libMutex);

    while (s_uninitializing)
    {
        s_uninitializingCondVar.wait(lock);
    }

    int err = CL_ERR_OK;
    ++s_startupCount;

    if (s_startupCount == 1)
    {
        // Startup the Winsock library
        WSADATA wsaData;
        int wsaStartupErr = WSAStartup(MAKEWORD(2,2), &wsaData);
        if (wsaStartupErr != 0)
        {
            // Winsock startup failed
            err = wsaStartupErr;
            --s_startupCount;
        }
    }

    return err;
}

extern "C" __declspec(dllexport) void __cdecl CLCleanup(void)
{
    // Gain exclusive access to the library
    boost::unique_lock<boost::shared_mutex> lock(s_libMutex);

    while (s_uninitializing)
    {
        s_uninitializingCondVar.wait(lock);
    }

    --s_startupCount;

    if (s_startupCount == 0)
    {
        // Close server socket objects
        SrvSocketObjSPtr srvSktObj =
            s_srvSocketRegistry.removeFrontSocketObj();
        while (srvSktObj.get() != 0)
        {
            closeSrvSocketObj(srvSktObj);
            srvSktObj = s_srvSocketRegistry.removeFrontSocketObj();
        }

        // Close socket objects
        SocketObjSPtr sktObj = s_socketRegistry.removeFrontSocketObj();
        while (sktObj.get() != 0)
        {
            closeSocketObj(sktObj);
            sktObj = s_socketRegistry.removeFrontSocketObj();
        }

        // Wait for the net thread pool to shutdown. Note that we need to
        // unlock the library mutex while we do this otherwise network threads
        // in the pool may block for quite a while when attempting to aquire
        // the mutex. However, we don't want these threads to do anything
        // during this time so we set a special "uninitializing" flag to ensure
        // this.
        s_uninitializing = true;
        lock.unlock();
        // How long we are prepared to wait in milliseconds for the network
        // threads in the pool to shutdown
        static const DWORD SHUTDOWN_TIMEOUT_INTERVAL = 10000;
        if (!s_netThreadPool.waitForShutdown(SHUTDOWN_TIMEOUT_INTERVAL))
        {
            OUTPUT_FMT_DEBUG_STRING("Network thread pool did not shutdown in "
                << SHUTDOWN_TIMEOUT_INTERVAL << "ms");
        }
        lock.lock();
        s_uninitializing = false;
        s_uninitializingCondVar.notify_all();

        // TODO: Test if this is really needed
        // Sleep a little bit so that the destructors of socket and thread
        // objects in the library can complete
        Sleep(500 /*Milliseconds*/);

        // Cleanup the Winsock library
        WSACleanup();
    }
}

extern "C" __declspec(dllexport) int __cdecl CLCreateSrvSocket(
    const char* ipAddr, unsigned short port, CLPConPendingFn conPendingFn,
    CLPSrvSocketClosedFn srvSocketClosedFn, int conBacklog, void* srvArg,
    CLSrvSocket* pSrvSkt)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return CL_ERR_NOT_INITIALIZED;
    }

    if (ipAddr == 0 || port > 65535 || conPendingFn == 0 ||
        srvSocketClosedFn == 0 || pSrvSkt == 0)
    {
        return CL_ERR_ILLEGAL_ARG;
    }

    // Create server socket object
    SrvSocketObj* srvSktObj = 0;
    int err = SrvSocketObj::create(ipAddr, port, conPendingFn,
        srvSocketClosedFn, conBacklog, srvArg, &srvSktObj);
    if (err == CL_ERR_OK)
    {
        err = finishCreateSrvSocketObj(srvSktObj, pSrvSkt);
    }

    return err;
}

extern "C" __declspec(dllexport) int __cdecl CLAcceptCon(CLSrvSocket srvSkt,
    CLPDataRecvFn dataRecvFn, CLPSocketClosedFn socketClosedFn, void* arg,
    CLSocket* pClientSkt, char* clientIpAddr, int clientIpAddrLen,
    unsigned short* pClientPort)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return CL_ERR_NOT_INITIALIZED;
    }

    if (dataRecvFn == 0 || socketClosedFn == 0 || pClientSkt == 0 ||
        clientIpAddrLen < 0)
    {
        return CL_ERR_ILLEGAL_ARG;
    }

    SrvSocketObjSPtr srvSktObj = s_srvSocketRegistry.findSocketObj(srvSkt);
    if (srvSktObj.get() == 0)
    {
        return CL_ERR_SOCKET_NOT_FOUND;
    }

    // Accept the connection then create the client socket obj
    SOCKET acceptedSocket = INVALID_SOCKET;
    int err = srvSktObj->acceptConnection(&acceptedSocket, clientIpAddr,
        clientIpAddrLen, pClientPort);
    if (err == CL_ERR_OK)
    {
        // Create the client socket obj given the accepted socket
        SocketObj* clientSktObj = 0;
        err = SocketObj::createAccepted(acceptedSocket, dataRecvFn,
            socketClosedFn, arg, &clientSktObj);
        if (err == CL_ERR_OK)
        {
            err = finishCreateSocketObj(clientSktObj, pClientSkt);
        }
    }

    return err;
}

extern "C" __declspec(dllexport) void __cdecl CLDeleteSrvSocket(
    CLSrvSocket srvSkt)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return;
    }

    // Remove server socket object from registry
    SrvSocketObjSPtr srvSktObj = s_srvSocketRegistry.removeSocketObj(srvSkt);
    if (srvSktObj.get() == 0)
    {
        // Server socket object not found
        return;
    }

    closeSrvSocketObj(srvSktObj);
}

extern "C" __declspec(dllexport) int __cdecl CLCreateSocket(
    const char* hostAddr, unsigned short hostPort, CLPDataRecvFn dataRecvFn,
    CLPSocketClosedFn socketClosedFn, void* arg, CLSocket* pSkt)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return CL_ERR_NOT_INITIALIZED;
    }

    if (hostAddr == 0 || hostPort > 65535 || dataRecvFn == 0 ||
        socketClosedFn == 0 || pSkt == 0)
    {
        return CL_ERR_ILLEGAL_ARG;
    }

    // Create socket object
    SocketObj* sktObj = 0;
    int err = SocketObj::create(hostAddr, hostPort, dataRecvFn,
        socketClosedFn, arg, &sktObj);
    if (err == CL_ERR_OK)
    {
        err = finishCreateSocketObj(sktObj, pSkt);
    }

    return err;
}

extern "C" __declspec(dllexport) int __cdecl CLCreateSocketAsync(
    const char* hostAddr, unsigned short hostPort,
    CLPConCompletedFn conCompletedFn, CLPDataRecvFn dataRecvFn,
    CLPSocketClosedFn socketClosedFn, void* arg, CLSocket* pSkt)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return CL_ERR_NOT_INITIALIZED;
    }

    if (hostAddr == 0 || hostPort > 65535 || conCompletedFn == 0 ||
        dataRecvFn == 0 || socketClosedFn == 0 || pSkt == 0)
    {
        return CL_ERR_ILLEGAL_ARG;
    }

    // Create socket object
    SocketObj* sktObj = 0;
    int err = SocketObj::createAsync(hostAddr, hostPort, conCompletedFn,
        dataRecvFn, socketClosedFn, arg, &sktObj);
    if (err == CL_ERR_OK)
    {
        err = finishCreateSocketObj(sktObj, pSkt);
    }

    return err;
}

extern "C" __declspec(dllexport) int __cdecl CLSendData(
    CLSocket skt, const char* buf, int len)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return CL_ERR_NOT_INITIALIZED;
    }

    if (buf == 0 || len < 0)
    {
        return CL_ERR_ILLEGAL_ARG;
    }

    if (len > SocketObj::DATA_MAX_LEN)
    {
        return CL_ERR_BUF_TOO_BIG;
    }

    SocketObjSPtr sktObj = s_socketRegistry.findSocketObj(skt);
    if (sktObj.get() == 0)
    {
        return CL_ERR_SOCKET_NOT_FOUND;
    }

    if (len == 0)
    {
        // Treat sending a buffer of length 0 as a null operation
        return CL_ERR_OK;
    }

    return sktObj->sendData(buf, len);
}

extern "C" __declspec(dllexport) void __cdecl CLDeleteSocket(CLSocket skt)
{
    // Gain shared access to the library
    boost::shared_lock<boost::shared_mutex> lock(s_libMutex);

    if (s_startupCount <= 0)
    {
        return;
    }

    // Remove socket object from registry
    SocketObjSPtr sktObj = s_socketRegistry.removeSocketObj(skt);
    if (sktObj.get() == 0)
    {
        // Socket object not found
        return;
    }

    closeSocketObj(sktObj);
}
