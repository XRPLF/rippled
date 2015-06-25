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

#ifndef RIPPLE_LEDGER_METAVIEW_H_INCLUDED
#define RIPPLE_LEDGER_METAVIEW_H_INCLUDED

#include <ripple/app/tx/TransactionMeta.h>
#include <ripple/ledger/View.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <beast/utility/noexcept.h>
#include <boost/optional.hpp>
#include <list>
#include <tuple>
#include <utility>

namespace ripple {

/** A MetaView can produce tx metadata and is attached to a parent.

    It's a view into a ledger used while a transaction is processing.
    The transaction manipulates the MetaView rather than the ledger
    (because it's cheaper, can be checkpointed, and so on). When the
    transaction finishes, the MetaView is committed into the ledger to make
    the modifications. The transaction metadata is built from the LES too.
*/
class MetaView : public View
{
private:
    enum Action
    {
        taaCACHED,  // Unmodified.
        taaMODIFY,  // Modifed, must have previously been taaCACHED.
        taaDELETE,  // Delete, must have previously been taaDELETE or taaMODIFY.
        taaCREATE,  // Newly created.
    };

    using Item = std::pair<Action,
        std::shared_ptr<SLE>>;

    using Mods = hash_map<uint256,
        std::shared_ptr<SLE>>;

    // The SLEs and Serializers in here are
    // shared between copy-constructed instances
    using item_list = std::map<uint256, Item>;
    using tx_list = hardened_hash_map<
        uint256, std::pair<std::shared_ptr<
            Serializer const>, std::shared_ptr<
                Serializer const>>>;

    // Note that this class needs to be
    // somewhat light-weight copy constructible.
    BasicView const* parent_;
    ViewFlags flags_ = tapNONE;
    LedgerIndex seq_;
    std::uint32_t time_; // should be Clock::time_point
    tx_list txs_;
    item_list items_;
    TransactionMetaSet meta_;
    std::uint32_t destroyedCoins_ = 0;

public:
    MetaView() = delete;
    MetaView& operator= (MetaView const&) = delete;

    /** Create a shallow copy of a MetaView.

        The SLEs and Serializers in the created copy
        are shared with the other view.

        It is only safe to use the BasicView modification
        functions. Using View modification functions will
        break invariants.

        The seq, time, and flags are copied from `other`.

        @note This is used to apply new transactions to
              the open MetaView.
    */
    // VFALCO Refactor to disallow at compile time,
    //        breaking invariants on a shallow copy.
    //
    MetaView (MetaView const& other) = default;

    /** Create a MetaView with a BasicView as its parent.

        Effects:
            The sequence number and time are set
            from the passed parameters.

        It is only safe to use the BasicView modification
        functions. Using View modification functions will
        break invariants.

        @note This is for converting a closed ledger
              into an open ledger.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    // VFALCO Refactor to disallow at compile time,
    //        breaking invariants on a shallow copy.
    //
    MetaView (BasicView const* parent,
        LedgerIndex seq, std::uint32_t time,
            ViewFlags flags);

    /** Create a MetaView with a BasicView as its parent.

        Effects:
            The sequence number and time are inherited
            from the parent.

            The MetaSet is prepared to produce metadata
            for a transaction with the specified key.

        @note This is for applying a particular transaction
              and computing its metadata, or for applying
              a transaction without extracting metadata. For
              example, to calculate changes in a sandbox
              and then throw the sandbox away.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    MetaView (BasicView const* parent,
        ViewFlags flags,
            boost::optional<uint256
                > const& key = boost::none);

    /** Create a MetaView with a View as its parent.

        Effects:
            The sequence number, time, and flags
            are inherited from the parent.

        @note This is for stacking view for the purpose of
              performing calculations or applying to an
              underlying MetaView associated with a particular
              transation.

        @note A pointer is used to prevent confusion
              with copy construction.
    */
    MetaView (View const* parent);

    //--------------------------------------------------------------------------
    //
    // BasicView
    //
    //--------------------------------------------------------------------------

    Fees const&
    fees() const override
    {
        return parent_->fees();
    }

    LedgerIndex
    seq() const override
    {
        return seq_;
    }

    std::uint32_t
    time() const override
    {
        return time_;
    }

    bool
    exists (Keylet const& k) const override;

    boost::optional<uint256>
    succ (uint256 const& key, boost::optional<
        uint256> last = boost::none) const override;

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;

    bool
    unchecked_erase(
        uint256 const& key) override;

    void
    unchecked_insert (
        std::shared_ptr<SLE>&& sle) override;

    void
    unchecked_replace(
        std::shared_ptr<SLE>&& sle) override;

    void
    destroyCoins (std::uint64_t feeDrops) override;

    bool
    txExists (uint256 const& key) const override;

    bool
    txInsert (uint256 const& key,
        std::shared_ptr<Serializer const
            > const& txn, std::shared_ptr<
                Serializer const> const& metaData) override;

    std::vector<uint256>
    txList() const override;

    //--------------------------------------------------------------------------
    //
    // view
    //
    //--------------------------------------------------------------------------

    ViewFlags
    flags() const override
    {
        return flags_;
    }

    std::shared_ptr<SLE>
    peek (Keylet const& k) override;

    void
    erase (std::shared_ptr<SLE> const& sle) override;

    void
    insert (std::shared_ptr<SLE> const& sle) override;

    void
    update (std::shared_ptr<SLE> const& sle) override;

    //--------------------------------------------------------------------------

    /** Apply changes to the parent View.

        `to` must contain contents identical to the parent
        view passed upon construction, else undefined
        behavior will result.

        After a call to apply(), the only valid operation that
        may be performed on this is a call to the destructor.
    */
    void
    apply (BasicView& to);

    // For diagnostics
    Json::Value getJson (int) const;

    void calcRawMeta (Serializer&, TER result, std::uint32_t index);

    void setDeliveredAmount (STAmount const& amt)
    {
        meta_.setDeliveredAmount (amt);
    }

private:
    std::shared_ptr<SLE>
    getForMod (uint256 const& key,
        Mods& mods);

    bool
    threadTx (AccountID const& to,
        Mods& mods);

    bool
    threadTx (std::shared_ptr<SLE> const& to,
    Mods& mods);

    bool
    threadOwners (std::shared_ptr<
        SLE const> const& sle, Mods& mods);
};

} // ripple

#endif
