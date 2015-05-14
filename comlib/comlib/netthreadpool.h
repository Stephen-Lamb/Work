/**
 * @file
 * Declares the NetThreadPool class.
 */

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/utility.hpp>
#include <map>
#include <vector>
#include "netobj.h"
#include "netthreadobj.h"

/** A pool of network threads. */
class NetThreadPool : private boost::noncopyable
{
public:
    /**
     * Adds the given network object to a thread in this pool.
     *
     * @param netObj the network object to add to this thread pool.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int addNetObj(const NetObjSPtr& netObj);

    /**
     * Removes the given network object from the thread it was added to in this
     * pool. If the network object was the only one added to the thread then
     * the thread will start to shutdown.
     *
     * @param netObj the network object to remove from this thread pool.
     */
    void removeNetObj(const NetObjSPtr& netObj);

    /**
     * Waits until all shutting down threads in this pool have completed
     * shutdown or the time-out interval elapses.
     *
     * @param milliseconds the time-out interval in milliseconds.
     * @return Whether or not all shutting down threads in this pool completed
     * shutdown in the time-out interval.
     */
    bool waitForShutdown(DWORD milliseconds);

private:
    /**
     * A network thread object and the number of network objects added to it.
     */
    struct ThreadObjCountPair
    {
        /** A network thread object. */
        NetThreadObjSPtr threadObj;

        /** The number of network objects added to the thread object. */
        boost::shared_ptr<DWORD> count;
    };

    /** Discards any threads in this pool that have completed shutdown. */
    void cleanupShuttingDownThreads();

    /** Synchronizes access to this object. */
    boost::mutex m_mutex;

    /** The currently running network threads. */
    std::vector<ThreadObjCountPair> m_threads;

    /** A mapping from network object to the thread object it was added to. */
    std::map<NetObjSPtr, ThreadObjCountPair> m_objToThreadMap;

    /** The threads in this pool that are in the process of shutting down. */
    std::vector<NetThreadObjSPtr> m_shuttingDownThreads;
};
