/**
 * @file
 * Declares the NetObj class.
 */

#pragma once

#include <winsock2.h>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>

/** An object that will receive notification of network events. */
class NetObj : private boost::noncopyable
{
public:
    virtual ~NetObj() {}

    /**
     * Returns the network event object associated with this object.
     *
     * @return The network event object associated with this object.
     */
    virtual WSAEVENT netEvent() const = 0;

    /** This will be called when a network event has occured. */
    virtual void onNetEvent() = 0;
};

/** A shared pointer to a network object. */
typedef boost::shared_ptr<NetObj> NetObjSPtr;
