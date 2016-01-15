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
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/ledger/LedgerTiming.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Slice.h>
#include <ripple/core/ConfigSections.h>
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
#include <ripple/protocol/Feature.h>
#include <memory>

namespace ripple {
namespace test {

void
setupConfigForUnitTests (Config& config)
{
    config.overwrite (ConfigSection::nodeDatabase (), "type", "memory");
    config.overwrite (ConfigSection::nodeDatabase (), "path", "main");

    config.deprecatedClearSection (ConfigSection::importNodeDatabase ());
    config.legacy("database_path", "");

    config.RUN_STANDALONE = true;
    config["server"].append("port_peer");
    config["port_peer"].set("ip", "127.0.0.1");
    config["port_peer"].set("port", "8080");
    config["port_peer"].set("protocol", "peer");
    config["server"].append("port_admin");
    config["port_admin"].set("ip", "127.0.0.1");
    config["port_admin"].set("port", "8081");
    config["port_admin"].set("protocol", "http");
    config["port_admin"].set("admin", "127.0.0.1");
}

//------------------------------------------------------------------------------

namespace jtx {

Env::AppBundle::AppBundle(Application* app_)
    : app (app_)
{
}

Env::AppBundle::AppBundle(std::unique_ptr<Config> config)
{
    auto logs = std::make_unique<Logs>();
    auto timeKeeper_ =
        std::make_unique<ManualTimeKeeper>();
    timeKeeper = timeKeeper_.get();
    owned = make_Application(std::move(config),
        std::move(logs), std::move(timeKeeper_));
    app = owned.get();
    app->setup();
    timeKeeper->set(
        app->getLedgerMaster().getClosedLedger()->info().closeTime);
    thread = std::thread(
        [&](){ app->run(); });
}

Env::AppBundle::~AppBundle()
{
    app->signalStop();
    thread.join();
}

//------------------------------------------------------------------------------

std::shared_ptr<ReadView const>
Env::closed()
{
    return app().getLedgerMaster().getClosedLedger();
}

void
Env::close(NetClock::time_point closeTime,
    boost::optional<std::chrono::milliseconds> consensusDelay)
{
    // Round up to next distinguishable value
    closeTime += closed()->info().closeTimeResolution - 1s;
    bundle_.timeKeeper->set(closeTime);
    app().getOPs().acceptLedger(consensusDelay);
    bundle_.timeKeeper->set(
        closed()->info().closeTime);
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
        Throw<std::runtime_error> (
            "Env::lookup:: unknown account ID");
    return iter->second;
}

Account const&
Env::lookup (std::string const& base58ID) const
{
    auto const account =
        parseBase58<AccountID>(base58ID);
    if (! account)
        Throw<std::runtime_error>(
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
        Throw<std::runtime_error> (
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
    return current()->read(k);
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
            drops(current()->fees().base)),
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
        drops(current()->fees().base)),
            jtx::seq(jtx::autofill),
                fee(jtx::autofill),
                    sig(jtx::autofill));
    test.expect(balance(account) == start);
}

void
Env::submit (JTx const& jt)
{
    bool didApply;
    if (jt.stx)
    {
        txid_ = jt.stx->getTransactionID();
        app().openLedger().modify(
            [&](OpenView& view, beast::Journal j)
            {
                std::tie(ter_, didApply) = app().getTxQ().apply(
                    app(), view, jt.stx, applyFlags(),
                        beast::Journal{});
                return didApply;
            });
    }
    else
    {
        // Parsing failed or the JTx is
        // otherwise missing the stx field.
        ter_ = temMALFORMED;
        didApply = false;
    }
    return postconditions(jt, ter_, didApply);
}

void
Env::postconditions(JTx const& jt, TER ter, bool didApply)
{
    if (jt.ter && ! test.expect(ter == *jt.ter,
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
        return jt.signer(*this, jt);
    if (! jt.fill_sig)
        return;
    auto const account =
        lookup(jv[jss::Account].asString());
    if (nosig_)
    {
        jv[jss::SigningPubKey] =
            strHex(account.pk().slice());
        // dummy sig otherwise STTx is invalid
        jv[jss::TxnSignature] = "00";
        return;
    }
    auto const ar = le(account);
    if (ar && ar->isFieldPresent(sfRegularKey))
        jtx::sign(jv, lookup(
            ar->getAccountID(sfRegularKey)));
    else
        jtx::sign(jv, account);
}

void
Env::autofill (JTx& jt)
{
    auto& jv = jt.jv;
    if(jt.fill_fee)
        jtx::fill_fee(jv, *current());
    if(jt.fill_seq)
        jtx::fill_seq(jv, *current());
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
        Throw();
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
        Throw();
    }

    try
    {
        return sterilize(STTx{std::move(*obj)});
    }
    catch(std::exception const&)
    {
    }
    return nullptr;
}

ApplyFlags
Env::applyFlags() const
{
    ApplyFlags flags = tapNONE;
    if (nosig_)
        flags = flags | tapNO_CHECK_SIGN;
    return flags;
}

} // jtx

} // test
} // ripple
