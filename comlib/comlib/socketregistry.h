/**
 * @file
 * Declares the SktObjTypeRegistry template class and the SrvSocketRegistry and
 * SocketRegistry typedefs.
 */

#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/utility.hpp>
#include <map>
#include <utility>
#include "inc/comlib/comlib.h"
#include "socketobj.h"
#include "srvsocketobj.h"

/**
 * A registry for socket objects, parameterized by the type of socket handle
 * and type of socket object.
 */
template<class SktHndType, class SktObjType>
class SktObjTypeRegistry : private boost::noncopyable
{
public:
    /**
     * Converts a socket object to a handle.
     *
     * @param sktObj a socket object.
     * @return The handle for the socket object.
     */
    inline static SktHndType toHandle(SktObjType* sktObj);

    /**
     * Adds the socket object specified by the given handle and object to this
     * registry.
     *
     * @param sktHnd the handle of the socket object to add to this registry.
     * @param sktObj the socket object to add to this registry.
     */
    void addSocketObj(const SktHndType& sktHnd,
        const boost::shared_ptr<SktObjType>& sktObj);

    /**
     * Finds the socket object for the given handle in this registry.
     *
     * @param sktHnd the handle of the socket object to find in this registry.
     * @return The socket object for the given handle. If the socket object
     * could not be found then a default instance of type
     * boost::shared_ptr<SktObjType> will be returned.
     */
    boost::shared_ptr<SktObjType> findSocketObj(SktHndType sktHnd);

    /**
     * Removes and returns the socket object specified by the given handle from
     * this registry.
     *
     * @param sktHnd the handle of the socket object to remove and return from
     * this registry.
     * @return The socket object for the given handle. If the socket object
     * could not be found then a default instance of type
     * boost::shared_ptr<SktObjType> will be returned.
     */
    boost::shared_ptr<SktObjType> removeSocketObj(SktHndType sktHnd);

    /**
     * Removes and returns the front socket object from this registry.
     *
     * @return The front socket object from this registry. If the registry was
     * empty then a default instance of type boost::shared_ptr<SktObjType> will
     * be returned.
     */
    boost::shared_ptr<SktObjType> removeFrontSocketObj();

private:
    /** Synchronizes access to this object. */
    boost::mutex m_mutex;

    /** A mapping from socket handle to socket object. */
    std::map<SktHndType, boost::shared_ptr<SktObjType> > m_hndToSktObjMap;
};

#include "socketregistry.inl"

/** A registry for server socket objects. */
typedef SktObjTypeRegistry<CLSrvSocket, SrvSocketObj> SrvSocketRegistry;

/** A registry for socket objects. */
typedef SktObjTypeRegistry<CLSocket, SocketObj> SocketRegistry;
