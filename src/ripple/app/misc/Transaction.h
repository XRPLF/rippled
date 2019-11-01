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

#ifndef RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED
#define RIPPLE_APP_MISC_TRANSACTION_H_INCLUDED

#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/basics/RangeSet.h>
#include <boost/optional.hpp>

namespace ripple {

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to
// obtain a binary version.
//

class Application;
class Database;
class Rules;

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

// This class is for constructing and examining transactions.
// Transactions are static so manipulation functions are unnecessary.
class Transaction
    : public std::enable_shared_from_this<Transaction>
    , public CountedObject <Transaction>
{
public:
    static char const* getCountedObjectName () { return "Transaction"; }

    using pointer = std::shared_ptr<Transaction>;
    using ref = const pointer&;

public:
    Transaction (
        std::shared_ptr<STTx const> const&, std::string&, Application&) noexcept;

    static
    Transaction::pointer
    transactionFromSQL (
        boost::optional<std::uint64_t> const& ledgerSeq,
        boost::optional<std::string> const& status,
        Blob const& rawTxn,
        Application& app);

    static
    TransStatus
    sqlTransactionStatus(boost::optional<std::string> const& status);

    std::shared_ptr<STTx const> const& getSTransaction ()
    {
        return mTransaction;
    }

    uint256 const& getID () const
    {
        return mTransactionID;
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

    /**
     * Set this flag once added to a batch.
     */
    void setApplying()
    {
        mApplying = true;
    }

    /**
     * Detect if transaction is being batched.
     *
     * @return Whether transaction is being applied within a batch.
     */
    bool getApplying()
    {
        return mApplying;
    }

    /**
     * Indicate that transaction application has been attempted.
     */
    void clearApplying()
    {
        mApplying = false;
    }

    Json::Value getJson (JsonOptions options, bool binary = false) const;

    static Transaction::pointer load (uint256 const& id, Application& app,
        error_code_i& ec, ClosedInterval<uint32_t> const& range = {},
            bool* searchedAll = nullptr);

private:
    uint256         mTransactionID;

    LedgerIndex     mInLedger = 0;
    TransStatus     mStatus = INVALID;
    TER             mResult = temUNCERTAIN;
    bool            mApplying = false;

    std::shared_ptr<STTx const>   mTransaction;
    Application&    mApp;
    beast::Journal  j_;
};

} // ripple

#endif
