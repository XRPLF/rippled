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

#include <ripple/app/consensus/RCLCxPeerPos.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

// Used to construct received proposals
RCLCxPeerPos::RCLCxPeerPos(
    PublicKey const& publicKey,
    Slice const& signature,
    uint256 const& suppression,
    Proposal&& proposal)
    : data_{std::make_shared<Data>(
          publicKey,
          signature,
          suppression,
          std::move(proposal))}
{
}

bool
RCLCxPeerPos::checkSign() const
{
    return verify(
        publicKey(),
        makeSlice(proposalSigningData(proposal())),
        signature(),
        false /* mustBeFullyCanonical */);
}


Json::Value
RCLCxPeerPos::getJson() const
{
    auto ret = proposal().getJson();

    if (publicKey().size())
        ret[jss::peer_id] = toBase58(TokenType::NodePublic, publicKey());

    return ret;
}

uint256
proposalUniqueId(
    uint256 const& proposeHash,
    uint256 const& previousLedger,
    std::uint32_t proposeSeq,
    NetClock::time_point closeTime,
    Slice const& publicKey,
    Slice const& signature)
{
    Serializer s(512);
    s.add256(proposeHash);
    s.add256(previousLedger);
    s.add32(proposeSeq);
    s.add32(closeTime.time_since_epoch().count());
    s.addVL(publicKey);
    s.addVL(signature);

    return s.getSHA512Half();
}

Blob
proposalSigningData(RCLCxPeerPos::Proposal const& proposal)
{
    Serializer s;
    s.add32(HashPrefix::proposal);
    s.add32(std::uint32_t(proposal.proposeSeq()));
    s.add32(proposal.closeTime().time_since_epoch().count());
    s.add256(proposal.prevLedger());
    s.add256(proposal.position());
    return s.getData();
}

RCLCxPeerPos::Data::Data(
    PublicKey const& publicKey,
    Slice const& signature,
    uint256 const& suppress,
    Proposal&& proposal)
    : publicKey_{publicKey}
    , signature_{signature}
    , suppression_{suppress}
    , proposal_{std::move(proposal)}
{
}

}  // ripple
