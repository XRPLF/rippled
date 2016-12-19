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

#ifndef RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED
#define RIPPLE_APP_LEDGER_LEDGERCONSENSUS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/LedgerProposal.h>
#include <ripple/app/ledger/InboundTransactions.h>
#include <ripple/app/consensus/RCLCxTraits.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/CanonicalTXSet.h>
#include <ripple/app/misc/FeeVote.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <chrono>

namespace ripple {

/** Manager for achieving consensus on the next ledger.
*/
template <class Traits>
class LedgerConsensus : public Traits
{
public:

    using typename Traits::Time_t;
    using typename Traits::Pos_t;
    using typename Traits::TxSet_t;
    using typename Traits::Tx_t;
    using typename Traits::LgrID_t;
    using typename Traits::TxID_t;
    using typename Traits::TxSetID_t;
    using typename Traits::NodeID_t;

    virtual ~LedgerConsensus() = default;

    virtual LgrID_t getLCL () = 0;

};

} // ripple

#endif
