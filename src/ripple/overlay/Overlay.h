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

#ifndef RIPPLE_OVERLAY_OVERLAY_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAY_H_INCLUDED

#include <ripple/overlay/Peer.h>

// VFALCO TODO Remove this include dependency it shouldn't be needed
#include <ripple/peerfinder/api/Slot.h>

#include <beast/threads/Stoppable.h>
#include <beast/utility/PropertyStream.h>

#include <beast/cxx14/type_traits.h> // <type_traits>

namespace ripple {

/** Manages the set of connected peers. */
class Overlay
    : public beast::Stoppable
    , public beast::PropertyStream::Source
{
protected:
    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    Overlay (Stoppable& parent)
        : Stoppable ("Overlay", parent)
        , beast::PropertyStream::Source ("peers")
    {

    }

public:
    typedef std::vector <Peer::ptr> PeerSequence;

    virtual
    ~Overlay () = default;

    /** Establish a peer connection to the specified endpoint.
        The call returns immediately, the connection attempt is
        performed asynchronously.
    */
    virtual
    void
    connect (beast::IP::Endpoint const& address) = 0;

    /** Returns the number of active peers.
        Active peers are only those peers that have completed the
        handshake and are using the peer protocol.
    */
    virtual
    std::size_t
    size () = 0;

    /** Return diagnostics on the status of all peers.
        @deprecated This is superceded by PropertyStream
    */
    virtual
    Json::Value
    json () = 0;

    /** Returns a sequence representing the current list of peers.
        The snapshot is made at the time of the call.
    */
    virtual
    PeerSequence
    getActivePeers () = 0;

    /** Returns the peer with the matching short id, or null. */
    virtual
    Peer::ptr
    findPeerByShortID (Peer::ShortId const& id) = 0;

    /** Visit every active peer and return a value
        The functor must:
        - Be callable as:
            void operator()(Peer::ptr const& peer);
         - Must have the following typedef:
            typedef void return_type;
         - Be callable as:
            Function::return_type operator()() const;

        @param f the functor to call with every peer
        @returns `f()`

        @note The functor is passed by value!
    */
    template<typename Function>
    std::enable_if_t <
        ! std::is_void <typename Function::return_type>::value,
        typename Function::return_type
    >
    foreach(Function f)
    {
        PeerSequence peers (getActivePeers());
        for(PeerSequence::const_iterator i = peers.begin(); i != peers.end(); ++i)
            f (*i);
        return f();
    }

    /** Visit every active peer
        The visitor functor must:
         - Be callable as:
            void operator()(Peer::ptr const& peer);
         - Must have the following typedef:
            typedef void return_type;

        @param f the functor to call with every peer
    */
    template <class Function>
    std::enable_if_t <
        std::is_void <typename Function::return_type>::value,
        typename Function::return_type
    >
    foreach(Function f)
    {
        PeerSequence peers (getActivePeers());

        for(PeerSequence::const_iterator i = peers.begin(); i != peers.end(); ++i)
            f (*i);
    }
};

}

#endif
