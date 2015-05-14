// Echo client

#include <windows.h>
#include <iostream>
#include <string>
#include <comlib/comlib.h>

static HANDLE s_shutdownEvent = NULL;
static int s_conCompletedErr = CL_ERR_OK;
static HANDLE s_conCompletedEvent = NULL;

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

void conCompleted(CLSocket skt, int err, void* arg)
{
    s_conCompletedErr = err;
    SetEvent(s_conCompletedEvent);
}

void dataRecv(CLSocket skt, const char* buf, int len, void* arg)
{
    // Output the string echoed back from the server
    std::string str(buf, len);
    std::cout << str << "\r\n" << std::flush;
}

void socketClosed(CLSocket skt, int err, void* arg)
{
    // Shutdown since socket closed
    std::cout << "\r\nSocket closed, err=" << err << "\r\n" << std::flush;
    SetEvent(s_shutdownEvent);
}

void displayUsage()
{
    std::cout << "Reads lines from standard input, sends them to a server and writes responses to\r\n";
    std::cout << "standard output.\r\n\r\n";

    std::cout << "ECHOCLIENT addr port [/A]\r\n\r\n";

    std::cout << "addr  The host address the client should connect to.\r\n";
    std::cout << "port  The port the client should connect to.\r\n";
    std::cout << "/A    Tells the client that it should connect asynchronously.\r\n";
    std::cout << "\r\n";
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        displayUsage();
        return 1;
    }

    unsigned short port =
        static_cast<unsigned short>(strtoul(argv[2], NULL, 10));
    bool connectAsync = false;

    for (int i = 3; i < argc; ++i)
    {
        if (_stricmp(argv[i], "/A") == 0)
        {
            connectAsync = true;
        }
    }

    s_shutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_shutdownEvent == NULL)
    {
        return 1;
    }

    s_conCompletedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_conCompletedEvent == NULL)
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

    // Connect to the host
    CLSocket skt = 0;
    if (connectAsync)
    {
        err = CLCreateSocketAsync(argv[1], port, conCompleted, dataRecv,
            socketClosed, NULL, &skt);
        if (err == CL_ERR_OK)
        {
            HANDLE events[2];
            events[0] = s_shutdownEvent;
            events[1] = s_conCompletedEvent;
            DWORD waitRes = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (waitRes == WAIT_OBJECT_0 + 1)
            {
                err = s_conCompletedErr;
                if (err != CL_ERR_OK)
                {
                    std::cout << "\r\nConnection attempt failed, err=" <<
                        err << "\r\n" << std::flush;
                }
            }
        }
        else
        {
            std::cout << "\r\nCLCreateSocketAsync() failed, err=" << err <<
                "\r\n" << std::flush;
        }
    }
    else
    {
        err = CLCreateSocket(argv[1], port, dataRecv, socketClosed, NULL,
            &skt);
        if (err != CL_ERR_OK)
        {
            std::cout << "\r\nCLCreateSocket() failed, err=" << err <<
                "\r\n" << std::flush;
        }
    }

    if (err == CL_ERR_OK)
    {
        // Read lines from standard input and send them to the host
        std::string line;
        while (WaitForSingleObject(s_shutdownEvent, 0) != WAIT_OBJECT_0 &&
            std::getline(std::cin, line))
        {
            err = CLSendData(skt, line.c_str(),
                static_cast<int>(line.length()));
            if (err != CL_ERR_OK)
            {
                std::cout << "\r\nCLSendData() failed, err=" << err <<
                    "\r\n" << std::flush;
            }
        }
    }

    // Cleanup the communication library
    CLCleanup();

    return 0;
}
