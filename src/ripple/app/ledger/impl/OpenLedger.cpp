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

#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/tx/apply.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/predicates.h>
#include <ripple/protocol/Feature.h>
#include <boost/range/adaptor/transformed.hpp>

namespace ripple {

OpenLedger::OpenLedger(
    std::shared_ptr<Ledger const> const& ledger,
    CachedSLEs& cache,
    beast::Journal journal)
    : j_(journal), cache_(cache), current_(create(ledger->rules(), ledger))
{
}

bool
OpenLedger::empty() const
{
    std::lock_guard lock(modify_mutex_);
    return current_->txCount() == 0;
}

std::shared_ptr<OpenView const>
OpenLedger::current() const
{
    std::lock_guard lock(current_mutex_);
    return current_;
}

bool
OpenLedger::modify(modify_type const& f)
{
    std::lock_guard lock1(modify_mutex_);
    auto next = std::make_shared<OpenView>(*current_);
    auto const changed = f(*next, j_);
    if (changed)
    {
        std::lock_guard lock2(current_mutex_);
        current_ = std::move(next);
    }
    return changed;
}

void
OpenLedger::accept(
    Application& app,
    Rules const& rules,
    std::shared_ptr<Ledger const> const& ledger,
    OrderedTxs const& locals,
    bool retriesFirst,
    OrderedTxs& retries,
    ApplyFlags flags,
    std::string const& suffix,
    modify_type const& f)
{
    JLOG(j_.trace()) << "accept ledger " << ledger->seq() << " " << suffix;
    auto next = create(rules, ledger);
    std::map<uint256, bool> shouldRecover;
    if (retriesFirst)
    {
        for (auto const& tx : retries)
        {
            auto const txID = tx.second->getTransactionID();
            shouldRecover[txID] = app.getHashRouter().shouldRecover(txID);
        }
        // Handle disputed tx, outside lock
        using empty = std::vector<std::shared_ptr<STTx const>>;
        apply(app, *next, *ledger, empty{}, retries, flags, shouldRecover, j_);
    }
    // Block calls to modify, otherwise
    // new tx going into the open ledger
    // would get lost.
    std::lock_guard lock1(modify_mutex_);
    // Apply tx from the current open view
    if (!current_->txs.empty())
    {
        for (auto const& tx : current_->txs)
        {
            auto const txID = tx.first->getTransactionID();
            auto iter = shouldRecover.lower_bound(txID);
            if (iter != shouldRecover.end() && iter->first == txID)
                // already had a chance via disputes
                iter->second = false;
            else
                shouldRecover.emplace_hint(
                    iter, txID, app.getHashRouter().shouldRecover(txID));
        }
        apply(
            app,
            *next,
            *ledger,
            boost::adaptors::transform(
                current_->txs,
                [](std::pair<
                    std::shared_ptr<STTx const>,
                    std::shared_ptr<STObject const>> const& p) {
                    return p.first;
                }),
            retries,
            flags,
            shouldRecover,
            j_);
    }
    // Call the modifier
    if (f)
        f(*next, j_);
    // Apply local tx
    for (auto const& item : locals)
        app.getTxQ().apply(app, *next, item.second, flags, j_);

    // If we didn't relay this transaction recently, relay it to all peers
    for (auto const& txpair : next->txs)
    {
        auto const& tx = txpair.first;
        auto const txId = tx->getTransactionID();
        if (auto const toSkip = app.getHashRouter().shouldRelay(txId))
        {
            JLOG(j_.debug()) << "Relaying recovered tx " << txId;
            protocol::TMTransaction msg;
            Serializer s;

            tx->add(s);
            msg.set_rawtransaction(s.data(), s.size());
            msg.set_status(protocol::tsNEW);
            msg.set_receivetimestamp(
                app.timeKeeper().now().time_since_epoch().count());
            app.overlay().foreach (send_if_not(
                std::make_shared<Message>(msg, protocol::mtTRANSACTION),
                peer_in_set(*toSkip)));
        }
    }

    // Switch to the new open view
    std::lock_guard lock2(current_mutex_);
    current_ = std::move(next);
}

//------------------------------------------------------------------------------

std::shared_ptr<OpenView>
OpenLedger::create(
    Rules const& rules,
    std::shared_ptr<Ledger const> const& ledger)
{
    return std::make_shared<OpenView>(
        open_ledger,
        rules,
        std::make_shared<CachedLedger const>(ledger, cache_));
}

auto
OpenLedger::apply_one(
    Application& app,
    OpenView& view,
    std::shared_ptr<STTx const> const& tx,
    bool retry,
    ApplyFlags flags,
    bool shouldRecover,
    beast::Journal j) -> Result
{
    if (retry)
        flags = flags | tapRETRY;
    auto const result = [&] {
        auto const queueResult =
            app.getTxQ().apply(app, view, tx, flags | tapPREFER_QUEUE, j);
        // If the transaction can't get into the queue for intrinsic
        // reasons, and it can still be recovered, try to put it
        // directly into the open ledger, else drop it.
        if (queueResult.first == telCAN_NOT_QUEUE && shouldRecover)
            return ripple::apply(app, view, *tx, flags, j);
        return queueResult;
    }();
    if (result.second || result.first == terQUEUED)
        return Result::success;
    if (isTefFailure(result.first) || isTemMalformed(result.first) ||
        isTelLocal(result.first))
        return Result::failure;
    return Result::retry;
}

//------------------------------------------------------------------------------

std::string
debugTxstr(std::shared_ptr<STTx const> const& tx)
{
    std::stringstream ss;
    ss << tx->getTransactionID();
    return ss.str().substr(0, 4);
}

std::string
debugTostr(OrderedTxs const& set)
{
    std::stringstream ss;
    for (auto const& item : set)
        ss << debugTxstr(item.second) << ", ";
    return ss.str();
}

std::string
debugTostr(SHAMap const& set)
{
    std::stringstream ss;
    for (auto const& item : set)
    {
        try
        {
            SerialIter sit(item.slice());
            auto const tx = std::make_shared<STTx const>(sit);
            ss << debugTxstr(tx) << ", ";
        }
        catch (std::exception const&)
        {
            ss << "THRO, ";
        }
    }
    return ss.str();
}

std::string
debugTostr(std::shared_ptr<ReadView const> const& view)
{
    std::stringstream ss;
    for (auto const& item : view->txs)
        ss << debugTxstr(item.first) << ", ";
    return ss.str();
}

}  // namespace ripple
