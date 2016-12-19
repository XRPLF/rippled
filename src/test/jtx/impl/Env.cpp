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
#include <test/jtx/balance.h>
#include <test/jtx/Env.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/require.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/utility.h>
#include <test/jtx/JSONRPCClient.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/Slice.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/json/to_string.h>
#include <ripple/net/HTTPClient.h>
#include <ripple/net/RPCCall.h>
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
setupConfigForUnitTests (Config& cfg)
{
    cfg.overwrite (ConfigSection::nodeDatabase (), "type", "memory");
    cfg.overwrite (ConfigSection::nodeDatabase (), "path", "main");
    cfg.deprecatedClearSection (ConfigSection::importNodeDatabase ());
    cfg.legacy("database_path", "");
    cfg.setupControl(true, true, true);
    cfg["server"].append("port_peer");
    cfg["port_peer"].set("ip", "127.0.0.1");
    cfg["port_peer"].set("port", "8080");
    cfg["port_peer"].set("protocol", "peer");
    cfg["server"].append("port_rpc");
    cfg["port_rpc"].set("ip", "127.0.0.1");
    cfg["port_rpc"].set("port", "8081");
    cfg["port_rpc"].set("protocol", "http,ws2");
    cfg["port_rpc"].set("admin", "127.0.0.1");
    cfg["server"].append("port_ws");
    cfg["port_ws"].set("ip", "127.0.0.1");
    cfg["port_ws"].set("port", "8082");
    cfg["port_ws"].set("protocol", "ws");
    cfg["port_ws"].set("admin", "127.0.0.1");
}

//------------------------------------------------------------------------------

namespace jtx {

class SuiteSink : public beast::Journal::Sink
{
    std::string partition_;
    beast::unit_test::suite& suite_;

public:
    SuiteSink(std::string const& partition,
            beast::severities::Severity threshold,
            beast::unit_test::suite& suite)
        : Sink (threshold, false)
        , partition_(partition + " ")
        , suite_ (suite)
    {
    }

    // For unit testing, always generate logging text.
    bool active(beast::severities::Severity level) const override
    {
        return true;
    }

    void
    write(beast::severities::Severity level,
        std::string const& text) override
    {
        using namespace beast::severities;
        std::string s;
        switch(level)
        {
        case kTrace:    s = "TRC:"; break;
        case kDebug:    s = "DBG:"; break;
        case kInfo:     s = "INF:"; break;
        case kWarning:  s = "WRN:"; break;
        case kError:    s = "ERR:"; break;
        default:
        case kFatal:    s = "FTL:"; break;
        }

        // Only write the string if the level at least equals the threshold.
        if (level >= threshold())
            suite_.log << s << partition_ << text << std::endl;
    }
};

class SuiteLogs : public Logs
{
    beast::unit_test::suite& suite_;

public:
    explicit
    SuiteLogs(beast::unit_test::suite& suite)
        : Logs (beast::severities::kError)
        , suite_(suite)
    {
    }

    ~SuiteLogs() override = default;

    std::unique_ptr<beast::Journal::Sink>
    makeSink(std::string const& partition,
        beast::severities::Severity threshold) override
    {
        return std::make_unique<SuiteSink>(partition, threshold, suite_);
    }
};

//------------------------------------------------------------------------------

Env::AppBundle::AppBundle(beast::unit_test::suite& suite,
    std::unique_ptr<Config> config)
{
    using namespace beast::severities;
    // Use kFatal threshold to reduce noise from STObject.
    setDebugLogSink (std::make_unique<SuiteSink>("Debug", kFatal, suite));
    auto logs = std::make_unique<SuiteLogs>(suite);
    auto timeKeeper_ =
        std::make_unique<ManualTimeKeeper>();
    timeKeeper = timeKeeper_.get();
    // Hack so we don't have to call Config::setup
    HTTPClient::initializeSSLContext(*config);
    owned = make_Application(std::move(config),
        std::move(logs), std::move(timeKeeper_));
    app = owned.get();
    app->logs().threshold(kError);
    if(! app->setup())
        Throw<std::runtime_error> ("Env::AppBundle: setup failed");
    timeKeeper->set(
        app->getLedgerMaster().getClosedLedger()->info().closeTime);
    app->doStart();
    thread = std::thread(
        [&](){ app->run(); });

    client = makeJSONRPCClient(app->config());
}

Env::AppBundle::~AppBundle()
{
    client.reset();
    // Make sure all jobs finish, otherwise tests
    // might not get the coverage they expect.
    app->getJobQueue().rendezvous();
    app->signalStop();
    thread.join();

    // Remove the debugLogSink before the suite goes out of scope.
    setDebugLogSink (nullptr);
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
    timeKeeper().set(closeTime);
    // Go through the rpc interface unless we need to simulate
    // a specific consensus delay.
    if (consensusDelay)
        app().getOPs().acceptLedger(consensusDelay);
    else
    {
        rpc("ledger_accept");
        // VFALCO No error check?
    }
    timeKeeper().set(
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

std::pair<TER, bool>
Env::parseResult(Json::Value const& jr)
{
    TER ter;
    if (jr.isObject() && jr.isMember(jss::result) &&
        jr[jss::result].isMember(jss::engine_result_code))
        ter = static_cast<TER>(
            jr[jss::result][jss::engine_result_code].asInt());
    else
        ter = temINVALID;
    return std::make_pair(ter,
        isTesSuccess(ter) || isTecClaim(ter));
}

void
Env::submit (JTx const& jt)
{
    bool didApply;
    if (jt.stx)
    {
        txid_ = jt.stx->getTransactionID();
        Serializer s;
        jt.stx->add(s);
        auto const jr = rpc("submit", strHex(s.slice()));

        std::tie(ter_, didApply) = parseResult(jr);
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
Env::sign_and_submit(JTx const& jt, Json::Value params)
{
    bool didApply;

    auto const account =
        lookup(jt.jv[jss::Account].asString());
    auto const& passphrase = account.name();

    Json::Value jr;
    if (params.isNull())
    {
        // Use the command line interface
        auto const jv = boost::lexical_cast<std::string>(jt.jv);
        jr = rpc("submit", passphrase, jv);
    }
    else
    {
        // Use the provided parameters, and go straight
        // to the (RPC) client.
        assert(params.isObject());
        if (!params.isMember(jss::secret) &&
            !params.isMember(jss::key_type) &&
            !params.isMember(jss::seed) &&
            !params.isMember(jss::seed_hex) &&
            !params.isMember(jss::passphrase))
        {
            params[jss::secret] = passphrase;
        }
        params[jss::tx_json] = jt.jv;
        jr = client().invoke("submit", params);
    }
    txid_.SetHex(
        jr[jss::result][jss::tx_json][jss::hash].asString());

    std::tie(ter_, didApply) = parseResult(jr);

    return postconditions(jt, ter_, didApply);
}

void
Env::postconditions(JTx const& jt, TER ter, bool didApply)
{
    if (jt.ter && ! test.expect(ter == *jt.ter,
        "apply: " + transToken(ter) +
            " (" + transHuman(ter) + ") != " +
                transToken(*jt.ter) + " (" +
                    transHuman(*jt.ter) + ")"))
    {
        test.log << pretty(jt.jv) << std::endl;
        // Don't check postconditions if
        // we didn't get the expected result.
        return;
    }
    if (trace_)
    {
        if (trace_ > 0)
            --trace_;
        test.log << pretty(jt.jv) << std::endl;
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

std::shared_ptr<STTx const>
Env::tx() const
{
    return current()->txRead(txid_).first;
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
    if (!app().checkSigs())
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
        test.log << "parse failed:\n" <<
            pretty(jv) << std::endl;
        Rethrow();
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
        test.log << "Exception: parse_error\n" <<
            pretty(jt.jv) << std::endl;
        Rethrow();
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

Json::Value
Env::do_rpc(std::vector<std::string> const& args)
{
    auto jv = cmdLineToJSONRPC(args, journal);
    if (!jv.isMember(jss::jsonrpc))
    {
        jv[jss::jsonrpc] = "2.0";
        jv[jss::ripplerpc] = "2.0";
        jv[jss::id] = 5;
    }
    auto response = client().invoke(jv[jss::method].asString(), jv[jss::params][0U]);
    if (jv.isMember(jss::jsonrpc))
    {
        response[jss::jsonrpc] = jv[jss::jsonrpc];
        response[jss::ripplerpc] = jv[jss::ripplerpc];
        response[jss::id] = jv[jss::id];
    }
    return response;
}

} // jtx

} // test
} // ripple
