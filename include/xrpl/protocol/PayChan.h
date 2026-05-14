/** @file
 *  Canonical serialization for payment channel claim authorizations.
 *
 *  Defines the single function that all three call sites — channel
 *  authorization (RPC), channel verification (RPC), and on-ledger
 *  claim validation (transaction engine) — must use to build the
 *  signed payload. Centralizing this here ensures that a signature
 *  produced off-ledger is always accepted on-ledger.
 */

#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/** Serialize the signing payload for a payment channel claim authorization.
 *
 *  Writes exactly three fields into @p msg in a protocol-defined order:
 *  the `HashPrefix::paymentChannelClaim` domain-separation tag (4 bytes),
 *  the 256-bit channel keylet @p key, and the authorized cumulative amount
 *  @p amt as a 64-bit drop count. The resulting byte sequence is what the
 *  channel sender signs and what the recipient or ledger verifies.
 *
 *  This function is the single source of truth for the signed payload layout.
 *  It is called identically by `channel_authorize` (RPC), `channel_verify`
 *  (RPC), and `PaymentChannelClaim` preflight (transaction engine). Any drift
 *  between those sites would cause off-ledger signatures to fail on-ledger
 *  validation.
 *
 *  @param msg  Serializer to append the payload fields into. The caller is
 *      responsible for constructing the `Serializer` and, after this call,
 *      passing `msg.slice()` to the sign or verify primitive.
 *  @param key  The 256-bit keylet of the payment channel ledger object. Binds
 *      the authorization to exactly one channel so it cannot be replayed
 *      against a different channel.
 *  @param amt  The authorized cumulative ceiling in drops. The on-ledger claim
 *      validator rejects any claim whose running total exceeds this value.
 *
 *  @note The `HashPrefix::paymentChannelClaim` tag (`'C','L','M',0x00`) is
 *      protocol-immutable. Changing it would invalidate all existing payment
 *      channel authorizations.
 *  @see HashPrefix::paymentChannelClaim
 */
inline void
serializePayChanAuthorization(Serializer& msg, uint256 const& key, XRPAmount const& amt)
{
    msg.add32(HashPrefix::PaymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

}  // namespace xrpl
