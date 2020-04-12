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

#include <ripple/basics/random.h>
#include <ripple/overlay/SquelchCommon.h>
#include <ripple/protocol/PublicKey.h>

#include <chrono>
#include <functional>

namespace ripple {

namespace squelch {

/** Maintains squelching of relaying messages from validators */
template <typename clock_type>
class Squelch
{
    using time_point = typename clock_type::time_point;

public:
    Squelch() = default;
    virtual ~Squelch() = default;

    /** Squelch/Unsquelch relaying for the validator
     * @param validator The validator's public key
     * @param squelch Squelch/unsquelch flag
     * @param squelchDuration Squelch duration time if squelch is true
     */
    void
    squelch(PublicKey const& validator, bool squelch, uint64_t squelchDuration);

    /** Are the messages to this validator squelched
     * @param validator Validator's public key
     * @return true if squelched
     */
    bool
    isSquelched(PublicKey const& validator);

    /** Get random squelch duration between MIN_UNSQUELCH_EXPIRE and
     * MAX_UNSQUELCH_EXPIRE */
    static seconds
    getSquelchDuration();

private:
    /** Maintains the list of squelched relaying to downstream peers.
     * Expiration time is included in the TMSquelch message. */
    hash_map<PublicKey, time_point> squelched_;
};

template <typename clock_type>
void
Squelch<clock_type>::squelch(
    PublicKey const& validator,
    bool squelch,
    uint64_t squelchDuration)
{
    if (squelch)
    {
        squelched_[validator] = [squelchDuration]() {
            seconds duration = seconds(squelchDuration);
            return clock_type::now() +
                ((duration >= MIN_UNSQUELCH_EXPIRE &&
                  duration <= MAX_UNSQUELCH_EXPIRE)
                     ? duration
                     : getSquelchDuration());
        }();
    }
    else
        squelched_.erase(validator);
}

template <typename clock_type>
bool
Squelch<clock_type>::isSquelched(PublicKey const& validator)
{
    auto now = clock_type::now();

    auto const& it = squelched_.find(validator);
    if (it == squelched_.end())
        return false;
    else if (it->second > now)
        return true;

    squelched_.erase(it);

    return false;
}

template <typename clock_type>
seconds
Squelch<clock_type>::getSquelchDuration()
{
    auto d = seconds(ripple::rand_int(
        MIN_UNSQUELCH_EXPIRE.count(), MAX_UNSQUELCH_EXPIRE.count()));
    return d;
}

}  // namespace squelch

}  // namespace ripple

#endif  // RIPPLED_SQUELCH_H
