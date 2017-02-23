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

#include <BeastConfig.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/Feature.h>
#include <cstring>

namespace ripple {

static
uint256
feature (char const* s, std::size_t n)
{
    sha512_half_hasher h;
    h(s, n);
    return static_cast<uint256>(h);
}

uint256
feature (std::string const& name)
{
    return feature(name.c_str(), name.size());
}

uint256
feature (const char* name)
{
    return feature(name, std::strlen(name));
}

uint256 const featureMultiSign = feature("MultiSign");
uint256 const featureTickets = feature("Tickets");
uint256 const featureTrustSetAuth = feature("TrustSetAuth");
uint256 const featureFeeEscalation = feature("FeeEscalation");
uint256 const featureOwnerPaysFee = feature("OwnerPaysFee");
uint256 const featureCompareFlowV1V2 = feature("CompareFlowV1V2");
uint256 const featureSHAMapV2 = feature("SHAMapV2");
uint256 const featurePayChan = feature("PayChan");
uint256 const featureFlow = feature("Flow");
uint256 const featureCryptoConditions = feature("CryptoConditions");
uint256 const featureTickSize = feature("TickSize");
uint256 const fix1368 = feature("fix1368");
uint256 const featureEscrow = feature("Escrow");
uint256 const featureCryptoConditionsSuite = feature("CryptoConditionsSuite");
uint256 const featureToStrandV2 = feature("ToStrandV2");
uint256 const featureEnforceInvariants = feature("EnforceInvariants");

} // ripple
