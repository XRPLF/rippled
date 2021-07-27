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

#ifndef RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED
#define RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/basics/chrono.h>

#include <optional>
#include <string>
#include <utility>

namespace ripple {

class Config;
class Logs;
class PublicKey;
class SecretKey;

/** P2P required configuration properties.
 */
class P2PConfig
{
public:
    P2PConfig() = default;
    virtual ~P2PConfig() = default;
    virtual Config const&
    config() const = 0;
    virtual Logs&
    logs() const = 0;
    virtual bool
    isValidator() const = 0;
    virtual std::pair<PublicKey, SecretKey> const&
    identity() const = 0;
    virtual std::optional<std::string>
    clusterMember(PublicKey const& key) const = 0;
    virtual bool
    reservedPeer(PublicKey const& key) const = 0;
    virtual std::optional<std::pair<uint256, uint256>>
    clHashes() const = 0;
    virtual NetClock::time_point
    now() const = 0;
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_P2PCONFIG_H_INCLUDED
