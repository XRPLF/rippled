//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_LEDGER_BUILD_LEDGER_H_INCLUDED
#define RIPPLE_APP_LEDGER_BUILD_LEDGER_H_INCLUDED

#include <ripple/ledger/ApplyView.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <chrono>
#include <memory>

namespace ripple {

class Application;
class CanonicalTXSet;
class Ledger;
class LedgerReplay;
class SHAMap;


/** Build a new ledger by applying consensus transactions

    Build a new ledger by applying a set of transactions accepted as part of
    consensus.

    @param parent The ledger to apply transactions to
    @param closeTime The time the ledger closed
    @param closeTimeCorrect Whether consensus agreed on close time
    @param closeResolution Resolution used to determine consensus close time
    @param txs The consensus transactions to attempt to apply
    @param app Handle to application instance
    @param retriableTxs Populate with transactions to retry in next round
    @param j Journal to use for logging
    @return The newly built ledger
 */
std::shared_ptr<Ledger>
buildLedger(
    std::shared_ptr<Ledger const> const& parent,
    NetClock::time_point closeTime,
    const bool closeTimeCorrect,
    NetClock::duration closeResolution,
    SHAMap const& txs,
    Application& app,
    CanonicalTXSet& retriableTxs,
    beast::Journal j);

/** Build a new ledger by replaying transactions

    Build a new ledger by replaying transactions accepted into a prior ledger.

    @param replayData Data of the ledger to replay
    @param applyFlags Flags to use when applying transactions
    @param app Handle to application instance
    @param j Journal to use for logging
    @return The newly built ledger
 */
std::shared_ptr<Ledger>
buildLedger(
    LedgerReplay const& replayData,
    ApplyFlags applyFlags,
    Application& app,
    beast::Journal j);

}  // namespace ripple
#endif
