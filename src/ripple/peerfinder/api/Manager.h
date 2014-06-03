//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PEERFINDER_MANAGER_H_INCLUDED
#define RIPPLE_PEERFINDER_MANAGER_H_INCLUDED

#include <ripple/peerfinder/api/Config.h>
#include <ripple/peerfinder/api/Slot.h>
#include <ripple/peerfinder/api/Types.h>

#include <ripple/sitefiles/api/Manager.h>

#include <beast/module/core/files/File.h>

namespace ripple {
namespace PeerFinder {

/** Maintains a set of IP addresses used for getting into the network. */
class Manager
    : public beast::Stoppable
    , public beast::PropertyStream::Source
{
protected:
    explicit Manager (Stoppable& parent);

public:
    /** Create a new Manager. */
    static Manager* New (
        Stoppable& parent,
        SiteFiles::Manager& siteFiles,
        beast::File const& pathToDbFileOrDirectory,
        Callback& callback,
        clock_type& clock,
        beast::Journal journal);

    /** Destroy the object.
        Any pending source fetch operations are aborted.
        There may be some listener calls made before the
        destructor returns.
    */
    virtual ~Manager () { }

    /** Set the configuration for the manager.
        The new settings will be applied asynchronously.
        Thread safety:
            Can be called from any threads at any time.
    */
    virtual void setConfig (Config const& config) = 0;

    /** Add a peer that should always be connected.
        This is useful for maintaining a private cluster of peers.
        The string is the name as specified in the configuration
        file, along with the set of corresponding IP addresses.
    */
    virtual void addFixedPeer (std::string const& name,
        std::vector <beast::IP::Endpoint> const& addresses) = 0;

    /** Add a set of strings as fallback IP::Endpoint sources.
        @param name A label used for diagnostics.
    */
    virtual void addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings) = 0;

    /** Add a URL as a fallback location to obtain IP::Endpoint sources.
        @param name A label used for diagnostics.
    */
    /* VFALCO NOTE Unimplemented
    virtual void addFallbackURL (std::string const& name,
        std::string const& url) = 0;
    */

    //--------------------------------------------------------------------------

    /** Create a new inbound slot with the specified remote endpoint.
        If nullptr is returned, then the slot could not be assigned.
        Usually this is because of a detected self-connection.
    */
    virtual Slot::ptr new_inbound_slot (
        beast::IP::Endpoint const& local_endpoint,
            beast::IP::Endpoint const& remote_endpoint) = 0;

    /** Create a new outbound slot with the specified remote endpoint.
        If nullptr is returned, then the slot could not be assigned.
        Usually this is because of a duplicate connection.
    */
    virtual Slot::ptr new_outbound_slot (
        beast::IP::Endpoint const& remote_endpoint) = 0;

    /** Called when an outbound connection attempt succeeds.
        The local endpoint must be valid. If the caller receives an error
        when retrieving the local endpoint from the socket, it should
        proceed as if the connection attempt failed by calling on_closed
        instead of on_connected.
    */
    virtual void on_connected (Slot::ptr const& slot,
        beast::IP::Endpoint const& local_endpoint) = 0;

    /** Called when a handshake is completed. */
    virtual void on_handshake (Slot::ptr const& slot,
        RipplePublicKey const& key, bool cluster) = 0;

    /** Called when mtENDPOINTS is received. */
    virtual void on_endpoints (Slot::ptr const& slot,
        Endpoints const& endpoints) = 0;

    /** Called when legacy IP/port addresses are received. */
    virtual void on_legacy_endpoints (IPAddresses const& addresses) = 0;

    /** Called when the slot is closed.
        This always happens when the socket is closed, unless the socket
        was canceled.
    */
    virtual void on_closed (Slot::ptr const& slot) = 0;

    /** Called when the slot is closed via canceling operations.
        This is instead of on_closed.
    */
    virtual void on_cancel (Slot::ptr const& slot) = 0;

};

}
}

#endif
