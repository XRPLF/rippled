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

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/ledger/ViewAPIBasics.h>
#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <beast/utility/noexcept.h>
#include <boost/optional.hpp>
#include <utility>

namespace ripple {

// VFALCO Does this belong here?  Is it correctly named?

enum TransactionEngineParams
{
    tapNONE             = 0x00,

    // Signature already checked
    tapNO_CHECK_SIGN    = 0x01,

    // Transaction is running against an open ledger
    // true = failures are not forwarded, check transaction fee
    // false = debit ledger for consumed funds
    tapOPEN_LEDGER      = 0x10,

    // This is not the transaction's last pass
    // Transaction can be retried, soft failures allowed
    tapRETRY            = 0x20,

    // Transaction came from a privileged source
    tapADMIN            = 0x400,
};

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

    using list_type = std::map<uint256, Item>;

    BasicView* parent_;
    list_type items_;
    TransactionMetaSet mSet;
    TransactionEngineParams mParams = tapNONE;

public:
    MetaView& operator= (MetaView const&) = delete;

    MetaView (Ledger::ref ledger,
        uint256 const& transactionID,
            std::uint32_t ledgerID,
                TransactionEngineParams params);

    MetaView (BasicView& parent, 
        bool openLedger);

    // DEPRECATED
    MetaView (Ledger::ref ledger,
        TransactionEngineParams tep);

    MetaView (MetaView& parent);

    //--------------------------------------------------------------------------
    //
    // View
    //
    //--------------------------------------------------------------------------

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

    BasicView const*
    parent() const override
    {
        return parent_;
    }
 
    //---------------------------------------------

    std::shared_ptr<SLE>
    peek (Keylet const& k) override;

    void
    erase (std::shared_ptr<SLE> const& sle) override;

    void
    insert (std::shared_ptr<SLE> const& sle) override;

    void
    update (std::shared_ptr<SLE> const& sle) override;

    bool
    openLedger() const override;

    //--------------------------------------------------------------------------

    /** Apply changes to the parent View */
    void
    apply();

    // For diagnostics
    Json::Value getJson (int) const;

    void calcRawMeta (Serializer&, TER result, std::uint32_t index);

    void setDeliveredAmount (STAmount const& amt)
    {
        mSet.setDeliveredAmount (amt);
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
