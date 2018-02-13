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

#ifndef RIPPLE_APP_CONSENSUSS_VALIDATIONS_H_INCLUDED
#define RIPPLE_APP_CONSENSUSS_VALIDATIONS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/ScopedLock.h>
#include <ripple/consensus/Validations.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STValidation.h>
#include <vector>

namespace ripple {

class Application;

/** Wrapper over STValidation for generic Validation code

    Wraps an STValidation::pointer for compatibility with the generic validation
    code.
*/
class RCLValidation
{
    STValidation::pointer val_;
public:
    using NodeKey = ripple::PublicKey;
    using NodeID = ripple::NodeID;

    /** Constructor

        @param v The validation to wrap.
    */
    RCLValidation(STValidation::pointer const& v) : val_{v}
    {
    }

    /// Validated ledger's hash
    uint256
    ledgerID() const
    {
        return val_->getLedgerHash();
    }

    /// Validated ledger's sequence number (0 if none)
    std::uint32_t
    seq() const
    {
        return val_->getFieldU32(sfLedgerSequence);
    }

    /// Validation's signing time
    NetClock::time_point
    signTime() const
    {
        return val_->getSignTime();
    }

    /// Validated ledger's first seen time
    NetClock::time_point
    seenTime() const
    {
        return val_->getSeenTime();
    }

    /// Public key of validator that published the validation
    PublicKey
    key() const
    {
        return val_->getSignerPublic();
    }

    /// NodeID of validator that published the validation
    NodeID
    nodeID() const
    {
        return val_->getNodeID();
    }

    /// Whether the validation is considered trusted.
    bool
    trusted() const
    {
        return val_->isTrusted();
    }

    void
    setTrusted()
    {
        val_->setTrusted();
    }

    void
    setUntrusted()
    {
        val_->setUntrusted();
    }

    /// Whether the validation is full (not-partial)
    bool
    full() const
    {
        return val_->isFull();
    }

    /// Get the load fee of the validation if it exists
    boost::optional<std::uint32_t>
    loadFee() const
    {
        return ~(*val_)[~sfLoadFee];
    }

    /// Extract the underlying STValidation being wrapped
    STValidation::pointer
    unwrap() const
    {
        return val_;
    }

};

/** Wraps a ledger instance for use in generic Validations LedgerTrie.

    The LedgerTrie models a ledger's history as a map from Seq -> ID. Any
    two ledgers that have the same ID for a given Seq have the same ID for
    all earlier sequences (e.g. shared ancestry). In practice, a ledger only
    conveniently has the prior 256 ancestor hashes available. For
    RCLValidatedLedger, we treat any ledgers separated by more than 256 Seq as
    distinct.
*/
class RCLValidatedLedger
{
public:
    using ID = LedgerHash;
    using Seq = LedgerIndex;
    struct MakeGenesis
    {
    };

    RCLValidatedLedger(MakeGenesis);

    RCLValidatedLedger(
        std::shared_ptr<Ledger const> const& ledger,
        beast::Journal j);

    /// The sequence (index) of the ledger
    Seq
    seq() const;

    /// The ID (hash) of the ledger
    ID
    id() const;

    /** Lookup the ID of the ancestor ledger

        @param s The sequence (index) of the ancestor
        @return The ID of this ledger's ancestor with that sequence number or
                ID{0} if one was not determined
    */
    ID operator[](Seq const& s) const;

    /// Find the sequence number of the earliest mismatching ancestor
    friend Seq
    mismatch(RCLValidatedLedger const& a, RCLValidatedLedger const& b);

    Seq
    minSeq() const;

private:
    ID ledgerID_;
    Seq ledgerSeq_;
    std::vector<uint256> ancestors_;
    beast::Journal j_;
};

/** Generic validations adaptor class for RCL

    Manages storing and writing stale RCLValidations to the sqlite DB and
    acquiring validated ledgers from the network.
*/
class RCLValidationsAdaptor
{
public:
    // Type definitions for generic Validation
    using Mutex = std::mutex;
    using Validation = RCLValidation;
    using Ledger = RCLValidatedLedger;

    RCLValidationsAdaptor(Application& app, beast::Journal j);

    /** Current time used to determine if validations are stale.
     */
    NetClock::time_point
    now() const;

    /** Handle a newly stale validation.

        @param v The newly stale validation

        @warning This should do minimal work, as it is expected to be called
                 by the generic Validations code while it may be holding an
                 internal lock
    */
    void
    onStale(RCLValidation&& v);

    /** Flush current validations to disk before shutdown.

        @param remaining The remaining validations to flush
    */
    void
    flush(hash_map<NodeID, RCLValidation>&& remaining);

    /** Attempt to acquire the ledger with given id from the network */
    boost::optional<RCLValidatedLedger>
    acquire(LedgerHash const & id);

    beast::Journal
    journal() const
    {
        return j_;
    }

private:
    using ScopedLockType = std::lock_guard<Mutex>;
    using ScopedUnlockType = GenericScopedUnlock<Mutex>;

    Application& app_;
    beast::Journal j_;

    // Lock for managing staleValidations_ and writing_
    std::mutex staleLock_;
    std::vector<RCLValidation> staleValidations_;
    bool staleWriting_ = false;

    // Write the stale validations to sqlite DB, the scoped lock argument
    // is used to remind callers that the staleLock_ must be *locked* prior
    // to making the call
    void
    doStaleWrite(ScopedLockType&);
};

/// Alias for RCL-specific instantiation of generic Validations
using RCLValidations = Validations<RCLValidationsAdaptor>;


/** Handle a new validation

    Also sets the trust status of a validation based on the validating node's
    public key and this node's current UNL.

    @param app Application object containing validations and ledgerMaster
    @param val The validation to add
    @param source Name associated with validation used in logging

    @return Whether the validation should be relayed
*/
bool
handleNewValidation(
    Application& app,
    STValidation::ref val,
    std::string const& source);

}  // namespace ripple

#endif
