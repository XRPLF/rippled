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
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/tx/apply.h>
#include <boost/range/adaptor/transformed.hpp>

namespace ripple {

namespace detail {

auto
CachedSLEs::find(
    key_type const& key) ->
        value_type
{
    auto const iter =
        map_.find(key);
    if (iter == map_.end())
        return nullptr;
    map_.touch(iter);
    return iter->second;
}

auto
CachedSLEs::insert(
    key_type const& key,
        value_type const& value) ->
            std::pair<value_type, bool>
{
    beast::expire(map_, timeToLive_);
    auto const result =
        map_.emplace(key, value);
    if (result.second)
        return { value, true };
    return { result.first->second, false };
}

} // detail

//------------------------------------------------------------------------------

class CachedSLEView
    : public BasicViewWrapper<Ledger&>
{
private:
    detail::CachedSLEs& cache_;
    std::mutex mutable mutex_;
    std::shared_ptr<Ledger> ledger_;

public:
    // Retains ownership of ledger
    // for lifetime management.
    CachedSLEView(
        std::shared_ptr<Ledger> const& ledger,
            detail::CachedSLEs& cache)
        : BasicViewWrapper(*ledger)
        , cache_ (cache)
        , ledger_ (ledger)
    {
    }

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;
};

std::shared_ptr<SLE const>
CachedSLEView::read (Keylet const& k) const
{
    key_type key;
    auto const item =
        view_.stateMap().peekItem(k.key, key);
    if (! item)
        return nullptr;
    {
        std::lock_guard<
            std::mutex> lock(mutex_);
        if (auto sle = cache_.find(key))
        {
            if(! k.check(*sle))
                return nullptr;
            return sle;
        }
    }
    SerialIter sit(item->slice());
    // VFALCO This should be <SLE const>
    auto sle = std::make_shared<
        SLE>(sit, item->key());
    if (! k.check(*sle))
        return nullptr;
    // VFALCO TODO Eliminate "immutable" runtime property
    sle->setImmutable ();
    std::lock_guard<
        std::mutex> lock(mutex_);
    auto const result =
        cache_.insert(key, sle);
    if (! result.second)
        return result.first;
    // Need std::move to avoid a copy
    // because return type is different
    return std::move(sle);
}

//------------------------------------------------------------------------------

OpenLedger::OpenLedger(std::shared_ptr<
    Ledger> const& ledger,
        Config const& config,
            Stopwatch& clock,
                beast::Journal journal)
    : j_ (journal)
    , config_ (config)
    , clock_ (clock)
    , cache_ (std::chrono::minutes(1), clock)
    , current_ (create(ledger))
{
}

std::shared_ptr<BasicView const>
OpenLedger::current() const
{
    std::lock_guard<
        std::mutex> lock(
            current_mutex_);
    return current_;
}

bool
OpenLedger::modify (std::function<
    bool(View&, beast::Journal)> const& f)
{
    std::lock_guard<
        std::mutex> lock1(modify_mutex_);
    auto next = std::make_shared<
        MetaView>(shallow_copy, *current_);
    auto const changed = f(*next, j_);
    if (changed)
    {
        std::lock_guard<
            std::mutex> lock2(
                current_mutex_);
        current_ = std::move(next);
    }
    return changed;
}

void
OpenLedger::accept (std::shared_ptr<Ledger> const& ledger,
    OrderedTxs const& locals, bool retriesFirst,
        OrderedTxs& retries, IHashRouter& router,
            std::string const& suffix)
{
    JLOG(j_.error) <<
        "accept ledger " << ledger->seq() << " " << suffix;
    auto next = create(ledger);
    if (retriesFirst)
    {
        // Handle disputed tx, outside lock
        using empty =
            std::vector<std::shared_ptr<
                STTx const>>;
        apply (*next, *ledger, empty{},
            retries, router, config_, j_);
    }
    // Block calls to modify, otherwise
    // new tx going into the open ledger
    // would get lost.
    std::lock_guard<
        std::mutex> lock1(modify_mutex_);
    // Apply tx from the current open view
    if (! current_->txs.empty())
        apply (*next, *ledger,
            boost::adaptors::transform(
                current_->txs,
            [](std::pair<std::shared_ptr<
                STTx const>, std::shared_ptr<
                    STObject const>> const& p)
            {
                return p.first;
            }),
                retries, router, config_, j_);
    // Apply local tx
    for (auto const& item : locals)
        ripple::apply(*next, *item.second,
            tapNONE, config_, j_);
    // Switch to the new open view
    std::lock_guard<
        std::mutex> lock2(current_mutex_);
    current_ = std::move(next);
}

//------------------------------------------------------------------------------

std::shared_ptr<MetaView>
OpenLedger::create (std::shared_ptr<
    Ledger> const& ledger)
{
    auto cache = std::make_shared<
        CachedSLEView>(ledger, cache_);
    return std::make_shared<
        MetaView>(open_ledger,
            *cache, cache);
}

auto
OpenLedger::apply_one (View& view,
    std::shared_ptr<STTx const> const& tx,
        bool retry, IHashRouter& router,
            Config const& config, beast::Journal j) ->
                Result
{
    auto flags = view.flags();
    if (retry)
        flags = flags | tapRETRY;
    if ((router.getFlags(
        tx->getTransactionID()) & SF_SIGGOOD) ==
            SF_SIGGOOD)
        flags = flags | tapNO_CHECK_SIGN;
    auto const result = ripple::apply(
        view, *tx, flags, config, j);
    if (result.second)
        return Result::success;
    if (isTefFailure (result.first) ||
        isTemMalformed (result.first) ||
            isTelLocal (result.first))
        return Result::failure;
    return Result::retry;
}

//------------------------------------------------------------------------------

bool
OpenLedger::verify (Ledger const& ledger,
    std::string const& suffix) const
{
    std::lock_guard<
        std::mutex> lock(modify_mutex_);
    auto list1 = ledger.txList();
    auto list2 = current_->txList();
    std::sort(list1.begin(), list1.end());
    std::sort(list2.begin(), list2.end());
    if (list1 == list2)
        return true;
    JLOG(j_.error) <<
        "verify ledger " << ledger.seq() << ": " <<
        list1.size() << " / " << list2.size() << 
            " " << " MISMATCH " << suffix;
    return false;
}

//------------------------------------------------------------------------------

std::string
debugTxstr (std::shared_ptr<STTx const> const& tx)
{
    std::stringstream ss;
    ss << tx->getTransactionID();
    return ss.str().substr(0, 4);
}

std::string
debugTostr (OrderedTxs const& set)
{
    std::stringstream ss;
    for(auto const& item : set)
        ss << debugTxstr(item.second) << ", ";
    return ss.str();
}

std::string
debugTostr (SHAMap const& set)
{
    std::stringstream ss;
    for (auto const& item : set)
    {
        try
        {
            SerialIter sit(item->slice());
            auto const tx = std::make_shared<
                STTx const>(sit);
            ss << debugTxstr(tx) << ", ";
        }
        catch(...)
        {
            ss << "THRO, ";
        }
    }
    return ss.str();
}

std::string
debugTostr (std::shared_ptr<BasicView const> const& view)
{
    std::stringstream ss;
    for(auto const& item : view->txs)
        ss << debugTxstr(item.first) << ", ";
    return ss.str();
}

} // ripple
