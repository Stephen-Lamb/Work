#pragma once

#include <windows.h>
#include <map>

class Metrics
{
public:
    enum Func
    {
        CREATE_SOCKET,
        SEND_DATA,
        LAST_FUNC
    };

    Metrics();
    ~Metrics();
    void displayMetrics() const;
    void incAttemptedCons();
    void incFailedCons();
    void incClosedCons();
    void incErrorCount(Func func, int err);

private:
    static const char* const FUNC_STRINGS[];

    Metrics(const Metrics&);
    Metrics& operator=(const Metrics&);

    mutable CRITICAL_SECTION m_critSect;
    DWORD m_startTickCount;
    DWORD m_attemptedCons;
    DWORD m_failedCons;
    DWORD m_closedCons;
    std::map<int, unsigned long> m_errorCounts[LAST_FUNC];
};
