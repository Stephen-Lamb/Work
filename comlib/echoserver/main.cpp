// Echo server

#include <windows.h>
#include <iostream>
#include <comlib/comlib.h>
#include "metrics.h"

static HANDLE s_shutdownEvent = NULL;
static Metrics s_metrics;

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
        SetEvent(s_shutdownEvent);
        return TRUE;

    default:
        return FALSE;
    }
}

void dataRecv(CLSocket skt, const char* buf, int len, void* arg)
{
    s_metrics.updateRecvThroughput(len);

    // Echo the string back to the client
    int err = CLSendData(skt, buf, len);
    if (err == CL_ERR_OK)
    {
        s_metrics.updateSendThroughput(len);
    }
    else
    {
        s_metrics.incErrorCount(Metrics::SEND_DATA, err);
    }
}

void socketClosed(CLSocket skt, int err, void* arg)
{
    // This tells us that the socket was closed on the client side. Since there
    // is no longer a need to keep the socket around we delete it to free up
    // resources on the server
    CLDeleteSocket(skt);
}

void conPending(CLSrvSocket srvSkt, void* srvArg)
{
    // We have a connection pending from the client. Accept the connection
    char clientIpAddr[80];
    int clientIpAddrLen = sizeof(clientIpAddr);
    unsigned short clientPort = 0;
    CLSocket clientSkt = 0;
    int err = CLAcceptCon(srvSkt, dataRecv, socketClosed, NULL, &clientSkt,
        clientIpAddr, clientIpAddrLen, &clientPort);
    if (err == CL_ERR_OK)
    {
        s_metrics.incAcceptedCons();
    }
    else
    {
        s_metrics.incErrorCount(Metrics::ACCEPT_CON, err);
    }
}

void srvSocketClosed(CLSrvSocket srvSkt, int err, void* srvArg)
{
    // Shutdown since listening socket closed
    std::cout << "\r\nServer socket closed, err=" << err << "\r\n" <<
        std::flush;
    SetEvent(s_shutdownEvent);
}

void displayUsage()
{
    std::cout << "Sends data received from a client back to the client.\r\n\r\n";

    std::cout << "ECHOSERVER addr port\r\n\r\n";

    std::cout << "addr  The IP address the server should listen on.\r\n";
    std::cout << "port  The port the server should listen on.\r\n";
    std::cout << "\r\n";
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        displayUsage();
        return 1;
    }

    unsigned short port =
        static_cast<unsigned short>(strtoul(argv[2], NULL, 10));

    s_shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_shutdownEvent == NULL)
    {
        return 1;
    }

    if (!SetConsoleCtrlHandler(&consoleCtrlHandler, TRUE))
    {
        return 1;
    }

    // Startup the communication library
    int err = CLStartup();
    if (err != CL_ERR_OK)
    {
        std::cout << "\r\nCLStartup() failed, err=" << err << "\r\n" <<
            std::flush;
        return 1;
    }

    // Create the listening socket
    CLSrvSocket srvSkt = 0;
    err = CLCreateSrvSocket(argv[1], port, conPending, srvSocketClosed, 200,
        NULL, &srvSkt);
    if (err == CL_ERR_OK)
    {
        static const DWORD DISPLAY_INTERVAL = 5 * 60 * 1000; // 5 mins
        while (WaitForSingleObject(s_shutdownEvent, DISPLAY_INTERVAL) !=
            WAIT_OBJECT_0)
        {
            s_metrics.displayMetrics();
        }
    }
    else
    {
        std::cout << "\r\nCLCreateSrvSocket() failed, err=" << err << "\r\n" <<
            std::flush;
    }

    // Cleanup the communication library
    CLCleanup();

    s_metrics.displayMetrics();
    return 0;
}
