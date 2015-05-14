#include "metrics.h"
#include <cassert>
#include <iostream>

const char* const Metrics::FUNC_STRINGS[] =
{
    "CLCreateSocket",
    "CLSendData"
};

Metrics::Metrics() : m_attemptedCons(0), m_failedCons(0), m_closedCons(0)
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
    std::cout << "Attempted cons: " << m_attemptedCons << "\r\n";
    std::cout << "Failed cons   : " << m_failedCons << "\r\n";
    std::cout << "Closed cons   : " << m_closedCons << "\r\n";
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

void Metrics::incAttemptedCons()
{
    EnterCriticalSection(&m_critSect);
    ++m_attemptedCons;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::incFailedCons()
{
    EnterCriticalSection(&m_critSect);
    ++m_failedCons;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::incClosedCons()
{
    EnterCriticalSection(&m_critSect);
    ++m_closedCons;
    LeaveCriticalSection(&m_critSect);
}

void Metrics::incErrorCount(Func func, int err)
{
    EnterCriticalSection(&m_critSect);
    assert(func < LAST_FUNC);
    ++m_errorCounts[func][err];
    LeaveCriticalSection(&m_critSect);
}
