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

#ifndef RIPPLE_VALIDATORS_VALIDATOR_H_INCLUDED
#define RIPPLE_VALIDATORS_VALIDATOR_H_INCLUDED

namespace ripple {
namespace Validators {

// Stored for each distinguishable Validator in the trusted list.
// These are kept in an associative container or multi-index container.
//
class Validator
{
public:
    Validator () : refCount (0)
    {
    }

    void receiveValidation (RippleLedgerHash const& ledgerHash)
    {
        typedef LedgerMap::container_type::iterator iterator;

        ++count->seen;

        // If we already have it in the expected list, close it out
        //
        iterator iter (expected->find (ledgerHash));
        if (iter != expected->end())
        {
            expected->erase (iter);
            expected.back().erase (ledgerHash);
            return;
        }
        else if ((iter = expected.back().find(ledgerHash)) !=
            expected.back().end())
        {
            expected.back().erase (iter);
            return;
        }

        // Ledger hasn't closed yet so put it in the received list
        //
        std::pair <iterator, bool> result (
            received->emplace (ledgerHash, Ledger()));
        bassert (result.second);
        if (received->size() >= maxSizeBeforeSwap)
            swap();
    }

    void ledgerClosed (RippleLedgerHash const& ledgerHash)
    {
        typedef LedgerMap::container_type::iterator iterator;

        ++count->closed;

        // If the Validator already gave us the ledger
        // then count it and remove it from both tables.
        //
        iterator iter (received->find (ledgerHash));
        if (iter != received->end())
        {
            received->erase (iter);
            received.back().erase (ledgerHash);
            return;
        }
        else if ((iter = received.back().find (ledgerHash)) !=
            received.back().end())
        {
            received.back().erase (iter);
            return;
        }

        // We haven't seen this ledger hash from the
        // validator yet so put it on the expected list
        //
        std::pair <iterator, bool> result (
            expected->emplace (ledgerHash, Ledger ()));
        bassert (result.second);
        if (expected->size() >= maxSizeBeforeSwap)
            swap();
    }

    void swap()
    {
        // Count anything in the old expected list as missing
        count->missing += expected.back().size();

        // Count anything in the old received list as orphaned
        count->orphans += received.back().size();

        // Rotate and clear
        count.swap();
        expected.swap();
        received.swap();
        count->clear();
        expected->clear();
        received->clear();
    }

    struct Count
    {
        Count()
            : closed (0)
            , seen (0)
            , missing (0)
            , orphans (0)
        {
        }

        void clear ()
        {
            *this = Count();
        }

        // How many LedgerMap we've seen
        std::size_t closed;

        // How many validation's we've seen
        std::size_t seen;

        // Estimate of validation's that were missed
        std::size_t missing;

        // Estimate of validations not belonging to any ledger
        std::size_t orphans;
    };

    int refCount;

    AgedHistory <Count> count;
    LedgerMap received;
    LedgerMap expected;
};

//------------------------------------------------------------------------------

typedef boost::unordered_map <
    RipplePublicKey, Validator,
        RipplePublicKey::hasher> ValidatorMap;

// The master in-memory database of Validator, indexed by all the
// possible things that we need to care about, and even some that we don't.
//
/*
typedef boost::multi_index_container <
    Validator, boost::multi_index::indexed_by <
            
        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(Logic::Validator,UniqueID,uniqueID)>,

        boost::multi_index::hashed_unique <
            BOOST_MULTI_INDEX_MEMBER(Logic::Validator,IPEndpoint,endpoint),
            Connectible::HashAddress>
    >
> ValidationsMap;
*/

}
}

#endif
