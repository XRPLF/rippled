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

namespace ripple {
namespace PeerFinder {

/** Maintains a set of IP addresses used for getting into the network. */
class Manager
    : public Stoppable
    , public PropertyStream::Source
{
protected:
    explicit Manager (Stoppable& parent);

public:
    /** Create a new Manager. */
    static Manager* New (Stoppable& parent,
        Callback& callback, Journal journal);

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

    /** Add a set of strings for peers that should always be connected.
        This is useful for maintaining a private cluster of peers.
        If a string is not parseable as a numeric IP address it will
        be passed to a DNS resolver to perform a lookup.
    */
    virtual void addFixedPeers (
        std::vector <std::string> const& strings) = 0;

    /** Add a set of strings as fallback IPAddress sources.
        @param name A label used for diagnostics.
    */
    virtual void addFallbackStrings (std::string const& name,
        std::vector <std::string> const& strings) = 0;

    /** Add a URL as a fallback location to obtain IPAddress sources.
        @param name A label used for diagnostics.
    */
    virtual void addFallbackURL (std::string const& name,
        std::string const& url) = 0;

	/** Called when a new peer connection is established. 
		Internally, we add the peer to our tracking table, validate that
		we can connect to it, and begin advertising it to others after
		we are sure that its connection is stable.
	*/
	virtual void onPeerConnected (PeerID const& id,
                                  IPAddress const& address,
                                  bool inbound) = 0;

	/** Called when an existing peer connection drops for whatever reason.
		Internally, we mark the peer as no longer connected, calculate 
		stability metrics, and consider whether we should try to reconnect
		to it or drop it from our list.
	*/
	virtual void onPeerDisconnected (PeerID const& id) = 0;

    /** Called when mtENDPOINTS is received. */
    virtual void onPeerEndpoints (PeerID const& id,
        std::vector <Endpoint> const& endpoints) = 0;

    /** Called when a legacy IP/port address is received (from mtPEER). */
    virtual void onPeerLegacyEndpoint (IPAddress const& ep) = 0;
};

}
}

#endif
