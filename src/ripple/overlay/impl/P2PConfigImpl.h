//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/base_uint.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/overlay/impl/P2PConfig.h>

#ifndef RIPPLE_OVERLAY_P2PCONFIGIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_P2PCONFIGIMPL_H_INCLUDED

namespace ripple {

/** Get P2P required configuration properties from the Application */
class P2PConfigImpl : public P2PConfig
{
    Application& app_;

public:
    P2PConfigImpl(Application& app) : app_(app)
    {
    }

    P2PConfigImpl(P2PConfigImpl const&) = delete;
    P2PConfigImpl&
    operator=(P2PConfigImpl const&) = delete;

    Config const&
    config() const override
    {
        return app_.config();
    }

    Logs&
    logs() const override
    {
        return app_.logs();
    }

    bool
    isValidator() const override
    {
        return !app_.getValidationPublicKey().empty();
    }

    std::pair<PublicKey, SecretKey> const&
    identity() const override
    {
        return app_.nodeIdentity();
    }

    std::optional<std::string>
    clusterMember(PublicKey const& key) const override
    {
        return app_.cluster().member(key);
    }

    bool
    reservedPeer(PublicKey const& key) const override
    {
        return app_.peerReservations().contains(key);
    }

    std::optional<std::pair<uint256, uint256>>
    clHashes() const override
    {
        if (auto cl = app_.getLedgerMaster().getClosedLedger())
            return std::make_pair(cl->info().hash, cl->info().parentHash);
        return std::nullopt;
    }

    NetClock::time_point
    now() const override
    {
        return app_.timeKeeper().now();
    }
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_APPCONFIGREQUESTORIMPL_H_INCLUDED
