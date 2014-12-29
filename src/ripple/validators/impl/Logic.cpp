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

#include <BeastConfig.h>
#include <ripple/validators/impl/ConnectionImp.h>
#include <ripple/validators/impl/Logic.h>

/*

Questions the code should answer:

Most important thing that we do:
    Determine the new last fully validated ledger



- Are we robustly connected to the Ripple network?

- Given a new recent validation for a ledger with a sequence number higher
  than the last fully validated ledger, do we have a new last fully validated
  ledger?

- What's the latest fully validated ledger?

    Sequence number must always be known to set a fully validated ledger

    Accumulate validations from nodes you trust at least a little bit,
    and that aren't stale.

    If you have a last fully validated ledger then validations for ledgers
    with lower sequence numbers can be ignored.

    Flow of validations recent in time for sequence numbers greater or equal than
    the last fully validated ledger.

- What ledger is the current consenus round built on?

- Determine when the current consensus round is over?
    Criteria: Number of validations for a ledger that comes after.

*/

namespace ripple {
namespace Validators {
    
Logic::Logic (Store& store, beast::Journal journal)
    : /*store_ (store)
    , */journal_ (journal)
    , ledgers_(get_seconds_clock())
{
}

void
Logic::stop()
{
}

void
Logic::load()
{
}

void
Logic::add (ConnectionImp& c)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.insert(&c);
}

void
Logic::remove (ConnectionImp& c)
{
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.erase(&c);
}

bool
Logic::isStale (STValidation const& v)
{
    return false;
}

void
Logic::onTimer()
{
    std::lock_guard<std::mutex> lock(mutex_);
    beast::expire(ledgers_, std::chrono::minutes(5));
}

void
Logic::onValidation (STValidation const& v)
{
    assert(v.isFieldPresent (sfLedgerSequence));
    auto const seq_no =
        v.getFieldU32 (sfLedgerSequence);
    auto const key = v.getSignerPublic();
    auto const ledger = v.getLedgerHash();

    std::lock_guard<std::mutex> lock(mutex_);
    if (journal_.trace) journal_.trace <<
        "onValidation: " << ledger;
    auto const result = ledgers_.emplace (std::piecewise_construct,
        std::make_tuple(ledger), std::make_tuple());
    auto& meta = result.first->second;
    assert(result.second || seq_no == meta.seq_no);
    if (result.second)
        meta.seq_no = seq_no;
    meta.keys.insert (v.getSignerPublic());
    if (meta.seq_no > latest_.second.seq_no)
    {
        if (policy_.acceptLedgerMeta (*result.first))
        {
            //ledgers_.clear();
            latest_ = *result.first;
            if (journal_.info) journal_.info <<
                "Accepted " << latest_.second.seq_no <<
                    " (" << ledger << ")";
            for (auto& _ : connections_)
                _->onLedger(latest_.first);
        }
    }
}

void
Logic::onLedgerClosed (LedgerIndex index,
    LedgerHash const& hash, LedgerHash const& parent)
{
    if (journal_.info) journal_.info <<
        "onLedgerClosed: " << index << " " << hash << " (parent " << parent << ")";
}

}
}
