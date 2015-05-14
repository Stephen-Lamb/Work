// Stress test client

#include <process.h>
#include <windows.h>
#include <iostream>
#include <comlib/comlib.h>
#include "metrics.h"

static HANDLE s_shutdownEvent = NULL;
static Metrics s_metrics;
static DWORD s_sendInterval = 0;
static const char* s_data = 0;
static int s_dataLen = 0;

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
    // Do nothing
}

void socketClosed(CLSocket skt, int err, void* arg)
{
    s_metrics.incClosedCons();
}

unsigned __stdcall threadProc(void* arglist)
{
    CLSocket skt = static_cast<CLSocket>(arglist);
    while (WaitForSingleObject(s_shutdownEvent, 0) != WAIT_OBJECT_0)
    {
        int err = CLSendData(skt, s_data, s_dataLen);
        if (err != CL_ERR_OK)
        {
            s_metrics.incErrorCount(Metrics::SEND_DATA, err);
        }
        WaitForSingleObject(s_shutdownEvent, s_sendInterval);
    }
    return 0;
}

void displayUsage()
{
    std::cout << "Continuously sends data to a server using multiple connections.\r\n\r\n";

    std::cout << "STRESSTEST addr port cons con_int send_int data\r\n\r\n";

    std::cout << "addr     The host address the client should connect to.\r\n";
    std::cout << "port     The port the client should connect to.\r\n";
    std::cout << "cons     The number of connections the client should make.\r\n";
    std::cout << "con_int  The period in ms between connection attempts.\r\n";
    std::cout << "send_int The period in ms between sends of data.\r\n";
    std::cout << "data     The data to send.\r\n";
    std::cout << "\r\n";
}

int main(int argc, char* argv[])
{
    if (argc != 7)
    {
        displayUsage();
        return 1;
    }

    unsigned short port = static_cast<unsigned short>(
        strtoul(argv[2], NULL, 10));
    DWORD numCons = strtoul(argv[3], NULL, 10);
    DWORD conInterval = strtoul(argv[4], NULL, 10);
    s_sendInterval = strtoul(argv[5], NULL, 10);
    s_data = argv[6];
    s_dataLen = static_cast<int>(strlen(s_data));

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

    // Create the sockets and threads for the sockets
    HANDLE* threads = new HANDLE[numCons];
    DWORD idx = 0;
    while (WaitForSingleObject(s_shutdownEvent, 0) != WAIT_OBJECT_0 &&
        idx < numCons)
    {
        bool threadCreated = false;
        // Create the socket
        CLSocket skt = 0;
        s_metrics.incAttemptedCons();
        err = CLCreateSocket(argv[1], port, dataRecv, socketClosed, NULL,
            &skt);
        if (err == CL_ERR_OK)
        {
            // Successfully created socket. Create the thread for the socket
            threads[idx] = reinterpret_cast<HANDLE>(
                _beginthreadex(NULL, 0, &threadProc, skt, 0, NULL));
            if (threads[idx] != 0)
            {
                // Successfully created thread
                threadCreated = true;
            }
            else
            {
                CLDeleteSocket(skt);
                skt = 0;
            }

            if (idx < numCons - 1)
            {
                WaitForSingleObject(s_shutdownEvent, conInterval);
            }
        }
        else
        {
            s_metrics.incFailedCons();
            s_metrics.incErrorCount(Metrics::CREATE_SOCKET, err);
        }

        if (threadCreated)
        {
            ++idx;
        }
        else
        {
            --numCons;
        }
    }

    if (numCons > 0)
    {
        static const DWORD DISPLAY_INTERVAL = 5 * 60 * 1000; // 5 mins
        while (WaitForSingleObject(s_shutdownEvent, DISPLAY_INTERVAL) !=
            WAIT_OBJECT_0)
        {
            s_metrics.displayMetrics();
        }
    }

    // Wait for all the threads to finish
    idx = 0;
    while (idx < numCons)
    {
        DWORD count = min(numCons - idx, MAXIMUM_WAIT_OBJECTS);
        WaitForMultipleObjects(count, threads + idx, TRUE, INFINITE);
        idx += count;
    }

    // Cleanup the communication library
    CLCleanup();

    s_metrics.displayMetrics();
    return 0;
}
