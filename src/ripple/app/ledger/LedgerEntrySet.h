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

#ifndef RIPPLE_APP_LEDGER_LEDGERENTRYSET_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERENTRYSET_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/DeferredCredits.h>
#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <beast/utility/noexcept.h>
#include <boost/optional.hpp>
#include <utility>

namespace ripple {

// VFALCO Does this belong here? Is it correctly named?

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

enum FreezeHandling
{
    fhIGNORE_FREEZE,
    fhZERO_IF_FROZEN
};

/** An LES is a LedgerEntrySet.

    It's a view into a ledger used while a transaction is processing.
    The transaction manipulates the LES rather than the ledger
    (because it's cheaper, can be checkpointed, and so on). When the
    transaction finishes, the LES is committed into the ledger to make
    the modifications. The transaction metadata is built from the LES too.
*/
class LedgerEntrySet
{
private:
    using NodeToLedgerEntry =
        hash_map<uint256, SLE::pointer>;

    enum Action
    {
        taaNONE,
        taaCACHED,  // Unmodified.
        taaMODIFY,  // Modifed, must have previously been taaCACHED.
        taaDELETE,  // Delete, must have previously been taaDELETE or taaMODIFY.
        taaCREATE,  // Newly created.
    };

    class Item
    {
    public:
        int mSeq;
        Action mAction;
        std::shared_ptr<SLE> mEntry;

        Item (SLE::ref e, Action a, int s)
            : mSeq (s)
            , mAction (a)
            , mEntry (e)
        {
        }
    };

    Ledger::pointer mLedger;
    // Implementation requires an ordered container
    std::map<uint256, Item> mEntries;
    boost::optional<DeferredCredits> mDeferredCredits;
    TransactionMetaSet mSet;
    TransactionEngineParams mParams = tapNONE;
    int mSeq = 0;
    bool mImmutable = false;

public:
    LedgerEntrySet& operator= (LedgerEntrySet const&) = delete;

    /** Construct a copy.
        Effects:
            The copy is identical except that
            the sequence number is one higher.
    */
    LedgerEntrySet (LedgerEntrySet const&);

    LedgerEntrySet (Ledger::ref ledger,
        uint256 const& transactionID,
            std::uint32_t ledgerID,
                TransactionEngineParams params);

    LedgerEntrySet (Ledger::ref ledger,
        TransactionEngineParams tep,
            bool immutable = false);

    /** Apply changes to the backing ledger. */
    void
    apply();

    // Swap the contents of two sets
    void swapWith (LedgerEntrySet&);

    // VFALCO Only called from RippleCalc.cpp
    void deprecatedInvalidate()
    {
        mLedger.reset ();
        mDeferredCredits.reset ();
    }

    bool isValid () const
    {
        return mLedger != nullptr;
    }

    int getSeq () const
    {
        return mSeq;
    }

    void bumpSeq ()
    {
        ++mSeq;
    }

    Ledger::pointer& getLedger ()
    {
        return mLedger;
    }

    void entryCache (SLE::ref);     // Add this entry to the cache
    void entryCreate (SLE::ref);    // This entry will be created
    void entryDelete (SLE::ref);    // This entry will be deleted
    void entryModify (SLE::ref);    // This entry will be modified

    // higher-level ledger functions
    SLE::pointer entryCache (LedgerEntryType letType, uint256 const& key);

    std::shared_ptr<SLE const>
    entryCacheI (LedgerEntryType letType, uint256 const& uIndex);

    // Directory functions.
    TER dirAdd (
        std::uint64_t&                      uNodeDir,      // Node of entry.
        uint256 const&                      uRootIndex,
        uint256 const&                      uLedgerIndex,
        std::function<void (SLE::ref, bool)> fDescriber);

    TER dirDelete (
        const bool           bKeepRoot,
        const std::uint64_t& uNodeDir,      // Node item is mentioned in.
        uint256 const&       uRootIndex,
        uint256 const&       uLedgerIndex,  // Item being deleted
        const bool           bStable,
        const bool           bSoft);

    bool dirFirst (uint256 const& uRootIndex, SLE::pointer& sleNode,
        unsigned int & uDirEntry, uint256 & uEntryIndex);

    bool dirFirst (uint256 const& uRootIndex, std::shared_ptr<SLE const>& sleNode,
        unsigned int & uDirEntry, uint256 & uEntryIndex);
    
    bool dirNext (uint256 const& uRootIndex, SLE::pointer& sleNode,
        unsigned int & uDirEntry, uint256 & uEntryIndex);

    bool dirNext (uint256 const& uRootIndex, std::shared_ptr<SLE const>& sleNode,
        unsigned int & uDirEntry, uint256 & uEntryIndex);
    
    bool dirIsEmpty (uint256 const& uDirIndex);
    
    uint256 getNextLedgerIndex (uint256 const& uHash);
    uint256 getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd);

    // Offer functions.
    TER offerDelete (SLE::pointer);

    TER offerDelete (uint256 const& offerIndex)
    {
        return offerDelete( entryCache (ltOFFER, offerIndex));
    }

    // Balance functions.
    bool isFrozen (
        AccountID const& account,
        Currency const& currency,
        AccountID const& issuer);

    bool isGlobalFrozen (AccountID const& issuer);

    void enableDeferredCredits (bool enable=true);

    bool areCreditsDeferred () const;

    TER rippleCredit (
        AccountID const& uSenderID, AccountID const& uReceiverID,
        const STAmount & saAmount, bool bCheckIssuer = true);

    STAmount accountHolds (
        AccountID const& account, Currency const& currency,
        AccountID const& issuer, FreezeHandling freezeHandling);

    TER accountSend (
        AccountID const& uSenderID, AccountID const& uReceiverID,
        const STAmount & saAmount);

    TER trustCreate (
        const bool      bSrcHigh,
        AccountID const&  uSrcAccountID,
        AccountID const&  uDstAccountID,
        uint256 const&  uIndex,
        SLE::ref        sleAccount,
        const bool      bAuth,
        const bool      bNoRipple,
        const bool      bFreeze,
        STAmount const& saSrcBalance,
        STAmount const& saSrcLimit,
        const std::uint32_t uSrcQualityIn = 0,
        const std::uint32_t uSrcQualityOut = 0);

    TER trustDelete (
        SLE::ref sleRippleState, AccountID const& uLowAccountID,
        AccountID const& uHighAccountID);

    Json::Value getJson (int) const;

    void calcRawMeta (Serializer&, TER result, std::uint32_t index);

    void setDeliveredAmount (STAmount const& amt)
    {
        mSet.setDeliveredAmount (amt);
    }

    TER issue_iou (AccountID const& account,
        STAmount const& amount, Issue const& issue);

    TER redeem_iou (AccountID const& account,
        STAmount const& amount, Issue const& issue);

    TER transfer_xrp (AccountID const& from, AccountID const& to, STAmount const& amount);

private:
    SLE::pointer getEntry (uint256 const& index, Action&);

    SLE::pointer getForMod (
        uint256 const& node, Ledger::ref ledger,
        NodeToLedgerEntry& newMods);

    bool threadTx (
        const RippleAddress & threadTo, Ledger::ref ledger,
        NodeToLedgerEntry& newMods);

    bool threadTx (
        SLE::ref threadTo, Ledger::ref ledger, NodeToLedgerEntry& newMods);

    bool threadOwners (std::shared_ptr<SLE const> const& node,
        Ledger::ref ledger, NodeToLedgerEntry& newMods);

    TER rippleSend (
        AccountID const& uSenderID, AccountID const& uReceiverID,
        const STAmount & saAmount, STAmount & saActual);

    STAmount rippleHolds (
        AccountID const& account, Currency const& currency,
        AccountID const& issuer, FreezeHandling zeroIfFrozen);

    STAmount rippleTransferFee (
        AccountID const& from, AccountID const& to,
        AccountID const& issuer, STAmount const& saAmount);

    bool checkState (SLE::pointer state, bool bSenderHigh,
        AccountID const& sender, STAmount const& before, STAmount const& after);

    STAmount adjustedBalance (AccountID const& main,
                              AccountID const& other,
                              STAmount const& amount) const;

    void cacheCredit (AccountID const& sender,
                      AccountID const& receiver,
                      STAmount const& amount);
};

template <class... Args>
inline
void
reconstruct (LedgerEntrySet& v, Args&&... args) noexcept
{
    v.~LedgerEntrySet();
    new(&v) LedgerEntrySet(
        std::forward<Args>(args)...);
}

using LedgerView = LedgerEntrySet;

//------------------------------------------------------------------------------

class ScopedDeferCredits
{
private:
    LedgerEntrySet& les_;
    bool enabled_;
public:
    ScopedDeferCredits(LedgerEntrySet& l);
    ~ScopedDeferCredits ();
};

// NIKB FIXME: move these to the right place
std::uint32_t
rippleTransferRate (LedgerEntrySet& ledger, AccountID const& issuer);

std::uint32_t
rippleTransferRate (LedgerEntrySet& ledger, AccountID const& uSenderID,
    AccountID const& uReceiverID, AccountID const& issuer);

//------------------------------------------------------------------------------
//
// API
//
//------------------------------------------------------------------------------

/** Adjust the owner count up or down. */
void
adjustOwnerCount (LedgerEntrySet& view,
    std::shared_ptr<SLE> const& sle, int amount);

// Returns the funds available for account for a currency/issuer.
// Use when you need a default for rippling account's currency.
// XXX Should take into account quality?
// --> saDefault/currency/issuer
// <-- saFunds: Funds available. May be negative.
//
// If the issuer is the same as account, funds are unlimited, use result is
// saDefault.
STAmount
funds (LedgerEntrySet& view, AccountID const& id,
    STAmount const& saDefault,
        FreezeHandling freezeHandling);

} // ripple

#endif
