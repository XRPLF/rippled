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

#include <ripple/basics/ScopedLock.h>
#include <ripple/consensus/Validations.h>
#include <ripple/protocol/Protocol.h>
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
        if(auto res = (*val_)[~sfLedgerSequence])
            return *res;
        return 0;
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

/** Implements the StalePolicy policy class for adapting Validations in the RCL

    Manages storing and writing stale RCLValidations to the sqlite DB.
*/
class RCLValidationsPolicy
{
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard<LockType>;
    using ScopedUnlockType = GenericScopedUnlock<LockType>;

    Application& app_;

    // Lock for managing staleValidations_ and writing_
    std::mutex staleLock_;
    std::vector<RCLValidation> staleValidations_;
    bool staleWriting_ = false;

    // Write the stale validations to sqlite DB, the scoped lock argument
    // is used to remind callers that the staleLock_ must be *locked* prior
    // to making the call
    void
    doStaleWrite(ScopedLockType&);

public:

    RCLValidationsPolicy(Application & app);

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
    flush(hash_map<PublicKey, RCLValidation> && remaining);
};


/// Alias for RCL-specific instantiation of generic Validations
using RCLValidations =
    Validations<RCLValidationsPolicy, RCLValidation, std::mutex>;

/** Handle a new validation

    1. Set the trust status of a validation based on the validating node's
       public key and this node's current UNL.
    2. Add the validation to the set of validations if current.
    3. If new and trusted, send the validation to the ledgerMaster.

    @param app Application object containing validations and ledgerMaster
    @param val The validation to add
    @param source Name associated with validation used in logging

    @return Whether the validation should be relayed
*/
bool
handleNewValidation(Application & app, STValidation::ref val, std::string const& source);


}  // namespace ripple

#endif
