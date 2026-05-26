#include <test/jtx/Env.h>

#include <test/jtx/Account.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx/JTx.h>
#include <test/jtx/ManualTimeKeeper.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/RPCCall.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/scope.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/NetworkIDService.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>
#include <xrpl/net/HTTPClient.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

//------------------------------------------------------------------------------

Env::AppBundle::AppBundle(
    beast::unit_test::Suite& suite,
    std::unique_ptr<Config> config,
    std::unique_ptr<Logs> logs,
    beast::Severity thresh)
    : AppBundle()
{
    using beast::Severity;
    if (logs)
    {
        setDebugLogSink(logs->makeSink("Debug", Severity::Fatal));
    }
    else
    {
        logs = std::make_unique<SuiteLogs>(suite);
        // Use kFatal threshold to reduce noise from STObject.
        setDebugLogSink(std::make_unique<SuiteJournalSink>("Debug", Severity::Fatal, suite));
    }
    auto tk = std::make_unique<ManualTimeKeeper>();
    timeKeeper = tk.get();
    // Hack so we don't have to call Config::setup
    HTTPClient::initializeSSLContext(
        config->sslVerifyDir, config->sslVerifyFile, config->sslVerify, debugLog());
    owned = makeApplication(std::move(config), std::move(logs), std::move(tk));
    app = owned.get();
    app->getLogs().threshold(thresh);
    if (!app->setup({}))
        Throw<std::runtime_error>("Env::AppBundle: setup failed");
    timeKeeper->set(app->getLedgerMaster().getClosedLedger()->header().closeTime);
    app->start(false /*don't start timers*/);
    thread = std::thread([&]() { app->run(); });

    client = makeJSONRPCClient(app->config());
}

Env::AppBundle::~AppBundle()
{
    client.reset();
    // Make sure all jobs finish, otherwise tests
    // might not get the coverage they expect.
    if (app != nullptr)
    {
        app->getJobQueue().rendezvous();
        app->signalStop("~AppBundle");
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
Env::close(NetClock::time_point closeTime, std::optional<std::chrono::milliseconds> consensusDelay)
{
    // Round up to next distinguishable value
    using namespace std::chrono_literals;
    bool res = true;
    closeTime += closed()->header().closeTimeResolution - 1s;
    timeKeeper().set(closeTime);
    // Go through the rpc interface unless we need to simulate
    // a specific consensus delay.
    if (consensusDelay)
    {
        app().getOPs().acceptLedger(consensusDelay);
    }
    else
    {
        auto resp = rpc("ledger_accept");
        if (resp["result"]["status"] != std::string("success"))
        {
            std::string reason = "internal error";
            if (resp.isMember("error_what"))
            {
                reason = resp["error_what"].asString();
            }
            else if (resp.isMember("error_message"))
            {
                reason = resp["error_message"].asString();
            }
            else if (resp.isMember("error"))
            {
                reason = resp["error"].asString();
            }

            JLOG(journal.error()) << "Env::close() failed: " << reason;
            res = false;
        }
    }
    timeKeeper().set(closed()->header().closeTime);
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
Env::balance(Account const& account, Asset const& asset) const
{
    return asset.visit(
        [&](Issue const& issue) -> PrettyAmount {
            if (isXRP(issue.currency))
                return balance(account);
            auto const sle = le(keylet::line(account.id(), issue));
            if (!sle)
                return {STAmount(issue, 0), account.name()};
            auto amount = sle->getFieldAmount(sfBalance);
            amount.get<Issue>().account = issue.account;
            if (account.id() > issue.account)
                amount.negate();
            return {amount, lookup(issue.account).name()};
        },
        [&](MPTIssue const& mptIssue) -> PrettyAmount {
            MPTID const& id = mptIssue.getMptID();
            if (!id)
                return {STAmount(mptIssue, 0), account.name()};

            AccountID const& issuer = mptIssue.getIssuer();
            if (account.id() == issuer)
            {
                // Issuer balance
                auto const sle = le(keylet::mptIssuance(id));
                if (!sle)
                    return {STAmount(mptIssue, 0), account.name()};

                // Make it negative
                STAmount const amount{mptIssue, sle->getFieldU64(sfOutstandingAmount), 0, true};
                return {amount, lookup(issuer).name()};
            }

            // Holder balance
            auto const sle = le(keylet::mptoken(id, account));
            if (!sle)
                return {STAmount(mptIssue, 0), account.name()};

            STAmount const amount{mptIssue, sle->getFieldU64(sfMPTAmount)};
            return {amount, lookup(issuer).name()};
        });
}

PrettyAmount
Env::limit(Account const& account, Issue const& issue) const
{
    auto const sle = le(keylet::line(account.id(), issue));
    if (!sle)
        return {STAmount(issue, 0), account.name()};
    auto const aHigh = account.id() > issue.account;
    if (sle && sle->isFieldPresent(aHigh ? sfLowLimit : sfHighLimit))
        return {(*sle)[aHigh ? sfLowLimit : sfHighLimit], account.name()};
    return {STAmount(issue, 0), account.name()};
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
            jtx::Seq(jtx::kAutofill),
            Fee(jtx::kAutofill),
            Sig(jtx::kAutofill));
        apply(
            fset(account, asfDefaultRipple),
            jtx::Seq(jtx::kAutofill),
            Fee(jtx::kAutofill),
            Sig(jtx::kAutofill));
        require(Flags(account, asfDefaultRipple));
    }
    else
    {
        apply(
            pay(master, account, amount),
            jtx::Seq(jtx::kAutofill),
            Fee(jtx::kAutofill),
            Sig(jtx::kAutofill));
        require(Nflags(account, asfDefaultRipple));
    }
    require(jtx::Balance(account, amount));
}

void
Env::trust(STAmount const& amount, Account const& account)
{
    if (!amount.holds<Issue>())
        Throw<std::runtime_error>("Env::trust: amount doesn't hold Issue");
    auto const start = balance(account);
    apply(
        jtx::trust(account, amount),
        jtx::Seq(jtx::kAutofill),
        Fee(jtx::kAutofill),
        Sig(jtx::kAutofill));
    apply(
        pay(master, account, drops(current()->fees().base)),
        jtx::Seq(jtx::kAutofill),
        Fee(jtx::kAutofill),
        Sig(jtx::kAutofill));
    test.expect(balance(account) == start);
}

Env::ParsedResult
Env::parseResult(json::Value const& jr)
{
    auto error = [](ParsedResult& parsed, json::Value const& object) {
        // Use an error code that is not used anywhere in the transaction
        // engine to distinguish this case.
        parsed.ter = telENV_RPC_FAILED;
        // Extract information about the error
        if (!object.isObject())
            return;
        if (object.isMember(jss::error_code))
            parsed.rpcCode = safeCast<ErrorCodeI>(object[jss::error_code].asInt());
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
            parsed.rpcCode.emplace(RpcSuccess);
        }
        else
        {
            error(parsed, result);
        }
    }
    else
    {
        error(parsed, jr);
    }

    return parsed;
}

void
Env::submit(JTx const& jt, std::source_location const& loc)
{
    ParsedResult parsedResult;
    auto const jr = [&]() {
        if (jt.stx)
        {
            txid_ = jt.stx->getTransactionID();
            Serializer s;
            jt.stx->add(s);
            auto const jr = rpc("submit", strHex(s.slice()));

            parsedResult = parseResult(jr);
            test.expect(parsedResult.ter, "ter uninitialized!");
            ter_ = parsedResult.ter.value_or(telENV_RPC_FAILED);

            return jr;
        }

        // Parsing failed or the JTx is
        // otherwise missing the stx field.
        parsedResult.ter = ter_ = temMALFORMED;

        return json::Value();
    }();
    postconditions(jt, parsedResult, jr, loc);
}

void
Env::signAndSubmit(JTx const& jt, json::Value params, std::source_location const& loc)
{
    auto const account = lookup(jt.jv[jss::Account].asString());
    auto const& passphrase = account.name();

    json::Value jr;
    if (params.isNull())
    {
        // Use the command line interface
        auto const jv = to_string(jt.jv);
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

    postconditions(jt, parsedResult, jr, loc);
}

void
Env::postconditions(
    JTx const& jt,
    ParsedResult const& parsed,
    json::Value const& jr,
    std::source_location const& loc)
{
    auto const locStr = std::string("(") + loc.file_name() + ":" + to_string(loc.line()) + ")";
    bool bad = !test.expect(parsed.ter, "apply " + locStr + ": No ter result!");
    bad =
        (jt.ter && parsed.ter &&
         !test.expect(
             *parsed.ter == *jt.ter,
             "apply " + locStr + ": Got " + transToken(*parsed.ter) + " (" +
                 transHuman(*parsed.ter) + "); Expected " + transToken(*jt.ter) + " (" +
                 transHuman(*jt.ter) + ")"));
    using namespace std::string_literals;
    bad =
        (jt.rpcCode &&
         !test.expect(
             parsed.rpcCode == jt.rpcCode->first && parsed.rpcMessage == jt.rpcCode->second,
             "apply " + locStr + ": Got RPC result "s +
                 (parsed.rpcCode ? RPC::getErrorInfo(*parsed.rpcCode).token.cStr() : "NO RESULT") +
                 " (" + parsed.rpcMessage + "); Expected " +
                 RPC::getErrorInfo(jt.rpcCode->first).token.cStr() + " (" + jt.rpcCode->second +
                 ")")) ||
        bad;
    // If we have an rpcCode (just checked), then the rpcException check is
    // optional - the 'error' field may not be defined, but if it is, it must
    // match rpcError.
    bad = (jt.rpcException &&
           !test.expect(
               (jt.rpcCode && parsed.rpcError.empty()) ||
                   (parsed.rpcError == jt.rpcException->first &&
                    (!jt.rpcException->second || parsed.rpcException == *jt.rpcException->second)),
               "apply " + locStr + ": Got RPC result "s + parsed.rpcError + " (" +
                   parsed.rpcException + "); Expected " + jt.rpcException->first + " (" +
                   jt.rpcException->second.value_or("n/a") + ")")) ||
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
    if (trace_ != 0)
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
    if (current()->txCount() != 0)
    {
        // close the ledger if it has not already been closed
        // (metadata is not finalized until the ledger is closed)
        close();
    }
    auto const item = closed()->txRead(txid_);
    auto const result = item.second;
    if (result == nullptr)
    {
        test.log << "Env::meta: no metadata for txid: " << txid_ << std::endl;
        test.log << "This is probably because the transaction failed with a "
                    "non-tec error."
                 << std::endl;
        Throw<std::runtime_error>("Env::meta: no metadata for txid");
    }
    return result;
}

std::shared_ptr<STTx const>
Env::tx() const
{
    return current()->txRead(txid_).first;
}

void
Env::autofillSig(JTx& jt)
{
    auto& jv = jt.jv;

    ScopeSuccess const success([&]() {
        // Call all the post-signers after the main signers or autofill are done
        for (auto const& signer : jt.postSigners)
            signer(*this, jt);
    });

    // Call all the main signers
    if (!jt.mainSigners.empty())
    {
        for (auto const& signer : jt.mainSigners)
            signer(*this, jt);
        return;
    }

    // If the sig is still needed, get it here.
    if (!jt.fillSig)
        return;
    auto const account = jv.isMember(sfDelegate.jsonName)
        ? lookup(jv[sfDelegate.jsonName].asString())
        : lookup(jv[jss::Account].asString());
    if (!app().checkSigs())
    {
        jv[jss::SigningPubKey] = strHex(account.pk().slice());
        // dummy sig otherwise STTx is invalid
        jv[jss::TxnSignature] = "00";
        return;
    }
    auto const ar = le(account);
    if (ar && ar->isFieldPresent(sfRegularKey))
    {
        jtx::sign(jv, lookup(ar->getAccountID(sfRegularKey)));
    }
    else
    {
        jtx::sign(jv, account);
    }
}

void
Env::autofill(JTx& jt)
{
    auto& jv = jt.jv;
    if (jt.fillFee)
        jtx::fillFee(jv, *current());
    if (jt.fillSeq)
        jtx::fillSeq(jv, *current());

    if (jt.fillNetid)
    {
        uint32_t const networkID = app().getNetworkIDService().getNetworkID();
        if (!jv.isMember(jss::NetworkID) && networkID > 1024)
            jv[jss::NetworkID] = std::to_string(networkID);
    }

    // Must come last
    try
    {
        autofillSig(jt);
    }
    catch (ParseError const&)
    {
        if (!parseFailureExpected_)
            test.log << "parse failed:\n" << pretty(jv) << std::endl;
        rethrow();
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
    catch (jtx::ParseError const&)
    {
        test.log << "Exception: ParseError\n" << pretty(jt.jv) << std::endl;
        rethrow();
    }

    try
    {
        return sterilize(STTx{std::move(*obj)});
    }
    catch (...)
    {
        return nullptr;
    }
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
    catch (jtx::ParseError const&)
    {
        test.log << "Exception: ParseError\n" << pretty(jt.jv) << std::endl;
        rethrow();
    }

    try
    {
        return std::make_shared<STTx const>(std::move(*obj));
    }
    catch (...)
    {
        return nullptr;
    }
}

json::Value
Env::doRpc(
    unsigned apiVersion,
    std::vector<std::string> const& args,
    std::unordered_map<std::string, std::string> const& headers)
{
    auto response = rpcClient(args, app().config(), app().getLogs(), apiVersion, headers);

    for (unsigned ctr = 0; (ctr < retries_) and (response.first == RpcInternal); ++ctr)
    {
        JLOG(journal.error()) << "Env::doRpc error, retrying, attempt #" << ctr + 1 << " ...";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        response = rpcClient(args, app().config(), app().getLogs(), apiVersion, headers);
    }

    return response.second;
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

}  // namespace xrpl::test::jtx
