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

#ifndef RIPPLE_APP_MISC_VALIDATIONS_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATIONS_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STValidation.h>
#include <memory>
#include <vector>

namespace ripple {

// VFALCO TODO rename and move these type aliases into the Validations interface

// nodes validating and highest node ID validating
using ValidationSet = hash_map<PublicKey, STValidation::pointer>;

using ValidationCounter = std::pair<int, NodeID>;
using LedgerToValidationCounter = hash_map<uint256, ValidationCounter>;

class Validations
{
public:
    virtual ~Validations() = default;

    virtual bool addValidation (STValidation::ref, std::string const& source) = 0;

    virtual bool current (STValidation::ref) = 0;

    virtual ValidationSet getValidations (uint256 const& ledger) = 0;

    virtual int getTrustedValidationCount (uint256 const& ledger) = 0;

    /** Returns fees reported by trusted validators in the given ledger. */
    virtual
    std::vector <std::uint64_t>
    fees (uint256 const& ledger, std::uint64_t base) = 0;

    virtual int getNodesAfter (uint256 const& ledger) = 0;
    virtual int getLoadRatio (bool overLoaded) = 0;

    virtual hash_set<PublicKey> getCurrentPublicKeys () = 0;

    // VFALCO TODO make a type alias for this ugly return value!
    virtual LedgerToValidationCounter getCurrentValidations (
        uint256 currentLedger, uint256 previousLedger,
        LedgerIndex cutoffBefore) = 0;

    /** Return the times of all validations for a particular ledger hash. */
    virtual std::vector<NetClock::time_point> getValidationTimes (
        uint256 const& ledger) = 0;

    virtual std::list <STValidation::pointer>
    getCurrentTrustedValidations () = 0;

    virtual void flush () = 0;

    virtual void sweep () = 0;
};

extern
std::unique_ptr<Validations>
make_Validations(Application& app);

} // ripple

#endif
