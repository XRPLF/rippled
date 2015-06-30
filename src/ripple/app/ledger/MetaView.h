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

#include <ripple/ledger/View.h>
#include <ripple/app/ledger/TxMeta.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STTx.h>
#include <beast/utility/noexcept.h>
#include <boost/optional.hpp>
#include <functional>
#include <list>
#include <tuple>
#include <utility>

namespace ripple {

/** Shallow-copy construction tag.

    When a MetaView is shallow-copied, the SLEs and
    Serializers are shared between instances. It is
    only safe to use BasicView interfaces, using
    View members results in undefined behavior.
*/
struct shallow_copy_t {};
extern shallow_copy_t const shallow_copy;

/** Open ledger construction tag.

    Views constructed with this tag will have the
    rules of open ledgers applied during transaction
    processing.
*/
struct open_ledger_t {};
extern open_ledger_t const open_ledger;

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
    BasicView const& base_;
    ViewFlags flags_ = tapNONE;
    ViewInfo info_;
    tx_list txs_;
    item_list items_;
    std::uint32_t destroyedCoins_ = 0;
    boost::optional<STAmount> deliverAmount_;
    std::shared_ptr<void const> hold_;

public:
    MetaView() = delete;
    MetaView(MetaView const&) = delete;
    MetaView& operator= (MetaView const&) = delete;

    /** Create a shallow copy of a MetaView.

        Effects:
            Duplicates the information in the
            passed MetaView.

            The SLEs and Serializers in the copy
            are shared with the other view.
            The copy has the same Info values.

        It is only safe to use the BasicView modification
        functions. Using View modification functions will
        break invariants.
    */
    // VFALCO Refactor to disallow at compile time,
    //        breaking invariants on a shallow copy.
    //
    MetaView (shallow_copy_t,
        MetaView const& other);

    /** Create a MetaView representing an open ledger.

        Preconditions:
            
            `prev` cannot represent an open ledger.

        Effects:

            The sequence number is set to the
            sequence number of parent plus one.

            The parentCloseTime is set to the
            closeTime of parent.

            If `hold` is not nullptr, retains
            ownership of a copy of `hold` until
            the MetaView is destroyed.

        It is only safe to use the BasicView modification
        functions. Using View modification functions will
        break invariants.

        @param parent A view representing the previous
                      ledger that this open ledger follows.
    */
    MetaView (open_ledger_t,
        BasicView const& parent,
            std::shared_ptr<
                void const> hold = nullptr);

    /** Create a nested MetaView.

        Effects:

            The ViewInfo is copied from the base.
    */
    MetaView (BasicView const& base,
        ViewFlags flags, std::shared_ptr<
            void const> hold = nullptr);

    //--------------------------------------------------------------------------
    //
    // BasicView
    //
    //--------------------------------------------------------------------------

    ViewInfo const&
    info() const
    {
        return info_;
    }

    Fees const&
    fees() const override
    {
        return base_.fees();
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

    std::size_t
    txCount() const override;

    bool
    txExists (uint256 const& key) const override;

    void
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

    /** Apply changes to the base View.

        `to` must contain contents identical to the
        parent view passed upon construction, else
        undefined behavior will result.

        After a call to apply(), the only valid operation
        on the object is a call to the destructor.
    */
    void
    apply (BasicView& to,
        beast::Journal j = {});

    /** Apply the results of a transaction to the base view.
    
        `to` must contain contents identical to the
        parent view passed upon construction, else
        undefined behavior will result.

        After a call to apply(), the only valid operation
        on the object is a call to the destructor.

        Effects:

            The transaction is inserted to the tx map.

            If the base view represents a closed ledger,
            the transaction metadata is computed and
            inserted with the transaction.

        The metadata is computed by recording the
        differences between the base view and the
        modifications in this view.

        @param view The view to apply to.
        @param tx The transaction that was processed.
        @param ter The result of applying the transaction.
        @param j Where to log.
    */
    void
    apply (BasicView& to, STTx const& tx,
        TER result, beast::Journal j);

    // For diagnostics
    Json::Value getJson (int) const;

    void setDeliveredAmount (STAmount const& amt)
    {
        deliverAmount_ = amt;
    }

private:
    static
    bool
    threadTx (TxMeta& meta,
        std::shared_ptr<SLE> const& to,
            Mods& mods);

    std::shared_ptr<SLE>
    getForMod (uint256 const& key,
        Mods& mods, beast::Journal j);

    bool
    threadTx (TxMeta& meta,
        AccountID const& to, Mods& mods,
            beast::Journal j);

    bool
    threadOwners (TxMeta& meta, std::shared_ptr<
        SLE const> const& sle, Mods& mods,
            beast::Journal j);
};

} // ripple

#endif
