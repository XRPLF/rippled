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

#ifndef RIPPLE_TEST_JTX_ENV_H_INCLUDED
#define RIPPLE_TEST_JTX_ENV_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/JTx.h>
#include <test/jtx/require.h>
#include <test/jtx/tags.h>
#include <test/jtx/AbstractClient.h>
#include <test/jtx/ManualTimeKeeper.h>
#include <test/unit_test/SuiteJournal.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/ledger/OpenLedger.h>
#include <ripple/app/paths/Pathfinder.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/json/json_value.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/CachedSLEs.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <functional>
#include <string>
#include <tuple>
#include <ripple/beast/cxx17/type_traits.h> // <type_traits>
#include <utility>
#include <unordered_map>
#include <vector>


namespace ripple {
namespace test {
namespace jtx {

/** Designate accounts as no-ripple in Env::fund */
template <class... Args>
std::array<Account, 1 + sizeof...(Args)>
noripple (Account const& account, Args const&... args)
{
    return {{account, args...}};
}

inline
FeatureBitset
supported_amendments()
{
    static const FeatureBitset ids = []{
        auto const& sa = ripple::detail::supportedAmendments();
        std::vector<uint256> feats;
        feats.reserve(sa.size());
        for (auto const& s : sa)
        {
            if (auto const f = getRegisteredFeature(s))
                feats.push_back(*f);
            else
                Throw<std::runtime_error> ("Unknown feature: " + s + "  in supportedAmendments.");
        }
        return FeatureBitset(feats);
    }();
    return ids;
}

//------------------------------------------------------------------------------

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
        return std::make_unique<SuiteJournalSink>(
            partition, threshold, suite_);
    }
};

//------------------------------------------------------------------------------

/** A transaction testing environment. */
class Env
{
public:
    beast::unit_test::suite& test;

    Account const& master = Account::master;

private:
    struct AppBundle
    {
        Application* app;
        std::unique_ptr<Application> owned;
        ManualTimeKeeper* timeKeeper;
        std::thread thread;
        std::unique_ptr<AbstractClient> client;

        AppBundle (beast::unit_test::suite& suite,
            std::unique_ptr<Config> config,
            std::unique_ptr<Logs> logs);
        ~AppBundle();
    };

    AppBundle bundle_;

public:
    beast::Journal const journal;

    Env() = delete;
    Env& operator= (Env const&) = delete;
    Env (Env const&) = delete;

    /**
     * @brief Create Env using suite, Config pointer, and explicit features.
     *
     * This constructor will create an Env with the specified configuration
     * and takes ownership the passed Config pointer. Features will be enabled
     * according to rules described below (see next constructor).
     *
     * @param suite_ the current unit_test::suite
     * @param config The desired Config - ownership will be taken by moving
     * the pointer. See envconfig and related functions for common config tweaks.
     * @param args with_only_features() to explicitly enable or
     * supported_features_except() to enable all and disable specific features.
     */
    // VFALCO Could wrap the suite::log in a Journal here
    Env (beast::unit_test::suite& suite_,
            std::unique_ptr<Config> config,
            FeatureBitset features,
            std::unique_ptr<Logs> logs = nullptr)
        : test (suite_)
        , bundle_ (
            suite_,
            std::move(config),
            logs ? std::move(logs) : std::make_unique<SuiteLogs>(suite_))
        , journal {bundle_.app->journal ("Env")}
    {
        memoize(Account::master);
        Pathfinder::initPathTable();
        foreachFeature(
            features, [& appFeats = app().config().features](uint256 const& f) {
                appFeats.insert(f);
            });
    }

    /**
     * @brief Create Env with default config and specified
     * features.
     *
     * This constructor will create an Env with the standard Env configuration
     * (from envconfig()) and features explicitly specified. Use
     * with_only_features(...) or supported_features_except(...) to create a
     * collection of features appropriate for passing here.
     *
     * @param suite_ the current unit_test::suite
     * @param args collection of features
     *
     */
    Env (beast::unit_test::suite& suite_,
            FeatureBitset features)
        : Env(suite_, envconfig(), features)
    {
    }

    /**
     * @brief Create Env using suite and Config pointer.
     *
     * This constructor will create an Env with the specified configuration
     * and takes ownership the passed Config pointer. All supported amendments
     * are enabled by this version of the constructor.
     *
     * @param suite_ the current unit_test::suite
     * @param config The desired Config - ownership will be taken by moving
     * the pointer. See envconfig and related functions for common config tweaks.
     */
    Env (beast::unit_test::suite& suite_,
        std::unique_ptr<Config> config,
        std::unique_ptr<Logs> logs = nullptr)
        : Env(suite_, std::move(config),
            supported_amendments(), std::move(logs))
    {
    }

    /**
     * @brief Create Env with only the current test suite
     *
     * This constructor will create an Env with the standard
     * test Env configuration (from envconfig()) and all supported
     * amendments enabled.
     *
     * @param suite_ the current unit_test::suite
     */
    Env (beast::unit_test::suite& suite_)
        : Env(suite_, envconfig())
    {
    }

    virtual ~Env() = default;

    Application&
    app()
    {
        return *bundle_.app;
    }

    Application const&
    app() const
    {
        return *bundle_.app;
    }

    ManualTimeKeeper&
    timeKeeper()
    {
        return *bundle_.timeKeeper;
    }

    /** Returns the current Ripple Network Time

        @note This is manually advanced when ledgers
              close or by callers.
    */
    NetClock::time_point
    now()
    {
        return timeKeeper().now();
    }

    /** Returns the connected client. */
    AbstractClient&
    client()
    {
        return *bundle_.client;
    }

    /** Execute an RPC command.

        The command is examined and used to build
        the correct JSON as per the arguments.
    */
    template<class... Args>
    Json::Value
    rpc(std::unordered_map<std::string, std::string> const& headers,
        std::string const& cmd,
        Args&&... args);

    template<class... Args>
    Json::Value
    rpc(std::string const& cmd, Args&&... args);

    /** Returns the current ledger.

        This is a non-modifiable snapshot of the
        open ledger at the moment of the call.
        Transactions applied after the call to open()
        will not be visible.

    */
    std::shared_ptr<OpenView const>
    current() const
    {
        return app().openLedger().current();
    }

    /** Returns the last closed ledger.

        The open ledger is built on top of the
        last closed ledger. When the open ledger
        is closed, it becomes the new closed ledger
        and a new open ledger takes its place.
    */
    std::shared_ptr<ReadView const>
    closed();

    /** Close and advance the ledger.

        The resulting close time will be different and
        greater than the previous close time, and at or
        after the passed-in close time.

        Effects:

            Creates a new closed ledger from the last
            closed ledger.

            All transactions that made it into the open
            ledger are applied to the closed ledger.

            The Application network time is set to
            the close time of the resulting ledger.
    */
    void
    close (NetClock::time_point closeTime,
        boost::optional<std::chrono::milliseconds> consensusDelay = boost::none);

    /** Close and advance the ledger.

        The time is calculated as the duration from
        the previous ledger closing time.
    */
    template <class Rep, class Period>
    void
    close (std::chrono::duration<
        Rep, Period> const& elapsed)
    {
        // VFALCO Is this the correct time?
        close (now() + elapsed);
    }

    /** Close and advance the ledger.

        The time is calculated as five seconds from
        the previous ledger closing time.
    */
    void
    close()
    {
        // VFALCO Is this the correct time?
        close (std::chrono::seconds(5));
    }

    /** Turn on JSON tracing.
        With no arguments, trace all
    */
    void
    trace (int howMany = -1)
    {
        trace_ = howMany;
    }

    /** Turn off JSON tracing. */
    void
    notrace ()
    {
        trace_ = 0;
    }

    /** Turn off signature checks. */
    void
    disable_sigs()
    {
        app().checkSigs(false);
    }

    /** Associate AccountID with account. */
    void
    memoize (Account const& account);

    /** Returns the Account given the AccountID. */
    /** @{ */
    Account const&
    lookup (AccountID const& id) const;

    Account const&
    lookup (std::string const& base58ID) const;
    /** @} */

    /** Returns the XRP balance on an account.
        Returns 0 if the account does not exist.
    */
    PrettyAmount
    balance (Account const& account) const;

    /** Returns the next sequence number on account.
        Exceptions:
            Throws if the account does not exist
    */
    std::uint32_t
    seq (Account const& account) const;

    /** Return the balance on an account.
        Returns 0 if the trust line does not exist.
    */
    // VFALCO NOTE This should return a unit-less amount
    PrettyAmount
    balance (Account const& account,
        Issue const& issue) const;

    /** Return an account root.
        @return empty if the account does not exist.
    */
    std::shared_ptr<SLE const>
    le (Account const& account) const;

    /** Return a ledger entry.
        @return empty if the ledger entry does not exist
    */
    std::shared_ptr<SLE const>
    le (Keylet const& k) const;

    /** Create a JTx from parameters. */
    template <class JsonValue,
        class... FN>
    JTx
    jt (JsonValue&& jv, FN const&... fN)
    {
        JTx jt(std::forward<JsonValue>(jv));
        invoke(jt, fN...);
        autofill(jt);
        jt.stx = st(jt);
        return jt;
    }

    /** Create JSON from parameters.
        This will apply funclets and autofill.
    */
    template <class JsonValue,
        class... FN>
    Json::Value
    json (JsonValue&&jv, FN const&... fN)
    {
        auto tj = jt(
            std::forward<JsonValue>(jv),
                fN...);
        return std::move(tj.jv);
    }

    /** Check a set of requirements.

        The requirements are formed
        from condition functors.
    */
    template <class... Args>
    void
    require (Args const&... args)
    {
        jtx::required(args...)(*this);
    }

    /** Gets the TER result and `didApply` flag from a RPC Json result object.
    */
    static
    std::pair<TER, bool>
    parseResult(Json::Value const& jr);

    /** Submit an existing JTx.
        This calls postconditions.
    */
    virtual
    void
    submit (JTx const& jt);

    /** Use the submit RPC command with a provided JTx object.
        This calls postconditions.
    */
    void
    sign_and_submit(JTx const& jt, Json::Value params = Json::nullValue);

    /** Check expected postconditions
        of JTx submission.
    */
    void
    postconditions(JTx const& jt, TER ter, bool didApply);

    /** Apply funclets and submit. */
    /** @{ */
    template <class JsonValue, class... FN>
    void
    apply (JsonValue&& jv, FN const&... fN)
    {
        submit(jt(std::forward<
            JsonValue>(jv), fN...));
    }

    template <class JsonValue,
        class... FN>
    void
    operator()(JsonValue&& jv, FN const&... fN)
    {
        apply(std::forward<
            JsonValue>(jv), fN...);
    }
    /** @} */

    /** Return the TER for the last JTx. */
    TER
    ter() const
    {
        return ter_;
    }

    /** Return metadata for the last JTx.

        Effects:

            The open ledger is closed as if by a call
            to close(). The metadata for the last
            transaction ID, if any, is returned.
    */
    std::shared_ptr<STObject const>
    meta();

    /** Return the tx data for the last JTx.

        Effects:

            The tx data for the last transaction
            ID, if any, is returned. No side
            effects.

        @note Only necessary for JTx submitted
            with via sign-and-submit method.
    */
    std::shared_ptr<STTx const>
    tx() const;

    void
    enableFeature(uint256 const feature);

private:
    void
    fund (bool setDefaultRipple,
        STAmount const& amount,
            Account const& account);

    // If you get an error here it means
    // you're calling fund with no accounts
    inline
    void
    fund (STAmount const&)
    {
    }

    void
    fund_arg (STAmount const& amount,
        Account const& account)
    {
        fund (true, amount, account);
    }

    template <std::size_t N>
    void
    fund_arg (STAmount const& amount,
        std::array<Account, N> const& list)
    {
        for (auto const& account : list)
            fund (false, amount, account);
    }
public:

    /** Create a new account with some XRP.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings. The XRP is transferred from the master
        account.

        Preconditions:
            The account must not already exist

        Effects:
            The asfDefaultRipple on the account is set,
            and the sequence number is incremented, unless
            the account is wrapped with a call to noripple.

            The account's XRP balance is set to amount.

            Generates a test that the balance is set.

        @param amount The amount of XRP to transfer to
                      each account.

        @param args A heterogeneous list of accounts to fund
                    or calls to noripple with lists of accounts
                    to fund.
    */
    template<class Arg, class... Args>
    void
    fund (STAmount const& amount,
        Arg const& arg, Args const&... args)
    {
        fund_arg (amount, arg);
        fund (amount, args...);
    }

    /** Establish trust lines.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings.

        Preconditions:
            The account must already exist

        Effects:
            A trust line is added for the account.
            The account's sequence number is incremented.
            The account is refunded for the transaction fee
                to set the trust line.

        The refund comes from the master account.
    */
    /** @{ */
    void
    trust (STAmount const& amount,
        Account const& account);

    template<class... Accounts>
    void
    trust (STAmount const& amount, Account const& to0,
        Account const& to1, Accounts const&... toN)
    {
        trust(amount, to0);
        trust(amount, to1, toN...);
    }
    /** @} */

protected:
    int trace_ = 0;
    TestStopwatch stopwatch_;
    uint256 txid_;
    TER ter_ = tesSUCCESS;

    Json::Value
    do_rpc(std::vector<std::string> const& args,
        std::unordered_map<std::string, std::string> const& headers = {});

    void
    autofill_sig (JTx& jt);

    virtual
    void
    autofill (JTx& jt);

    /** Create a STTx from a JTx
        The framework requires that JSON is valid.
        On a parse error, the JSON is logged and
        an exception thrown.
        Throws:
            parse_error
    */
    std::shared_ptr<STTx const>
    st (JTx const& jt);

    // Invoke funclets on stx
    // Note: The STTx may not be modified
    template <class... FN>
    void
    invoke (STTx& stx, FN const&... fN)
    {
        // Sean Parent for_each_argument trick (C++ fold expressions would be
        // nice here)
        (void)std::array<int, sizeof...(fN)>{{((fN(*this, stx)), 0)...}};
    }

    // Invoke funclets on jt
    template <class... FN>
    void
    invoke (JTx& jt, FN const&... fN)
    {
        // Sean Parent for_each_argument trick (C++ fold expressions would be
        // nice here)
        (void)std::array<int, sizeof...(fN)>{{((fN(*this, jt)), 0)...}};
    }

    // Map of account IDs to Account
    std::unordered_map<
        AccountID, Account> map_;
};

template<class... Args>
Json::Value
Env::rpc(std::unordered_map<std::string, std::string> const& headers,
    std::string const& cmd,
    Args&&... args)
{
    return do_rpc(std::vector<std::string>{cmd, std::forward<Args>(args)...},
        headers);
}

template<class... Args>
Json::Value
Env::rpc(std::string const& cmd, Args&&... args)
{
    return rpc(std::unordered_map<std::string, std::string>(), cmd,
        std::forward<Args>(args)...);
}

} // jtx
} // test
} // ripple

#endif
