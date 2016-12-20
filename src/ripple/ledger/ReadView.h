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

#ifndef RIPPLE_LEDGER_READVIEW_H_INCLUDED
#define RIPPLE_LEDGER_READVIEW_H_INCLUDED

#include <ripple/ledger/detail/ReadViewFwdRange.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/XRPAmount.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace ripple {

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    std::uint64_t base = 0;         // Reference tx cost (drops)
    std::uint32_t units = 0;        // Reference fee units
    std::uint32_t reserve = 0;      // Reserve base (drops)
    std::uint32_t increment = 0;    // Reserve increment (drops)

    Fees() = default;
    Fees (Fees const&) = default;
    Fees& operator= (Fees const&) = default;

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    XRPAmount
    accountReserve (std::size_t ownerCount) const
    {
        return { reserve + ownerCount * increment };
    }
};

//------------------------------------------------------------------------------

/** Information about the notional ledger backing the view. */
struct LedgerInfo
{
    //
    // For all ledgers
    //

    LedgerIndex seq = 0;
    NetClock::time_point parentCloseTime = {};

    //
    // For closed ledgers
    //

    // Closed means "tx set already determined"
    uint256 hash = zero;
    uint256 txHash = zero;
    uint256 accountHash = zero;
    uint256 parentHash = zero;

    XRPAmount drops = zero;

    // If validated is false, it means "not yet validated."
    // Once validated is true, it will never be set false at a later time.
    // VFALCO TODO Make this not mutable
    bool mutable validated = false;
    bool accepted = false;

    // flags indicating how this ledger close took place
    int closeFlags = 0;

    // the resolution for this ledger close time (2-120 seconds)
    NetClock::duration closeTimeResolution = {};

    // For closed ledgers, the time the ledger
    // closed. For open ledgers, the time the ledger
    // will close if there's no transactions.
    //
    NetClock::time_point closeTime = {};
};

//------------------------------------------------------------------------------

class DigestAwareReadView;

/** Rules controlling protocol behavior. */
class Rules
{
private:
    class Impl;

    std::shared_ptr<Impl const> impl_;

public:
    Rules (Rules const&) = default;
    Rules& operator= (Rules const&) = default;

    /** Construct an empty rule set.

        These are the rules reflected by
        the genesis ledger.
    */
    Rules() = default;

    /** Construct rules from a ledger.

        The ledger contents are analyzed for rules
        and amendments and extracted to the object.
    */
    explicit
    Rules (DigestAwareReadView const& ledger);

    /** Returns `true` if a feature is enabled. */
    bool
    enabled (uint256 const& id,
        std::unordered_set<uint256,
            beast::uhash<>> const& presets) const;

    /** Returns `true` if these rules don't match the ledger. */
    bool
    changed (DigestAwareReadView const& ledger) const;

    /** Returns `true` if two rule sets are identical.

        @note This is for diagnostics. To determine if new
        rules should be constructed, call changed() first instead.
    */
    bool
    operator== (Rules const&) const;

    bool
    operator!= (Rules const& other) const
    {
        return ! (*this == other);
    }
};

//------------------------------------------------------------------------------

/** A view into a ledger.

    This interface provides read access to state
    and transaction items. There is no checkpointing
    or calculation of metadata.
*/
class ReadView
{
public:
    using tx_type =
        std::pair<std::shared_ptr<STTx const>,
            std::shared_ptr<STObject const>>;

    using key_type = uint256;

    using mapped_type =
        std::shared_ptr<SLE const>;

    struct sles_type : detail::ReadViewFwdRange<
        std::shared_ptr<SLE const>>
    {
        explicit sles_type (ReadView const& view);
        iterator begin() const;
        iterator const& end() const;
        iterator upper_bound(key_type const& key) const;
    };

    struct txs_type
        : detail::ReadViewFwdRange<tx_type>
    {
        explicit txs_type (ReadView const& view);
        bool empty() const;
        iterator begin() const;
        iterator const& end() const;
    };

    virtual ~ReadView() = default;

    ReadView& operator= (ReadView&& other) = delete;
    ReadView& operator= (ReadView const& other) = delete;

    ReadView ()
        : sles(*this)
        , txs(*this)
    {
    }

    ReadView (ReadView const& other)
        : sles(*this)
        , txs(*this)
    {
    }

    ReadView (ReadView&& other)
        : sles(*this)
        , txs(*this)
    {
    }

    /** Returns information about the ledger. */
    virtual
    LedgerInfo const&
    info() const = 0;

    /** Returns true if this reflects an open ledger. */
    virtual
    bool
    open() const = 0;

    /** Returns the close time of the previous ledger. */
    NetClock::time_point
    parentCloseTime() const
    {
        return info().parentCloseTime;
    }

    /** Returns the sequence number of the base ledger. */
    LedgerIndex
    seq() const
    {
        return info().seq;
    }

    /** Returns the fees for the base ledger. */
    virtual
    Fees const&
    fees() const = 0;

    /** Returns the tx processing rules. */
    virtual
    Rules const&
    rules() const = 0;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    virtual
    bool
    exists (Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, boost::none is returned.

        If `last` is engaged, returns boost::none when
        the key returned would be outside the open
        interval (key, last).
    */
    virtual
    boost::optional<key_type>
    succ (key_type const& key, boost::optional<
        key_type> const& last = boost::none) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    virtual
    std::shared_ptr<SLE const>
    read (Keylet const& k) const = 0;

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `balanceHook` adjusts balances
    // so newly acquired assets are not counted toward the balance.
    // This is required to support PaymentSandbox.
    virtual
    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const
    {
        return amount;
    }

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `ownerCountHook` adjusts the
    // ownerCount so it returns the max value of the ownerCount so far.
    // This is required to support PaymentSandbox.
    virtual
    std::uint32_t
    ownerCountHook (AccountID const& account,
        std::uint32_t count) const
    {
        return count;
    }

    // used by the implementation
    virtual
    std::unique_ptr<sles_type::iter_base>
    slesBegin() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<sles_type::iter_base>
    slesEnd() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<sles_type::iter_base>
    slesUpperBound(key_type const& key) const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<txs_type::iter_base>
    txsBegin() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<txs_type::iter_base>
    txsEnd() const = 0;

    /** Returns `true` if a tx exists in the tx map.

        A tx exists in the map if it is part of the
        base ledger, or if it is a newly inserted tx.
    */
    virtual
    bool
    txExists (key_type const& key) const = 0;

    /** Read a transaction from the tx map.

        If the view represents an open ledger,
        the metadata object will be empty.

        @return A pair of nullptr if the
                key is not found in the tx map.
    */
    virtual
    tx_type
    txRead (key_type const& key) const = 0;

    //
    // Memberspaces
    //

    /** Iterable range of ledger state items.

        @note Visiting each state entry in the ledger can
              become quite expensive as the ledger grows.
    */
    sles_type sles;

    // The range of transactions
    txs_type txs;
};

//------------------------------------------------------------------------------

/** ReadView that associates keys with digests. */
class DigestAwareReadView
    : public ReadView
{
public:
    using digest_type = uint256;

    DigestAwareReadView () = default;
    DigestAwareReadView (const DigestAwareReadView&) = default;

    /** Return the digest associated with the key.

        @return boost::none if the item does not exist.
    */
    virtual
    boost::optional<digest_type>
    digest (key_type const& key) const = 0;
};

//------------------------------------------------------------------------------

// ledger close flags
static
std::uint32_t const sLCF_NoConsensusTime = 0x01;

static
std::uint32_t const sLCF_SHAMapV2 = 0x02;

inline
bool getCloseAgree (LedgerInfo const& info)
{
    return (info.closeFlags & sLCF_NoConsensusTime) == 0;
}

inline
bool getSHAMapV2 (LedgerInfo const& info)
{
    return (info.closeFlags & sLCF_SHAMapV2) != 0;
}

void addRaw (LedgerInfo const&, Serializer&);

} // ripple

#include <ripple/ledger/detail/ReadViewFwdRange.ipp>

#endif
