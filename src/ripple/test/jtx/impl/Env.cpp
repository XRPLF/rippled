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
#include <ripple/test/jtx/balance.h>
#include <ripple/test/jtx/Env.h>
#include <ripple/test/jtx/fee.h>
#include <ripple/test/jtx/flags.h>
#include <ripple/test/jtx/pay.h>
#include <ripple/test/jtx/trust.h>
#include <ripple/test/jtx/require.h>
#include <ripple/test/jtx/seq.h>
#include <ripple/test/jtx/sig.h>
#include <ripple/test/jtx/utility.h>
#include <ripple/app/tx/apply.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/basics/Slice.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/types.h>
#include <memory>

namespace ripple {
namespace test {

namespace jtx {

// VFALCO Could wrap the log in a Journal here
Env::Env (beast::unit_test::suite& test_)
    : test(test_)
    , master("master", generateKeyPair(
        KeyType::secp256k1,
            generateSeed("masterpassphrase")))
    , closed_ (std::make_shared<Ledger>(
        create_genesis, config, app().family()))
    , cachedSLEs_ (std::chrono::seconds(5), stopwatch_)
    , openLedger (closed_, config, cachedSLEs_, journal)
{
    memoize(master);
    initializePathfinding();
}

std::shared_ptr<ReadView const>
Env::open() const
{
    return openLedger.current();
}

std::shared_ptr<ReadView const>
Env::closed() const
{
    return closed_;
}

void
Env::close(NetClock::time_point const& closeTime)
{
    clock.set(closeTime);
    // VFALCO TODO Fix the Ledger constructor
    auto next = std::make_shared<Ledger>(
        open_ledger, *closed_);
    next->setClosed();
#if 0
    // Build a SHAMap, put all the transactions
    // in it, and calculate the hash of the SHAMap
    // to construct the OrderedTxs
    SHAMap sm;
    OrderedTxs txs(sm.getRootHash());
    ...
#else
    std::vector<std::shared_ptr<
        STTx const>> txs;
#endif
    auto cur = openLedger.current();
    for (auto iter = cur->txs.begin();
            iter != cur->txs.end(); ++iter)
        txs.push_back(iter->first);
    auto router = std::make_unique<HashRouter>(60);
    OrderedTxs retries(uint256{});
    {
        OpenView accum(&*next);
        OpenLedger::apply(app(), accum, *closed_,
            txs, retries, applyFlags(), *router,
                config, journal);
        accum.apply(*next);
    }
    // To ensure that the close time is exact and not rounded, we don't
    // claim to have reached consensus on what it should be.
    next->setAccepted (
        std::chrono::duration_cast<std::chrono::seconds> (
            closeTime.time_since_epoch ()).count (),
        ledgerPossibleTimeResolutions[0], false);
    OrderedTxs locals({});
    openLedger.accept(app(), next->rules(), next,
        locals, false, retries, applyFlags(), *router);
    closed_ = next;
    cachedSLEs_.expire();
}

void
Env::memoize (Account const& account)
{
    map_.emplace(account.id(), account);
}

Account const&
Env::lookup (AccountID const& id) const
{
    auto const iter = map_.find(id);
    if (iter == map_.end())
        throw std::runtime_error(
            "Env::lookup:: unknown account ID");
    return iter->second;
}

Account const&
Env::lookup (std::string const& base58ID) const
{
    auto const account =
        parseBase58<AccountID>(base58ID);
    if (! account)
        throw std::runtime_error(
            "Env::lookup: invalid account ID");
    return lookup(*account);
}

PrettyAmount
Env::balance (Account const& account) const
{
    auto const sle = le(account);
    if (! sle)
        return XRP(0);
    return {
        sle->getFieldAmount(sfBalance),
            "" };
}

PrettyAmount
Env::balance (Account const& account,
    Issue const& issue) const
{
    if (isXRP(issue.currency))
        return balance(account);
    auto const sle = le(keylet::line(
        account.id(), issue));
    if (! sle)
        return { STAmount( issue, 0 ),
            account.name() };
    auto amount = sle->getFieldAmount(sfBalance);
    amount.setIssuer(issue.account);
    if (account.id() > issue.account)
        amount.negate();
    return { amount,
        lookup(issue.account).name() };
}

std::uint32_t
Env::seq (Account const& account) const
{
    auto const sle = le(account);
    if (! sle)
        throw std::runtime_error(
            "missing account root");
    return sle->getFieldU32(sfSequence);
}

std::shared_ptr<SLE const>
Env::le (Account const& account) const
{
    return le(keylet::account(account.id()));
}

std::shared_ptr<SLE const>
Env::le (Keylet const& k) const
{
    return open()->read(k);
}

void
Env::fund (bool setDefaultRipple,
    STAmount const& amount,
        Account const& account)
{
    memoize(account);
    if (setDefaultRipple)
    {
        // VFALCO NOTE Is the fee formula correct?
        apply(pay(master, account, amount +
            drops(open()->fees().base)),
                jtx::seq(jtx::autofill),
                    fee(jtx::autofill),
                        sig(jtx::autofill));
        apply(fset(account, asfDefaultRipple),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
        require(flags(account, asfDefaultRipple));
    }
    else
    {
        apply(pay(master, account, amount),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
        require(nflags(account, asfDefaultRipple));
    }
    require(jtx::balance(account, amount));
}

void
Env::trust (STAmount const& amount,
    Account const& account)
{
    auto const start = balance(account);
    apply(jtx::trust(account, amount),
        jtx::seq(jtx::autofill),
            fee(jtx::autofill),
                sig(jtx::autofill));
    apply(pay(master, account,
        drops(open()->fees().base)),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
    test.expect(balance(account) == start);
}

void
Env::submit (JTx const& jt)
{
    TER ter;
    bool didApply;
    auto const& stx = jt.stx;
    if (stx)
    {
        txid_ = stx->getTransactionID();
        openLedger.modify(
            [&](OpenView& view, beast::Journal j)
            {
                std::tie(ter, didApply) = ripple::apply(
                    app(), view, *stx, applyFlags(),
                        directSigVerify, config,
                            beast::Journal{});
                return didApply;
            });
    }
    else
    {
        // Parsing failed or the JTx is
        // otherwise missing the stx field.
        ter = temMALFORMED;
        didApply = false;
    }
    if (! test.expect(ter == jt.ter,
        "apply: " + transToken(ter) +
            " (" + transHuman(ter) + ")"))
    {
        test.log << pretty(jt.jv);
        // Don't check postconditions if
        // we didn't get the expected result.
        return;
    }
    if (trace_)
    {
        if (trace_ > 0)
            --trace_;
        test.log << pretty(jt.jv);
    }
    for (auto const& f : jt.requires)
        f(*this);
    //if (isTerRetry(ter))
    //if (! isTesSuccess(ter))
    //    held.insert(stx);
}

std::shared_ptr<STObject const>
Env::meta()
{
    close();
    auto const item = closed()->txRead(txid_);
    return item.second;
}

void
Env::autofill_sig (JTx& jt)
{
    auto& jv = jt.jv;
    if (jt.signer)
        jt.signer(*this, jt);
    else if(jt.fill_sig)
    {
        auto const account =
            lookup(jv[jss::Account].asString());
        auto const ar = le(account);
        if (ar && ar->isFieldPresent(sfRegularKey))
            jtx::sign(jv, lookup(
                ar->getAccountID(sfRegularKey)));
        else
            jtx::sign(jv, account);
    }
}

void
Env::autofill (JTx& jt)
{
    auto& jv = jt.jv;
    if(jt.fill_fee)
        jtx::fill_fee(jv, *open());
    if(jt.fill_seq)
        jtx::fill_seq(jv, *open());
    // Must come last
    try
    {
        autofill_sig(jt);
    }
    catch (parse_error const&)
    {
        test.log <<
            "parse failed:\n" <<
            pretty(jv);
        throw;
    }
}

std::shared_ptr<STTx const>
Env::st (JTx const& jt)
{
    // The parse must succeed, since we
    // generated the JSON ourselves.
    boost::optional<STObject> obj;
    try
    {
        obj = jtx::parse(jt.jv);
    }
    catch(jtx::parse_error const&)
    {
        test.log <<
            "Exception: parse_error\n" <<
            pretty(jt.jv);
        throw;
    }

    try
    {
        return std::make_shared<STTx>(
            std::move(*obj));
    }
    catch(...)
    {
    }
    return nullptr;
}

ApplyFlags
Env::applyFlags() const
{
    if (testing_)
        return tapENABLE_TESTING;
    return tapNONE;
}

} // jtx

} // test
} // ripple
