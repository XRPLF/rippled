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

#ifndef RIPPLE_LEDGERENTRYSET_H
#define RIPPLE_LEDGERENTRYSET_H

namespace ripple {

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

    // set functions
    void setImmutable ()
    {
        mImmutable = true;
    }

    bool isImmutable () const
    {
        return mImmutable;
    }

    // Make a duplicate of this set.
    LedgerEntrySet duplicate () const;

    // Swap the contents of two sets
    void swapWith (LedgerEntrySet&);

    void invalidate ()
    {
        mLedger.reset ();
    }

    bool isValid () const
    {
        return mLedger != nullptr;
    }

    int getSeq () const
    {
        return mSeq;
    }

    TransactionEngineParams getParams () const
    {
        return mParams;
    }

    void bumpSeq ()
    {
        ++mSeq;
    }

    void init (Ledger::ref ledger, uint256 const & transactionID,
               std::uint32_t ledgerID, TransactionEngineParams params);

    void clear ();

    Ledger::pointer& getLedger ()
    {
        return mLedger;
    }

    // basic entry functions
    SLE::pointer getEntry (uint256 const & index, LedgerEntryAction&);
    LedgerEntryAction hasEntry (uint256 const & index) const;
    void entryCache (SLE::ref);     // Add this entry to the cache
    void entryCreate (SLE::ref);    // This entry will be created
    void entryDelete (SLE::ref);    // This entry will be deleted
    void entryModify (SLE::ref);    // This entry will be modified
    bool hasChanges ();             // True if LES has any changes

    // higher-level ledger functions
    SLE::pointer entryCreate (LedgerEntryType letType, uint256 const & uIndex);
    SLE::pointer entryCache (LedgerEntryType letType, uint256 const & uIndex);

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
    uint256 getNextLedgerIndex (uint256 const& uHash, uint256 const & uEnd);

    void ownerCountAdjust (Account const& uOwnerID, int iAmount,
        SLE::ref sleAccountRoot = SLE::pointer ());

    // Offer functions.
    TER offerDelete (SLE::pointer);
    TER offerDelete (uint256 const& offerIndex)
    {
        return offerDelete( entryCache (ltOFFER, offerIndex));
    }

    // Balance functions.
    std::uint32_t rippleTransferRate (Account const& issuer);
    std::uint32_t rippleTransferRate (
        Account const& uSenderID, Account const& uReceiverID,
        Account const& issuer);

    STAmount rippleOwed (
        Account const& uToAccountID, Account const& uFromAccountID,
        Currency const& currency);
    STAmount rippleLimit (
        Account const& uToAccountID, Account const& uFromAccountID,
        Currency const& currency);

    std::uint32_t rippleQualityIn (
        Account const& uToAccountID, Account const& uFromAccountID,
        Currency const& currency,
        SField::ref sfLow = sfLowQualityIn,
        SField::ref sfHigh = sfHighQualityIn);

    std::uint32_t rippleQualityOut (
        Account const& uToAccountID, Account const& uFromAccountID,
        Currency const& currency)
    {
        return rippleQualityIn (
            uToAccountID, uFromAccountID, currency,
            sfLowQualityOut, sfHighQualityOut);
    }

    STAmount rippleTransferFee (
        Account const& uSenderID, Account const& uReceiverID,
        Account const& issuer, const STAmount & saAmount);

    TER rippleCredit (
        Account const& uSenderID, Account const& uReceiverID,
        const STAmount & saAmount, bool bCheckIssuer = true);

    STAmount accountHolds (
        Account const& account, Currency const& currency,
        Account const& issuer);
    STAmount accountFunds (
        Account const& account, const STAmount & saDefault);
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
        const STAmount& saSrcBalance,
        const STAmount& saSrcLimit,
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

    bool isEmpty () const
    {
        return mEntries.empty ();
    }
    const_iterator begin () const
    {
        return mEntries.begin ();
    }
    const_iterator end () const
    {
        return mEntries.end ();
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

private:
    Ledger::pointer mLedger;
    std::map<uint256, LedgerEntrySetEntry>  mEntries; // cannot be unordered!

    typedef ripple::unordered_map<uint256, SLE::pointer> NodeToLedgerEntry;

    TransactionMetaSet mSet;
    TransactionEngineParams mParams;
    int mSeq;
    bool mImmutable;

    LedgerEntrySet (
        Ledger::ref ledger, const std::map<uint256, LedgerEntrySetEntry>& e,
        const TransactionMetaSet & s, int m) :
        mLedger (ledger), mEntries (e), mSet (s), mParams (tapNONE), mSeq (m),
        mImmutable (false)
    {}

    SLE::pointer getForMod (
        uint256 const & node, Ledger::ref ledger,
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
        Account const& issuer);
};

} // ripple

#endif
