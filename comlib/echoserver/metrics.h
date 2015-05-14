#pragma once

#include <windows.h>
#include <map>

class Metrics
{
public:
    enum Func
    {
        ACCEPT_CON,
        SEND_DATA,
        LAST_FUNC
    };

    Metrics();
    ~Metrics();
    void displayMetrics() const;
    void incAcceptedCons();
    void updateSendThroughput(int bytesSent);
    void updateRecvThroughput(int bytesRecv);
    void incErrorCount(Func func, int err);

private:
    static const char* const FUNC_STRINGS[];

    Metrics(const Metrics&);
    Metrics& operator=(const Metrics&);

    mutable CRITICAL_SECTION m_critSect;
    DWORD m_startTickCount;
    DWORD m_acceptedCons;
    unsigned __int64 m_totalBytesSent;
    unsigned __int64 m_totalBytesRecv;
    std::map<int, unsigned long> m_errorCounts[LAST_FUNC];
};
