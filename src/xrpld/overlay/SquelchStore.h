//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_SQUELCH_H_INCLUDED
#define RIPPLE_OVERLAY_SQUELCH_H_INCLUDED

#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>

namespace ripple {

namespace reduce_relay {

/** Tracks which validators were squelched.
 */
class SquelchStore
{
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;
    using time_point = typename clock_type::time_point;

public:
    explicit SquelchStore(beast::Journal journal, clock_type& clock)
        : journal_(journal), clock_(clock)
    {
    }
    virtual ~SquelchStore() = default;

    /** Handle a squelch request.
     * @param validator The validator's public key.
     * @param squelch Indicate if the validator should be squelched.
     * @param duration Duration in seconds for how long to squelch the
     * validator. Duration can be zero when squelch is false.
     */
    void
    handleSquelch(
        PublicKey const& validator,
        bool squelch,
        std::chrono::seconds duration);

    /** Check if a given validator is squelched. If the validator is no longer
     * squelched, clear the squelch entry.
     * @param validator Validator's public key
     * @return true if squelch exists and it is not expired. False otherwise.
     */
    bool
    expireAndIsSquelched(PublicKey const& validator);

private:
    /** Squelch validation/proposal relaying for the validator
     * @param validator The validator's public key
     * @param squelchDuration Squelch duration in seconds
     * @return false if invalid squelch duration
     */
    void
    add(PublicKey const& validator,
        std::chrono::seconds const& squelchDuration);

    /** Remove the squelch for a validator
     * @param validator The validator's public key
     */
    void
    remove(PublicKey const& validator);

    // holds a squelch expiration time_point for each validator
    hash_map<PublicKey, time_point> squelched_;
    beast::Journal const journal_;
    clock_type& clock_;
};

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLED_SQUELCH_H
