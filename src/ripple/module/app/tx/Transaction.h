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

#ifndef RIPPLE_TRANSACTION_H
#define RIPPLE_TRANSACTION_H

namespace ripple {

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to obtain a binary version.
//

class Database;

enum TransStatus
{
    NEW         = 0, // just received / generated
    INVALID     = 1, // no valid signature, insufficient funds
    INCLUDED    = 2, // added to the current ledger
    CONFLICTED  = 3, // losing to a conflicting transaction
    COMMITTED   = 4, // known to be in a ledger
    HELD        = 5, // not valid now, maybe later
    REMOVED     = 6, // taken out of a ledger
    OBSOLETE    = 7, // a compatible transaction has taken precedence
    INCOMPLETE  = 8  // needs more signatures
};

// This class is for constructing and examining transactions.  Transactions are static so manipulation functions are unnecessary.
class Transaction
    : public std::enable_shared_from_this<Transaction>
    , public CountedObject <Transaction>
{
public:
    static char const* getCountedObjectName () { return "Transaction"; }

    typedef std::shared_ptr<Transaction> pointer;
    typedef const pointer& ref;

public:
    Transaction (SerializedTransaction::ref st, bool bValidate);

    static Transaction::pointer sharedTransaction (Blob const & vucTransaction, bool bValidate);
    static Transaction::pointer transactionFromSQL (Database * db, bool bValidate);

    Transaction (
        TxType ttKind,
        const RippleAddress &    naPublicKey,       // To prove transaction is consistent and authorized.
        const RippleAddress &    naSourceAccount,   // To identify the paying account.
        std::uint32_t            uSeq,               // To order transactions.
        const STAmount &         saFee,             // Transaction fee.
        std::uint32_t            uSourceTag);        // User call back value.


    bool sign (const RippleAddress & naAccountPrivate);

    bool checkSign () const;

    void updateID ()
    {
        mTransactionID = mTransaction->getTransactionID ();
    }

    SerializedTransaction::ref getSTransaction ()
    {
        return mTransaction;
    }

    uint256 const& getID () const
    {
        return mTransactionID;
    }

    const RippleAddress& getFromAccount () const
    {
        return mAccountFrom;
    }

    STAmount getAmount () const
    {
        return mTransaction->getFieldU64 (sfAmount);
    }

    STAmount getFee () const
    {
        return mTransaction->getTransactionFee ();
    }

    std::uint32_t getFromAccountSeq () const
    {
        return mTransaction->getSequence ();
    }

    std::uint32_t getSourceTag () const
    {
        return mTransaction->getFieldU32 (sfSourceTag);
    }

    // VFALCO TODO Should this return a const reference?
    Blob getSignature () const
    {
        return mTransaction->getSignature ();
    }

    LedgerIndex getLedger () const
    {
        return mInLedger;
    }

    TransStatus getStatus () const
    {
        return mStatus;
    }

    TER getResult ()
    {
        return mResult;
    }

    void setResult (TER terResult)
    {
        mResult = terResult;
    }

    void setStatus (TransStatus status, std::uint32_t ledgerSeq);

    void setStatus (TransStatus status)
    {
        mStatus = status;
    }

    void setLedger (LedgerIndex ledger)
    {
        mInLedger = ledger;
    }

    bool operator< (const Transaction&) const;
    bool operator> (const Transaction&) const;
    bool operator== (const Transaction&) const;
    bool operator!= (const Transaction&) const;
    bool operator<= (const Transaction&) const;
    bool operator>= (const Transaction&) const;

    Json::Value getJson (int options, bool binary = false) const;

    static Transaction::pointer load (uint256 const & id);

    // conversion function
    static bool convertToTransactions (std::uint32_t ourLedgerSeq,
                                       std::uint32_t otherLedgerSeq,
                                       bool checkFirstTransactions,
                                       bool checkSecondTransactions,
                                       const SHAMap::Delta & inMap,
                                       std::map<uint256, std::pair<Transaction::pointer,
                                                                   Transaction::pointer> >& outMap);

    static bool isHexTxID (const std::string&);

protected:
    static Transaction::pointer transactionFromSQL (const std::string & statement);

private:
    uint256         mTransactionID;
    RippleAddress   mAccountFrom;
    RippleAddress   mFromPubKey;    // Sign transaction with this. mSignPubKey
    RippleAddress   mSourcePrivate; // Sign transaction with this.

    LedgerIndex     mInLedger;
    TransStatus     mStatus;
    TER             mResult;

    SerializedTransaction::pointer mTransaction;
};

} // ripple

#endif
