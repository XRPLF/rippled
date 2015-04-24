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
#include <boost/optional.hpp>

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

enum LedgerEntryAction
{
    taaNONE,
    taaCACHED,  // Unmodified.
    taaMODIFY,  // Modifed, must have previously been taaCACHED.
    taaDELETE,  // Delete, must have previously been taaDELETE or taaMODIFY.
    taaCREATE,  // Newly created.
};

enum FreezeHandling
{
    fhIGNORE_FREEZE,
    fhZERO_IF_FROZEN
};

class LedgerEntrySetEntry
    : public CountedObject <LedgerEntrySetEntry>
{
public:
    static char const* getCountedObjectName () { return "LedgerEntrySetEntry"; }

    SLE::pointer        mEntry;
    LedgerEntryAction   mAction;
    int                 mSeq;

    LedgerEntrySetEntry (SLE::ref e, LedgerEntryAction a, int s)
        : mEntry (e)
        , mAction (a)
        , mSeq (s)
    {
    }
};

/** An LES is a LedgerEntrySet.

    It's a view into a ledger used while a transaction is processing.
    The transaction manipulates the LES rather than the ledger
    (because it's cheaper, can be checkpointed, and so on). When the
    transaction finishes, the LES is committed into the ledger to make
    the modifications. The transaction metadata is built from the LES too.
*/
class LedgerEntrySet
    : public CountedObject <LedgerEntrySet>
{
public:
    static char const* getCountedObjectName () { return "LedgerEntrySet"; }

    LedgerEntrySet (
        Ledger::ref ledger, TransactionEngineParams tep, bool immutable = false)
        : mLedger (ledger), mParams (tep), mSeq (0), mImmutable (immutable)
    {
    }

    LedgerEntrySet () : mParams (tapNONE), mSeq (0), mImmutable (false)
    {
    }

    // Make a duplicate of this set.
    LedgerEntrySet duplicate () const;

    // Swap the contents of two sets
    void swapWith (LedgerEntrySet&);

    void invalidate ()
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

    void init (Ledger::ref ledger, uint256 const& transactionID,
               std::uint32_t ledgerID, TransactionEngineParams params);

    void clear ();

    Ledger::pointer& getLedger ()
    {
        return mLedger;
    }

    // basic entry functions
    SLE::pointer getEntry (uint256 const& index, LedgerEntryAction&);

    void entryCache (SLE::ref);     // Add this entry to the cache
    void entryCreate (SLE::ref);    // This entry will be created
    void entryDelete (SLE::ref);    // This entry will be deleted
    void entryModify (SLE::ref);    // This entry will be modified

    // higher-level ledger functions
    SLE::pointer entryCreate (LedgerEntryType letType, uint256 const& uIndex);
    SLE::pointer entryCache (LedgerEntryType letType, uint256 const& uIndex);

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
    bool dirNext (uint256 const& uRootIndex, SLE::pointer& sleNode,
        unsigned int & uDirEntry, uint256 & uEntryIndex);
    bool dirIsEmpty (uint256 const& uDirIndex);
    TER dirCount (uint256 const& uDirIndex, std::uint32_t & uCount);

    uint256 getNextLedgerIndex (uint256 const& uHash);
    uint256 getNextLedgerIndex (uint256 const& uHash, uint256 const& uEnd);

    /** @{ */
    void incrementOwnerCount (SLE::ref sleAccount);
    void incrementOwnerCount (Account const& owner);
    /** @} */

    /** @{ */
    void decrementOwnerCount (SLE::ref sleAccount);
    void decrementOwnerCount (Account const& owner);
    /** @} */

    // Offer functions.
    TER offerDelete (SLE::pointer);
    TER offerDelete (uint256 const& offerIndex)
    {
        return offerDelete( entryCache (ltOFFER, offerIndex));
    }

    // Balance functions.
    bool isFrozen (
        Account const& account,
        Currency const& currency,
        Account const& issuer);

    bool isGlobalFrozen (Account const& issuer);

    void enableDeferredCredits (bool enable=true);
    bool areCreditsDeferred () const;

    TER rippleCredit (
        Account const& uSenderID, Account const& uReceiverID,
        const STAmount & saAmount, bool bCheckIssuer = true);

    STAmount accountHolds (
        Account const& account, Currency const& currency,
        Account const& issuer, FreezeHandling freezeHandling);
    STAmount accountFunds (
        Account const& account, const STAmount & saDefault, FreezeHandling freezeHandling);
    TER accountSend (
        Account const& uSenderID, Account const& uReceiverID,
        const STAmount & saAmount);

    TER trustCreate (
        const bool      bSrcHigh,
        Account const&  uSrcAccountID,
        Account const&  uDstAccountID,
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
        SLE::ref sleRippleState, Account const& uLowAccountID,
        Account const& uHighAccountID);

    Json::Value getJson (int) const;
    void calcRawMeta (Serializer&, TER result, std::uint32_t index);

    // iterator functions
    typedef std::map<uint256, LedgerEntrySetEntry>::iterator iterator;
    typedef std::map<uint256, LedgerEntrySetEntry>::const_iterator const_iterator;

    bool empty () const
    {
        return mEntries.empty ();
    }
    const_iterator cbegin () const
    {
        return mEntries.cbegin ();
    }
    const_iterator cend () const
    {
        return mEntries.cend ();
    }
    const_iterator begin () const
    {
        return mEntries.cbegin ();
    }
    const_iterator end () const
    {
        return mEntries.cend ();
    }
    iterator begin ()
    {
        return mEntries.begin ();
    }
    iterator end ()
    {
        return mEntries.end ();
    }

    void setDeliveredAmount (STAmount const& amt)
    {
        mSet.setDeliveredAmount (amt);
    }

    TER issue_iou (Account const& account,
        STAmount const& amount, Issue const& issue);

    TER redeem_iou (Account const& account,
        STAmount const& amount, Issue const& issue);

    TER transfer_xrp (Account const& from, Account const& to, STAmount const& amount);

private:
    Ledger::pointer mLedger;
    std::map<uint256, LedgerEntrySetEntry>  mEntries; // cannot be unordered!
    // Defers credits made to accounts until later
    boost::optional<DeferredCredits> mDeferredCredits;

    typedef hash_map<uint256, SLE::pointer> NodeToLedgerEntry;

    TransactionMetaSet mSet;
    TransactionEngineParams mParams;
    int mSeq;
    bool mImmutable;

    LedgerEntrySet (
        Ledger::ref ledger, const std::map<uint256, LedgerEntrySetEntry>& e,
        const TransactionMetaSet & s, int m, boost::optional<DeferredCredits> const& ft) :
        mLedger (ledger), mEntries (e), mDeferredCredits (ft), mSet (s), mParams (tapNONE),
        mSeq (m), mImmutable (false)
    {}

    SLE::pointer getForMod (
        uint256 const& node, Ledger::ref ledger,
        NodeToLedgerEntry& newMods);

    bool threadTx (
        const RippleAddress & threadTo, Ledger::ref ledger,
        NodeToLedgerEntry& newMods);

    bool threadTx (
        SLE::ref threadTo, Ledger::ref ledger, NodeToLedgerEntry& newMods);

    bool threadOwners (
        SLE::ref node, Ledger::ref ledger, NodeToLedgerEntry& newMods);

    TER rippleSend (
        Account const& uSenderID, Account const& uReceiverID,
        const STAmount & saAmount, STAmount & saActual);

    STAmount rippleHolds (
        Account const& account, Currency const& currency,
        Account const& issuer, FreezeHandling zeroIfFrozen);

    STAmount rippleTransferFee (
        Account const& from, Account const& to,
        Account const& issuer, STAmount const& saAmount);

    bool checkState (SLE::pointer state, bool bSenderHigh,
        Account const& sender, STAmount const& before, STAmount const& after);

    STAmount adjustedBalance (Account const& main,
                              Account const& other,
                              STAmount const& amount) const;

    void cacheCredit (Account const& sender,
                      Account const& receiver,
                      STAmount const& amount);
};

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
rippleTransferRate (LedgerEntrySet& ledger, Account const& issuer);

std::uint32_t
rippleTransferRate (LedgerEntrySet& ledger, Account const& uSenderID,
    Account const& uReceiverID, Account const& issuer);

} // ripple

#endif
