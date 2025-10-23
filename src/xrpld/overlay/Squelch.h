#ifndef XRPL_OVERLAY_SQUELCH_H_INCLUDED
#define XRPL_OVERLAY_SQUELCH_H_INCLUDED

#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <algorithm>
#include <chrono>
#include <functional>

namespace ripple {

namespace reduce_relay {

/** Maintains squelching of relaying messages from validators */
template <typename clock_type>
class Squelch
{
    using time_point = typename clock_type::time_point;

public:
    explicit Squelch(beast::Journal journal) : journal_(journal)
    {
    }
    virtual ~Squelch() = default;

    /** Squelch validation/proposal relaying for the validator
     * @param validator The validator's public key
     * @param squelchDuration Squelch duration in seconds
     * @return false if invalid squelch duration
     */
    bool
    addSquelch(
        PublicKey const& validator,
        std::chrono::seconds const& squelchDuration);

    /** Remove the squelch
     * @param validator The validator's public key
     */
    void
    removeSquelch(PublicKey const& validator);

    /** Remove expired squelch
     * @param validator Validator's public key
     * @return true if removed or doesn't exist, false if still active
     */
    bool
    expireSquelch(PublicKey const& validator);

private:
    /** Maintains the list of squelched relaying to downstream peers.
     * Expiration time is included in the TMSquelch message. */
    hash_map<PublicKey, time_point> squelched_;
    beast::Journal const journal_;
};

template <typename clock_type>
bool
Squelch<clock_type>::addSquelch(
    PublicKey const& validator,
    std::chrono::seconds const& squelchDuration)
{
    if (squelchDuration >= MIN_UNSQUELCH_EXPIRE &&
        squelchDuration <= MAX_UNSQUELCH_EXPIRE_PEERS)
    {
        squelched_[validator] = clock_type::now() + squelchDuration;
        return true;
    }

    JLOG(journal_.error()) << "squelch: invalid squelch duration "
                           << squelchDuration.count();

    // unsquelch if invalid duration
    removeSquelch(validator);

    return false;
}

template <typename clock_type>
void
Squelch<clock_type>::removeSquelch(PublicKey const& validator)
{
    squelched_.erase(validator);
}

template <typename clock_type>
bool
Squelch<clock_type>::expireSquelch(PublicKey const& validator)
{
    auto now = clock_type::now();

    auto const& it = squelched_.find(validator);
    if (it == squelched_.end())
        return true;
    else if (it->second > now)
        return false;

    // squelch expired
    squelched_.erase(it);

    return true;
}

}  // namespace reduce_relay

}  // namespace ripple

#endif  // XRPL_SQUELCH_H
