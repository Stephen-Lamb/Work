/**
 * @file
 * Declares the NetThreadObj class.
 */

#pragma once

#include <winsock2.h>
#include <windows.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/utility.hpp>
#include <queue>
#include <vector>
#include "netobj.h"

/**
 * An object that is responsible for notifying network objects registered with
 * it when a network event has occured.
 */
class NetThreadObj : private boost::noncopyable
{
public:
    /**
     * The maximum number of network objects that can be added to each thread
     * object.
     */
    static const DWORD NET_OBJ_MAX_COUNT = WSA_MAXIMUM_WAIT_EVENTS - 1;

    /**
     * Creates a network thread object.
     *
     * @param pNetThreadObj if the method was successful this will be set to
     * point to the network thread object that was created.
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    static int create(NetThreadObj** pNetThreadObj);

    ~NetThreadObj();

    /** The method the thread associated with this object will run. */
    void run();

    /**
     * Adds the given network object to this thread object.
     *
     * @param netObj the network object to add to this thread object.
     */
    void addNetObj(const NetObjSPtr& netObj);

    /**
     * Removes the given network object from this thread object.
     *
     * @param netObj the network object to remove from this thread object.
     */
    void removeNetObj(const NetObjSPtr& netObj);

    /**
     * Signals the thread associated with this object that is should start
     * shutdown.
     */
    void startShutdown();

    /**
     * Has the thread associated with this object completed shutdown?
     *
     * @return Whether or not the thread associated with this object has
     * completed shutdown.
     */
    inline bool isShutdown() const;

    /**
     * Waits until the thread associated with this object has completed
     * shutdown or the time-out interval elapses.
     *
     * @param milliseconds the time-out interval in milliseconds.
     * @return Whether or not the shutdown completed in the time-out interval.
     */
    bool waitForShutdown(DWORD milliseconds);

private:
    /**
     * Represents a network object change request, either an addition or a
     * removal.
     */
    struct ChangeRequest
    {
        /**
         * Are we adding a network object to this thread object or removing
         * one?
         */
        bool add;

        /** The network object to add or remove. */
        NetObjSPtr netObj;
    };

    /** The first stage of construction. */
    NetThreadObj();

    /**
     * The second stage of construction.
     *
     * @return CL_ERR_OK if the method was successful, any other value
     * otherwise.
     */
    int construct();

    /** Synchronizes access to this object. */
    boost::mutex m_mutex;

    /** The ID of the thread associated with this object. */
    boost::thread::id m_threadId;

    /**
     * The network events the thread associated with this object will wait on.
     */
    std::vector<WSAEVENT> m_netEvents;

    /** The network objects added to this thread object. */
    std::vector<NetObjSPtr> m_netObjs;

    /** A queue of change requests for this thread object. */
    std::queue<ChangeRequest> m_changeRequests;

    /**
     * An event that will be signaled when the thread associated with this
     * object has started shutdown.
     */
    HANDLE m_startShutdownEvent;

    /**
     * An event that will be signaled when the thread associated with this
     * object has completed shutdown.
     */
    HANDLE m_isShutdownEvent;
};

#include "netthreadobj.inl"

/** A shared pointer to a network thread object. */
typedef boost::shared_ptr<NetThreadObj> NetThreadObjSPtr;
