#include "metrics.h"
#include <cassert>
#include <iostream>

const char* const Metrics::FUNC_STRINGS[] =
{
    "CLAcceptCon",
    "CLSendData"
};

Metrics::Metrics() : m_acceptedCons(0), m_totalBytesSent(0),
m_totalBytesRecv(0)
{
    assert((sizeof(FUNC_STRINGS) / sizeof(FUNC_STRINGS[0])) == LAST_FUNC);
        // Func strings not in sync with functions?

    m_startTickCount = GetTickCount();
    InitializeCriticalSection(&m_critSect);
}

Metrics::~Metrics()
{
    DeleteCriticalSection(&m_critSect);
}

void Metrics::displayMetrics() const
{
    EnterCriticalSection(&m_critSect);

    DWORD runTime = max(1, ((GetTickCount() - m_startTickCount) / 1000));
    static const DWORD SECS_PER_MIN = 60;
    static const DWORD SECS_PER_HOUR = 60 * 60;
    DWORD hours = runTime / SECS_PER_HOUR;
    DWORD secs = runTime % SECS_PER_HOUR;
    DWORD mins = secs / SECS_PER_MIN;
    secs %= SECS_PER_MIN;

    std::cout << "\r\n";
    std::cout << "Run time      : " << hours << "h " << mins << "m " <<
        secs << "s\r\n";
    std::cout << "Accepted cons : " << m_acceptedCons << "\r\n";
    std::cout << "Bytes sent    : " << m_totalBytesSent << "\r\n";
    std::cout << "Bytes sent/sec: " << (m_totalBytesSent / runTime) << "\r\n";
    std::cout << "Bytes recv    : " << m_totalBytesRecv << "\r\n";
    std::cout << "Bytes recv/sec: " << (m_totalBytesRecv / runTime) << "\r\n";
    std::cout << "Errors:\r\n";

    for (size_t idx = 0; idx < LAST_FUNC; ++idx)
    {
        std::cout << "  Function " << FUNC_STRINGS[idx] << ":\r\n";
        for (std::map<int, unsigned long>::const_iterator it =
            m_errorCounts[idx].begin(); it != m_errorCounts[idx].end(); ++it)
        {
            std::cout << "    Error: " << it->first <<
                "\tCount: " << it->second << "\r\n";
        }
    }

    std::cout << std::flush;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::incAcceptedCons()
{
    EnterCriticalSection(&m_critSect);
    ++m_acceptedCons;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::updateSendThroughput(int bytesSent)
{
    EnterCriticalSection(&m_critSect);
    m_totalBytesSent += bytesSent;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::updateRecvThroughput(int bytesRecv)
{
    EnterCriticalSection(&m_critSect);
    m_totalBytesRecv += bytesRecv;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::incErrorCount(Func func, int err)
{
    EnterCriticalSection(&m_critSect);
    assert(func < LAST_FUNC);
    ++m_errorCounts[func][err];
    LeaveCriticalSection(&m_critSect);
}
