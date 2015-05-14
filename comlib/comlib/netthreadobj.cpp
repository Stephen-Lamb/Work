/**
 * @file
 * Defines the NetThreadObj class.
 */

#include "netthreadobj.h"
#include <algorithm>
#include <boost/thread/locks.hpp>
#include "inc/comlib/comlib.h"

int NetThreadObj::create(NetThreadObj** pNetThreadObj)
{
    NetThreadObj* self = new NetThreadObj;
    int err = self->construct();
    if (err == CL_ERR_OK)
    {
        *pNetThreadObj = self;
    }
    else
    {
        delete self;
    }
    return err;
}

NetThreadObj::~NetThreadObj()
{
    if (m_isShutdownEvent != NULL)
    {
        CloseHandle(m_isShutdownEvent);
    }

    if (m_startShutdownEvent != NULL)
    {
        CloseHandle(m_startShutdownEvent);
    }

    if (m_netEvents.size() >= 1)
    {
        // Close the interrupt event
        CloseHandle(m_netEvents[0]);
    }
}

void NetThreadObj::run()
{
    m_threadId = boost::this_thread::get_id();

    // Run until the start shutdown event is signaled
    while (WaitForSingleObject(m_startShutdownEvent, 0) != WAIT_OBJECT_0)
    {
        DWORD wsaWaitErr = WSAWaitForMultipleEvents(
            static_cast<DWORD>(m_netEvents.size()), &m_netEvents[0], FALSE,
            WSA_INFINITE, FALSE);

        if (WSA_WAIT_EVENT_0 <= wsaWaitErr &&
            wsaWaitErr <= WSA_WAIT_EVENT_0 + (m_netEvents.size() - 1))
        {
            // One of our events has been signaled
            size_t netEventIdx = wsaWaitErr - WSA_WAIT_EVENT_0;

            if (netEventIdx == 0)
            {
                // The interrupt event was signaled
                ResetEvent(m_netEvents[0]);

                if (WaitForSingleObject(m_startShutdownEvent, 0) !=
                    WAIT_OBJECT_0)
                {
                    // We have net object additions and/or removals to handle
                    boost::lock_guard<boost::mutex> lock(m_mutex);

                    while (!m_changeRequests.empty())
                    {
                        ChangeRequest& changeRequest =
                            m_changeRequests.front();
                        if (changeRequest.add)
                        {
                            m_netObjs.push_back(changeRequest.netObj);
                            m_netEvents.push_back(
                                changeRequest.netObj->netEvent());
                        }
                        else
                        {
                            // Removal
                            std::vector<NetObjSPtr>::iterator netObjIt =
                                std::find(m_netObjs.begin(), m_netObjs.end(),
                                    changeRequest.netObj);

                            if (netObjIt != m_netObjs.end())
                            {
                                size_t netObjIdx = netObjIt -
                                    m_netObjs.begin();
                                m_netObjs.erase(netObjIt);

                                std::vector<WSAEVENT>::iterator netEventIt =
                                    m_netEvents.begin() + netObjIdx;
                                m_netEvents.erase(netEventIt);
                            }
                        }
                        m_changeRequests.pop();
                    }
                }
            }
            else
            {
                // One of the network events was signaled
                // Avoid socket starvation (caused by an event at the start of
                // the array being frequently signaled) by also examining all
                // the events after the one that was signaled
                for (size_t idx = netEventIdx; idx < m_netEvents.size() &&
                    WaitForSingleObject(m_startShutdownEvent, 0) !=
                        WAIT_OBJECT_0;
                    ++idx)
                {
                    if (WSAWaitForMultipleEvents(1, &m_netEvents[idx], FALSE,
                        0, FALSE) == WSA_WAIT_EVENT_0)
                    {
                        m_netObjs[idx]->onNetEvent();
                    }
                }
            }
        }
    }

    SetEvent(m_isShutdownEvent);
}

void NetThreadObj::addNetObj(const NetObjSPtr& netObj)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    // This method cannot add or remove elements from the m_netObjs or
    // m_netEvents containers directly as they may be in use by the thread
    // associated with this object

    ChangeRequest changeRequest;
    changeRequest.add = true;
    changeRequest.netObj = netObj;
    m_changeRequests.push(changeRequest);

    // Signal the interrupt event
    SetEvent(m_netEvents[0]);
}

void NetThreadObj::removeNetObj(const NetObjSPtr& netObj)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    // This method cannot add or remove elements from the m_netObjs or
    // m_netEvents containers directly as they may be in use by the thread
    // associated with this object

    ChangeRequest changeRequest;
    changeRequest.add = false;
    changeRequest.netObj = netObj;
    m_changeRequests.push(changeRequest);

    // Signal the interrupt event
    SetEvent(m_netEvents[0]);
}

void NetThreadObj::startShutdown()
{
    SetEvent(m_startShutdownEvent);

    // Signal the interrupt event
    SetEvent(m_netEvents[0]);
}

bool NetThreadObj::waitForShutdown(DWORD milliseconds)
{
    if (boost::this_thread::get_id() == m_threadId)
    {
        // The thread that called this method is the thread associated with
        // this object. Therefore there's no way the thread associated with
        // this object can have shutdown yet or will shutdown in the time-out
        // interval
        return false;
    }

    return (WaitForSingleObject(m_isShutdownEvent, milliseconds) ==
        WAIT_OBJECT_0);
}

NetThreadObj::NetThreadObj() : m_startShutdownEvent(NULL),
m_isShutdownEvent(NULL)
{
}

int NetThreadObj::construct()
{
    int err = CL_ERR_OK;

    m_netEvents.reserve(WSA_MAXIMUM_WAIT_EVENTS);
    m_netObjs.reserve(WSA_MAXIMUM_WAIT_EVENTS);

    HANDLE interruptEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        // Manual-reset, unsignaled
    if (interruptEvent != NULL)
    {
        // The first element in the net events array will be for interrupts
        m_netEvents.push_back(interruptEvent);
        NetObjSPtr nullNetObj;
        m_netObjs.push_back(nullNetObj);

        m_startShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            // Manual-reset, unsignaled
        if (m_startShutdownEvent != NULL)
        {
            m_isShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                // Manual-reset, unsignaled
            if (m_isShutdownEvent == NULL)
            {
                err = GetLastError();
            }
        }
        else
        {
            err = GetLastError();
        }
    }
    else
    {
        err = GetLastError();
    }

    return err;
}
