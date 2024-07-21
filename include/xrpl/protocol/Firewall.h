//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO THIS  SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED
#define RIPPLE_PROTOCOL_FIREWALL_H_INCLUDED

#include <xrpl/basics/XRPAmount.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>

namespace ripple {

/**
 * @brief Serializes firewall authorization data into a message.
 *
 * This function serializes the given account and preauthorize account IDs
 * into the provided Serializer object. It adds a shardInfo hash prefix,
 * followed by the account and preauthorize account IDs.
 *
 * @param msg The Serializer object to serialize the data into.
 * @param account The account ID to be serialized.
 * @param preauthorize The preauthorize account ID to be serialized.
 */
inline void
serializeFirewallAuthorization(Serializer& msg, AccountID const& account, AccountID const& preauthorize)
{
    msg.add32(HashPrefix::shardInfo);
    msg.addBitString(account);
    msg.addBitString(preauthorize);
}

/**
 * @brief Serializes firewall authorization data into a message.
 *
 * This function serializes the given account ID and amount into the provided
 * Serializer object. It adds a shardInfo hash prefix, followed by the account
 * ID and the amount's mantissa.
 *
 * @param msg The Serializer object to serialize the data into.
 * @param account The account ID to be serialized.
 * @param amount The amount to be serialized.
 */
inline void
serializeFirewallAuthorization(Serializer& msg, AccountID const& account, STAmount const& amount)
{
    msg.add32(HashPrefix::shardInfo);
    msg.addBitString(account);
    msg.add64(amount.mantissa());
}

/**
 * @brief Serializes firewall authorization data into a message.
 *
 * This function serializes the given account ID and public key into the
 * provided Serializer object. It adds a shardInfo hash prefix, followed by
 * the account ID and the raw bytes of the public key.
 *
 * @param msg The Serializer object to serialize the data into.
 * @param account The account ID to be serialized.
 * @param pk The public key to be serialized.
 */
inline void
serializeFirewallAuthorization(Serializer& msg, AccountID const& account, PublicKey const& pk)
{
    msg.add32(HashPrefix::shardInfo);
    msg.addBitString(account);
    msg.addRaw(pk.slice());
}

}  // namespace ripple

#endif