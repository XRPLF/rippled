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

#include <ripple/basics/RangeSet.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxMeta.h>
#include <boost/optional.hpp>
#include <optional>
#include <variant>

namespace ripple {

//
// Transactions should be constructed in JSON with. Use STObject::parseJson to
// obtain a binary version.
//

class Application;
class Database;
class Rules;

enum TransStatus : std::uint8_t {
    NEW = 0,         // just received / generated
    INVALID = 1,     // no valid signature, insufficient funds
    INCLUDED = 2,    // added to the current ledger
    CONFLICTED = 3,  // losing to a conflicting transaction
    COMMITTED = 4,   // known to be in a ledger
    HELD = 5,        // not valid now, maybe later
    REMOVED = 6,     // taken out of a ledger
    OBSOLETE = 7,    // a compatible transaction has taken precedence
    INCOMPLETE = 8   // needs more signatures
};

enum class TxSearched { all, some, unknown };

// The boost::optional parameter is because SOCI requires
// boost::optional (not std::optional) parameters.
TransStatus
sqlTransactionStatus(boost::optional<std::string> const& status);

// This class is for constructing and examining transactions.
// Transactions are static so manipulation functions are unnecessary.
class Transaction : public std::enable_shared_from_this<Transaction>,
                    public CountedObject<Transaction>
{
    /** Flags used for the state of a transaction. */
    constexpr static std::uint8_t const applied_ = 1;
    constexpr static std::uint8_t const broadcast_ = 2;
    constexpr static std::uint8_t const queued_ = 4;
    constexpr static std::uint8_t const kept_ = 8;
    constexpr static std::uint8_t const applying_ = 16;

public:
    Transaction(std::shared_ptr<STTx const> const& stx) noexcept
        : mTransaction(stx)
    {
    }

    std::shared_ptr<STTx const> const&
    getSTransaction()
    {
        return mTransaction;
    }

    uint256
    getID() const
    {
        return mTransaction->getTransactionID();
    }

    LedgerIndex
    getLedger() const
    {
        return mInLedger;
    }

    bool
    isValidated() const
    {
        return mInLedger != 0;
    }

    TransStatus
    getStatus() const
    {
        return status_;
    }

    TER
    getResult()
    {
        return mResult;
    }

    void
    setResult(TER terResult)
    {
        mResult = terResult;
    }

    void
    setStatus(TransStatus status, std::uint32_t ledgerSeq)
    {
        status_ = status;
        mInLedger = ledgerSeq;
    }

    void
    setStatus(TransStatus status)
    {
        status_ = status;
    }

    /**
     * Set this flag once added to a batch.
     */
    void
    setApplying()
    {
        state_ |= applying_;
    }

    /**
     * Detect if transaction is being batched.
     *
     * @return Whether transaction is being applied within a batch.
     */
    bool
    getApplying()
    {
        return (state_ & applying_) == applying_;
    }

    /**
     * Indicate that transaction application has been attempted.
     */
    void
    clearApplying()
    {
        state_ &= ~applying_;
    }

    class SubmitResult
    {
        friend class Transaction;

        SubmitResult(std::uint8_t state)
            : applied(state & applied_)
            , broadcast(state & broadcast_)
            , queued(state & queued_)
            , kept(state & kept_)
        {
        }

    public:
        bool const applied;
        bool const broadcast;
        bool const queued;
        bool const kept;

        /** Returns true if any state if true */
        bool
        any() const
        {
            return applied || broadcast || queued || kept;
        }
    };

    /**
     * @brief getSubmitResult Return submit result
     * @return SubmitResult struct
     */
    SubmitResult
    getSubmitResult() const
    {
        return {state_.load()};
    }

    /**
     * @brief clearSubmitResult Clear all flags in SubmitResult
     */
    void
    clearSubmitResult()
    {
        state_ &= ~(applied_ | broadcast_ | queued_ | kept_);
    }

    /** Note that the transaction was applied to open ledger */
    void
    setApplied()
    {
        state_ |= applied_;
    }

    /** Note that the trnasaction was put into heldtxns queue */
    void
    setQueued()
    {
        state_ |= queued_;
    }

    /** Note that the transaction was broadcast via network */
    void
    setBroadcast()
    {
        state_ |= broadcast_;
    }

    /** Note that the transaction was put to localtxns queue */
    void
    setKept()
    {
        state_ |= kept_;
    }

    struct CurrentLedgerState
    {
        CurrentLedgerState() = delete;

        CurrentLedgerState(
            LedgerIndex li,
            XRPAmount fee,
            std::uint32_t accSeqNext,
            std::uint32_t accSeqAvail)
            : validatedLedger{li}
            , minFeeRequired{fee}
            , accountSeqNext{accSeqNext}
            , accountSeqAvail{accSeqAvail}
        {
        }

        LedgerIndex validatedLedger;
        XRPAmount minFeeRequired;
        std::uint32_t accountSeqNext;
        std::uint32_t accountSeqAvail;
    };

    /**
     * @brief getCurrentLedgerState Get current ledger state of transaction
     * @return Current ledger state
     */
    std::optional<CurrentLedgerState>
    getCurrentLedgerState() const
    {
        return currentLedgerState_;
    }

    /**
     * @brief setCurrentLedgerState Set current ledger state of transaction
     * @param validatedLedger Number of last validated ledger
     * @param fee minimum Fee required for the transaction
     * @param accountSeq First valid account sequence in current ledger
     * @param availableSeq First available sequence for the transaction
     */
    void
    setCurrentLedgerState(
        LedgerIndex validatedLedger,
        XRPAmount fee,
        std::uint32_t accountSeq,
        std::uint32_t availableSeq)
    {
        currentLedgerState_.emplace(
            validatedLedger, fee, accountSeq, availableSeq);
    }

    Json::Value
    getJson(Application& app, JsonOptions options, bool binary = false) const;

    // Information used to locate a transaction.
    // Contains a nodestore hash and ledger sequence pair if the transaction was
    // found. Otherwise, contains the range of ledgers present in the database
    // at the time of search.
    struct Locator
    {
        std::variant<std::pair<uint256, uint32_t>, ClosedInterval<uint32_t>>
            locator;

        // @return true if transaction was found, false otherwise
        //
        // Call this function first to determine the type of the contained info.
        // Calling the wrong getter function will throw an exception.
        // See documentation for the getter functions for more details
        bool
        isFound()
        {
            return std::holds_alternative<std::pair<uint256, uint32_t>>(
                locator);
        }

        // @return key used to find transaction in nodestore
        //
        // Throws if isFound() returns false
        uint256 const&
        getNodestoreHash()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).first;
        }

        // @return sequence of ledger containing the transaction
        //
        // Throws is isFound() returns false
        uint32_t
        getLedgerSequence()
        {
            return std::get<std::pair<uint256, uint32_t>>(locator).second;
        }

        // @return range of ledgers searched
        //
        // Throws if isFound() returns true
        ClosedInterval<uint32_t> const&
        getLedgerRangeSearched()
        {
            return std::get<ClosedInterval<uint32_t>>(locator);
        }
    };

    static Locator
    locate(uint256 const& id, Application& app);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(uint256 const& id, Application& app, error_code_i& ec);

    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        ClosedInterval<uint32_t> const& range,
        error_code_i& ec);

private:
    static std::variant<
        std::pair<std::shared_ptr<Transaction>, std::shared_ptr<TxMeta>>,
        TxSearched>
    load(
        uint256 const& id,
        Application& app,
        std::optional<ClosedInterval<uint32_t>> const& range,
        error_code_i& ec);

    /** The ledger in which this transaction appears, or 0. */
    LedgerIndex mInLedger = 0;

    /** The result of applying this transaction. */
    TER mResult = temUNCERTAIN;

    /** The current state of the transaction. */
    std::atomic<std::uint8_t> state_ = 0;

    /** The status of the transaction. */
    TransStatus status_ = NEW;

    std::shared_ptr<STTx const> mTransaction;
    std::optional<CurrentLedgerState> currentLedgerState_;
};

}  // namespace ripple

#endif
