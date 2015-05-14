/**
 * @file
 * Defines the NetThreadPool class.
 */

#include "netthreadpool.h"
#include <boost/thread/locks.hpp>
#include <boost/thread/thread.hpp>
#include <utility>
#include "inc/comlib/comlib.h"

int NetThreadPool::addNetObj(const NetObjSPtr& netObj)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    cleanupShuttingDownThreads();
    int err = CL_ERR_OK;

    // Find a thread that has not had the maximum number of network objects
    // added to it and add the network object.
    bool foundThread = false;
    for (size_t i = m_threads.size(); !foundThread && i > 0; --i)
    {
        ThreadObjCountPair& aThreadObjCountPair = m_threads[i - 1];
        if (*aThreadObjCountPair.count < NetThreadObj::NET_OBJ_MAX_COUNT)
        {
            aThreadObjCountPair.threadObj->addNetObj(netObj);
            ++*aThreadObjCountPair.count;
            std::pair<std::map<NetObjSPtr, ThreadObjCountPair>::iterator, bool>
                insertResult = m_objToThreadMap.insert(
                    std::make_pair(netObj, aThreadObjCountPair));
            assert(insertResult.second);
                // Socket obj cannot already have been added to a thread
            foundThread = true;
        }
    }

    // If all threads are full then create a new thread and add the network
    // object to that.
    if (!foundThread)
    {
        NetThreadObj* threadObj = 0;
        err = NetThreadObj::create(&threadObj);
        if (err == CL_ERR_OK)
        {
            ThreadObjCountPair aThreadObjCountPair;
            aThreadObjCountPair.threadObj.reset(threadObj);
            threadObj->addNetObj(netObj);
            aThreadObjCountPair.count.reset(new DWORD(1));
            m_threads.push_back(aThreadObjCountPair);
            std::pair<std::map<NetObjSPtr, ThreadObjCountPair>::iterator, bool>
                insertResult = m_objToThreadMap.insert(
                    std::make_pair(netObj, aThreadObjCountPair));
            assert(insertResult.second);
                // Socket obj cannot already have been added to a thread
            boost::thread aThread(&NetThreadObj::run,
                aThreadObjCountPair.threadObj);
        }
    }

    return err;
}

void NetThreadPool::removeNetObj(const NetObjSPtr& netObj)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    cleanupShuttingDownThreads();

    // Find the thread the network object was added to and remove the network
    // object.
    std::map<NetObjSPtr, ThreadObjCountPair>::iterator objToThreadMapIt =
        m_objToThreadMap.find(netObj);
    if (objToThreadMapIt != m_objToThreadMap.end())
    {
        objToThreadMapIt->second.threadObj->removeNetObj(netObj);
        --*objToThreadMapIt->second.count;

        // If the network object was the only one added to the thread then
        // remove the thread
        if (*objToThreadMapIt->second.count <= 0)
        {
            std::vector<ThreadObjCountPair>::iterator threadsIt =
                m_threads.begin();
            for (; threadsIt != m_threads.end(); ++threadsIt)
            {
                if (threadsIt->threadObj == objToThreadMapIt->second.threadObj)
                {
                    // Found the thread in the m_threads container
                    break;
                }
            }

            if (threadsIt != m_threads.end())
            {
                threadsIt->threadObj->startShutdown();
                m_shuttingDownThreads.push_back(threadsIt->threadObj);
                m_threads.erase(threadsIt);
            }
        }

        m_objToThreadMap.erase(objToThreadMapIt);
    }
}

bool NetThreadPool::waitForShutdown(DWORD milliseconds)
{
    boost::lock_guard<boost::mutex> lock(m_mutex);

    bool allThreadsShutdown = true;
    DWORD timeOutInterval = milliseconds;
    DWORD startTickCount = GetTickCount();

    for (size_t i = m_shuttingDownThreads.size(); i > 0; --i)
    {
        std::vector<NetThreadObjSPtr>::iterator it =
            m_shuttingDownThreads.begin() + (i - 1);
        if ((*it)->waitForShutdown(timeOutInterval))
        {
            m_shuttingDownThreads.erase(it);
        }
        else
        {
            allThreadsShutdown = false;
        }

        DWORD elapsedInterval = GetTickCount() - startTickCount;
        if (elapsedInterval < timeOutInterval)
        {
            timeOutInterval -= elapsedInterval;
        }
        else
        {
            timeOutInterval = 0;
        }
    }

    return allThreadsShutdown;
}

void NetThreadPool::cleanupShuttingDownThreads()
{
    for (size_t i = m_shuttingDownThreads.size(); i > 0; --i)
    {
        std::vector<NetThreadObjSPtr>::iterator it =
            m_shuttingDownThreads.begin() + (i - 1);
        if ((*it)->isShutdown())
        {
            m_shuttingDownThreads.erase(it);
        }
    }
}
