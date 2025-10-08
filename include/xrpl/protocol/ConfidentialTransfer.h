//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED
#define RIPPLE_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/detail/secp256k1.h>

#include <secp256k1.h>

namespace ripple {

// breaks a 66-byte encrypted amount into two 33-byte components
// then parses each 33-byte component into 64-byte secp256k1_pubkey format
bool
makeEcPair(Slice const& buffer, secp256k1_pubkey& out1, secp256k1_pubkey& out2);

// serialize two secp256k1_pubkey components back into compressed 66-byte form
bool
serializeEcPair(
    secp256k1_pubkey const& in1,
    secp256k1_pubkey const& in2,
    Buffer& buffer);

TER
homomorphicAdd(Slice const& a, Slice const& b, Buffer& out);

TER
proveEquality(
    Slice const& proof,
    Slice const& encAmt,  // encrypted amount
    Slice const& pubkey,
    uint64_t const amount,
    uint256 const& txHash,  // Transaction context data
    std::uint32_t const spendVersion);

}  // namespace ripple

#endif
