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

#ifndef RIPPLE_RESOURCE_MANAGER_H_INCLUDED
#define RIPPLE_RESOURCE_MANAGER_H_INCLUDED

namespace ripple {
namespace Resource {

/** Tracks load and resource consumption. */
class Manager : public PropertyStream::Source
{
protected:
    Manager ();

public:
    static Manager* New (Journal journal);

    virtual ~Manager() { }

    /** Create a new endpoint keyed by inbound IP address. */
    virtual Consumer newInboundEndpoint (IPEndpoint const& address) = 0;

    /** Create a new endpoint keyed by outbound IP address and port. */
    virtual Consumer newOutboundEndpoint (IPEndpoint const& address) = 0;

    /** Create a new endpoint keyed by name. */
    virtual Consumer newAdminEndpoint (std::string const& name) = 0;

    /** Extract packaged consumer information for export. */
    virtual Gossip exportConsumers () = 0;

    /** Import packaged consumer information.
        @param origin An identifier that unique labels the origin.
    */
    virtual void importConsumers (std::string const& origin, Gossip const& gossip) = 0;
};

}
}

#endif
