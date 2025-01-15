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

#include <test/jtx/Env.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/require.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/consensus/LedgerTiming.h>
#include <xrpld/net/HTTPClient.h>
#include <xrpld/net/RPCCall.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <memory>

namespace ripple {
namespace test {
namespace jtx {

//------------------------------------------------------------------------------

Env::AppBundle::AppBundle(
    beast::unit_test::suite& suite,
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    beast::severities::Severity thresh)
    : AppBundle()
{
    using namespace beast::severities;
    if (logs)
    {
        setDebugLogSink(logs->makeSink("Debug", kFatal));
    }
    else
    {
        logs = std::make_unique<SuiteLogs>(suite);
        // Use kFatal threshold to reduce noise from STObject.
        setDebugLogSink(
            std::make_unique<SuiteJournalSink>("Debug", kFatal, suite));
    }
    auto timeKeeper_ = std::make_unique<ManualTimeKeeper>();
    timeKeeper = timeKeeper_.get();
    // Hack so we don't have to call Config::setup
    HTTPClient::initializeSSLContext(*config, debugLog());
    owned = make_Application(
        std::move(config), std::move(logs), std::move(timeKeeper_));
    app = owned.get();
    app->logs().threshold(thresh);
    if (!app->setup({}))
        Throw<std::runtime_error>("Env::AppBundle: setup failed");
    timeKeeper->set(app->getLedgerMaster().getClosedLedger()->info().closeTime);
    app->start(false /*don't start timers*/);
    thread = std::thread([&]() { app->run(); });

    client = makeJSONRPCClient(app->config());
}

Env::AppBundle::~AppBundle()
{
    client.reset();
    // Make sure all jobs finish, otherwise tests
    // might not get the coverage they expect.
    if (app)
    {
        app->getJobQueue().rendezvous();
        app->signalStop();
    }
    if (thread.joinable())
        thread.join();

    // Remove the debugLogSink before the suite goes out of scope.
    setDebugLogSink(nullptr);
}

//------------------------------------------------------------------------------

std::shared_ptr<ReadView const>
Env::closed()
{
    return app().getLedgerMaster().getClosedLedger();
}

bool
Env::close(
    NetClock::time_point closeTime,
    std::optional<std::chrono::milliseconds> consensusDelay)
{
    // Round up to next distinguishable value
    using namespace std::chrono_literals;
    bool res = true;
    closeTime += closed()->info().closeTimeResolution - 1s;
    timeKeeper().set(closeTime);
    // Go through the rpc interface unless we need to simulate
    // a specific consensus delay.
    if (consensusDelay)
        app().getOPs().acceptLedger(consensusDelay);
    else
    {
        auto resp = rpc("ledger_accept");
        if (resp["result"]["status"] != std::string("success"))
        {
            std::string reason = "internal error";
            if (resp.isMember("error_what"))
                reason = resp["error_what"].asString();
            else if (resp.isMember("error_message"))
                reason = resp["error_message"].asString();
            else if (resp.isMember("error"))
                reason = resp["error"].asString();

            JLOG(journal.error()) << "Env::close() failed: " << reason;
            res = false;
        }
    }
    timeKeeper().set(closed()->info().closeTime);
    return res;
}

void
Env::memoize(Account const& account)
{
    map_.emplace(account.id(), account);
}

Account const&
Env::lookup(AccountID const& id) const
{
    auto const iter = map_.find(id);
    if (iter == map_.end())
    {
        std::cout << "Unknown account: " << id << "\n";
        Throw<std::runtime_error>("Env::lookup:: unknown account ID");
    }
    return iter->second;
}

Account const&
Env::lookup(std::string const& base58ID) const
{
    auto const account = parseBase58<AccountID>(base58ID);
    if (!account)
        Throw<std::runtime_error>("Env::lookup: invalid account ID");
    return lookup(*account);
}

PrettyAmount
Env::balance(Account const& account) const
{
    auto const sle = le(account);
    if (!sle)
        return XRP(0);
    return {sle->getFieldAmount(sfBalance), ""};
}

PrettyAmount
Env::balance(Account const& account, Issue const& issue) const
{
    if (isXRP(issue.currency))
        return balance(account);
    auto const sle = le(keylet::line(account.id(), issue));
    if (!sle)
        return {STAmount(issue, 0), account.name()};
    auto amount = sle->getFieldAmount(sfBalance);
    amount.setIssuer(issue.account);
    if (account.id() > issue.account)
        amount.negate();
    return {amount, lookup(issue.account).name()};
}

std::uint32_t
Env::ownerCount(Account const& account) const
{
    auto const sle = le(account);
    if (!sle)
        Throw<std::runtime_error>("missing account root");
    return sle->getFieldU32(sfOwnerCount);
}

std::uint32_t
Env::seq(Account const& account) const
{
    auto const sle = le(account);
    if (!sle)
        Throw<std::runtime_error>("missing account root");
    return sle->getFieldU32(sfSequence);
}

std::shared_ptr<SLE const>
Env::le(Account const& account) const
{
    return le(keylet::account(account.id()));
}

std::shared_ptr<SLE const>
Env::le(Keylet const& k) const
{
    return current()->read(k);
}

void
Env::fund(bool setDefaultRipple, STAmount const& amount, Account const& account)
{
    memoize(account);
    if (setDefaultRipple)
    {
        // VFALCO NOTE Is the fee formula correct?
        apply(
            pay(master, account, amount + drops(current()->fees().base)),
            jtx::seq(jtx::autofill),
            fee(jtx::autofill),
            sig(jtx::autofill));
        apply(
            fset(account, asfDefaultRipple),
            jtx::seq(jtx::autofill),
            fee(jtx::autofill),
            sig(jtx::autofill));
        require(flags(account, asfDefaultRipple));
    }
    else
    {
        apply(
            pay(master, account, amount),
            jtx::seq(jtx::autofill),
            fee(jtx::autofill),
            sig(jtx::autofill));
        require(nflags(account, asfDefaultRipple));
    }
    require(jtx::balance(account, amount));
}

void
Env::trust(STAmount const& amount, Account const& account)
{
    auto const start = balance(account);
    apply(
        jtx::trust(account, amount),
        jtx::seq(jtx::autofill),
        fee(jtx::autofill),
        sig(jtx::autofill));
    apply(
        pay(master, account, drops(current()->fees().base)),
        jtx::seq(jtx::autofill),
        fee(jtx::autofill),
        sig(jtx::autofill));
    test.expect(balance(account) == start);
}

Env::ParsedResult
Env::parseResult(Json::Value const& jr)
{
    auto error = [](ParsedResult& parsed, Json::Value const& object) {
        // Use an error code that is not used anywhere in the transaction
        // engine to distinguish this case.
        parsed.ter = telENV_RPC_FAILED;
        // Extract information about the error
        if (!object.isObject())
            return;
        if (object.isMember(jss::error_code))
            parsed.rpcCode =
                safe_cast<error_code_i>(object[jss::error_code].asInt());
        if (object.isMember(jss::error_message))
            parsed.rpcMessage = object[jss::error_message].asString();
        if (object.isMember(jss::error))
            parsed.rpcError = object[jss::error].asString();
        if (object.isMember(jss::error_exception))
            parsed.rpcException = object[jss::error_exception].asString();
    };
    ParsedResult parsed;
    if (jr.isObject() && jr.isMember(jss::result))
    {
        auto const& result = jr[jss::result];
        if (result.isMember(jss::engine_result_code))
        {
            parsed.ter = TER::fromInt(result[jss::engine_result_code].asInt());
            parsed.rpcCode.emplace(rpcSUCCESS);
        }
        else if (
            !result.isMember(jss::error) && !result.isMember(jss::error_code) &&
            !result.isMember(jss::error_message) &&
            !result.isMember(jss::error_exception))
            // parsed.ter remains unseated
            parsed.rpcCode.emplace(rpcSUCCESS);
        else
            error(parsed, result);
    }
    else if (
        jr.isObject() && jr.isMember(jss::error) && jr[jss::error].isObject())
        error(parsed, jr[jss::error]);
    else
        error(parsed, jr);

    return parsed;
}

void
Env::submit(JTx const& jt)
{
    ParsedResult parsedResult;
    auto const jr = [&]() {
        if (jt.stx)
        {
            txid_ = jt.stx->getTransactionID();
            Serializer s;
            jt.stx->add(s);
            auto const cb = [&](Json::Value const& jr) {
                parsedResult = parseResult(jr);
                test.expect(parsedResult.ter, "ter uninitialized!");
                ter_ = parsedResult.ter.value_or(telENV_RPC_FAILED);
                return (
                    ter_ != telENV_RPC_FAILED ||
                    parsedResult.rpcCode != rpcINTERNAL ||
                    jt.ter == telENV_RPC_FAILED);
            };
            // rpc() will call cb(), which does all the parsing
            return rpc(cb, "submit", strHex(s.slice()));
        }
        else
        {
            // Parsing failed or the JTx is
            // otherwise missing the stx field.
            parsedResult.ter = ter_ = temMALFORMED;

            return Json::Value();
        }
    }();
    return postconditions(jt, parsedResult, jr);
}

void
Env::sign_and_submit(JTx const& jt, Json::Value params)
{
    auto const account = lookup(jt.jv[jss::Account].asString());
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
        if (!params.isMember(jss::secret) && !params.isMember(jss::key_type) &&
            !params.isMember(jss::seed) && !params.isMember(jss::seed_hex) &&
            !params.isMember(jss::passphrase))
        {
            params[jss::secret] = passphrase;
        }
        params[jss::tx_json] = jt.jv;
        jr = client().invoke("submit", params);
    }

    if (!txid_.parseHex(jr[jss::result][jss::tx_json][jss::hash].asString()))
        txid_.zero();

    ParsedResult const parsedResult = parseResult(jr);
    test.expect(parsedResult.ter, "ter uninitialized!");
    ter_ = parsedResult.ter.value_or(telENV_RPC_FAILED);

    return postconditions(jt, parsedResult, jr);
}

void
Env::postconditions(
    JTx const& jt,
    ParsedResult const& parsed,
    Json::Value const& jr)
{
    bool bad = !test.expect(parsed.ter, "apply: No ter result!");
    bad =
        (jt.ter && parsed.ter &&
         !test.expect(
             *parsed.ter == *jt.ter,
             "apply: Got " + transToken(*parsed.ter) + " (" +
                 transHuman(*parsed.ter) + "); Expected " +
                 transToken(*jt.ter) + " (" + transHuman(*jt.ter) + ")"));
    using namespace std::string_literals;
    bad = (jt.rpcCode &&
           !test.expect(
               parsed.rpcCode == jt.rpcCode->first &&
                   parsed.rpcMessage == jt.rpcCode->second,
               "apply: Got RPC result "s +
                   (parsed.rpcCode
                        ? RPC::get_error_info(*parsed.rpcCode).token.c_str()
                        : "NO RESULT") +
                   " (" + parsed.rpcMessage + "); Expected " +
                   RPC::get_error_info(jt.rpcCode->first).token.c_str() + " (" +
                   jt.rpcCode->second + ")")) ||
        bad;
    // If we have an rpcCode (just checked), then the rpcException check is
    // optional - the 'error' field may not be defined, but if it is, it must
    // match rpcError.
    bad =
        (jt.rpcException &&
         !test.expect(
             (jt.rpcCode && parsed.rpcError.empty()) ||
                 (parsed.rpcError == jt.rpcException->first &&
                  (!jt.rpcException->second ||
                   parsed.rpcException == *jt.rpcException->second)),
             "apply: Got RPC result "s + parsed.rpcError + " (" +
                 parsed.rpcException + "); Expected " + jt.rpcException->first +
                 " (" + jt.rpcException->second.value_or("n/a") + ")")) ||
        bad;
    if (bad)
    {
        test.log << pretty(jt.jv) << std::endl;
        if (jr)
            test.log << pretty(jr) << std::endl;
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
    for (auto const& f : jt.require)
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
Env::autofill_sig(JTx& jt)
{
    auto& jv = jt.jv;
    if (jt.signer)
        return jt.signer(*this, jt);
    if (!jt.fill_sig)
        return;
    auto const account = lookup(jv[jss::Account].asString());
    if (!app().checkSigs())
    {
        jv[jss::SigningPubKey] = strHex(account.pk().slice());
        // dummy sig otherwise STTx is invalid
        jv[jss::TxnSignature] = "00";
        return;
    }
    auto const ar = le(account);
    if (ar && ar->isFieldPresent(sfRegularKey))
        jtx::sign(jv, lookup(ar->getAccountID(sfRegularKey)));
    else
        jtx::sign(jv, account);
}

void
Env::autofill(JTx& jt)
{
    auto& jv = jt.jv;
    if (jt.fill_fee)
        jtx::fill_fee(jv, *current());
    if (jt.fill_seq)
        jtx::fill_seq(jv, *current());

    uint32_t networkID = app().config().NETWORK_ID;
    if (!jv.isMember(jss::NetworkID) && networkID > 1024)
        jv[jss::NetworkID] = std::to_string(networkID);

    // Must come last
    try
    {
        autofill_sig(jt);
    }
    catch (parse_error const&)
    {
        if (!parseFailureExpected_)
            test.log << "parse failed:\n" << pretty(jv) << std::endl;
        Rethrow();
    }
}

std::shared_ptr<STTx const>
Env::st(JTx const& jt)
{
    // The parse must succeed, since we
    // generated the JSON ourselves.
    std::optional<STObject> obj;
    try
    {
        obj = jtx::parse(jt.jv);
    }
    catch (jtx::parse_error const&)
    {
        test.log << "Exception: parse_error\n" << pretty(jt.jv) << std::endl;
        Rethrow();
    }

    try
    {
        return sterilize(STTx{std::move(*obj)});
    }
    catch (std::exception const&)
    {
    }
    return nullptr;
}

std::shared_ptr<STTx const>
Env::ust(JTx const& jt)
{
    // The parse must succeed, since we
    // generated the JSON ourselves.
    std::optional<STObject> obj;
    try
    {
        obj = jtx::parse(jt.jv);
    }
    catch (jtx::parse_error const&)
    {
        test.log << "Exception: parse_error\n" << pretty(jt.jv) << std::endl;
        Rethrow();
    }

    try
    {
        return std::make_shared<STTx const>(std::move(*obj));
    }
    catch (std::exception const&)
    {
    }
    return nullptr;
}

Json::Value
Env::do_rpc(
    RpcCallback cb,
    unsigned apiVersion,
    std::vector<std::string> const& args,
    std::unordered_map<std::string, std::string> const& headers)
{
    // We shouldn't need to retry, but it fixes the test on macOS for
    // the moment.
    if (!test.BEAST_EXPECT(cb))
        cb = rejectInternalError;
    int retries = 3;
    do
    {
        std::string retString;
        try
        {
            auto const ret =
                rpcClient(
                    args, app().config(), app().logs(), apiVersion, headers)
                    .second;
            if (cb(ret) || --retries <= 0)
                return ret;
            test.log << "RPC failure: ";
            retString = to_string(ret);
        }
        catch (std::exception const& e)
        {
            using namespace std::string_literals;
            // TODO: Narrow down the exceptions that can be retried
            if (--retries <= 0)
                throw;
            test.log << "RPC exception: ";
            retString = e.what();
        }
        std::stringstream ss;
        for (auto const& arg : args)
        {
            ss << arg << ", ";
        }
        test.log << ss.str() << " -> " << retString << std::endl;
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);
    } while (true);
}

void
Env::retry(std::function<void()> cb, std::string const& context)
{
    using namespace std::chrono_literals;
    retry(cb, context, &test, 100ms);
}

void
Env::retry(
    std::function<void()> cb,
    std::string const& context,
    std::chrono::milliseconds delay)
{
    retry(cb, context, nullptr, delay);
}

void
Env::retry(
    std::function<void()> cb,
    std::string const& context,
    beast::unit_test::suite* test,
    std::chrono::milliseconds delay)
{
    int retries = 3;
    do
    {
        try
        {
            return cb();
        }
        catch (std::exception const& e)
        {
            if (--retries <= 0)
                throw;
            if (test)
                test->log << "Retry exception(" << context << "): " << e.what()
                          << std::endl;
            // TODO remove
            else
                std::cout << "Retry exception(" << context << "): " << e.what()
                          << std::endl;
            std::this_thread::sleep_for(delay);
        }
    } while (true);
}

void
Env::enableFeature(uint256 const feature)
{
    // Env::close() must be called for feature
    // enable to take place.
    app().config().features.insert(feature);
}

void
Env::disableFeature(uint256 const feature)
{
    // Env::close() must be called for feature
    // enable to take place.
    app().config().features.erase(feature);
}

}  // namespace jtx

}  // namespace test
}  // namespace ripple
