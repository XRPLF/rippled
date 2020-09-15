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

#ifndef RIPPLE_APP_LEDGER_ACCEPTEDLEDGER_H_INCLUDED
#define RIPPLE_APP_LEDGER_ACCEPTEDLEDGER_H_INCLUDED

#include <ripple/app/ledger/AcceptedLedgerTx.h>
#include <ripple/protocol/AccountID.h>

namespace ripple {

/** A ledger that has become irrevocable.

    An accepted ledger is a ledger that has a sufficient number of
    validations to convince the local server that it is irrevocable.

    The existence of an accepted ledger implies all preceding ledgers
    are accepted.
*/
/* VFALCO TODO digest this terminology clarification:
    Closed and accepted refer to ledgers that have not passed the
    validation threshold yet. Once they pass the threshold, they are
    "Validated". Closed just means its close time has passed and no
    new transactions can get in. "Accepted" means we believe it to be
    the result of the a consensus process (though haven't validated
    it yet).
*/
class AcceptedLedger
{
public:
    using pointer = std::shared_ptr<AcceptedLedger>;
    using ret = const pointer&;
    using map_t = std::map<int, AcceptedLedgerTx::pointer>;
    // mapt_t must be an ordered map!
    using value_type = map_t::value_type;
    using const_iterator = map_t::const_iterator;

public:
    std::shared_ptr<ReadView const> const&
    getLedger() const
    {
        return mLedger;
    }
    const map_t&
    getMap() const
    {
        return mMap;
    }

    int
    getTxnCount() const
    {
        return mMap.size();
    }

    AcceptedLedgerTx::pointer
    getTxn(int) const;

    AcceptedLedger(
        std::shared_ptr<ReadView const> const& ledger,
        Application& app);

private:
    void insert(AcceptedLedgerTx::ref);

    std::shared_ptr<ReadView const> mLedger;
    map_t mMap;
};

}  // namespace ripple

#endif
