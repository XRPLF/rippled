#pragma once

#include <test/jtx/AbstractClient.h>
#include <test/jtx/Account.h>
#include <test/jtx/JTx.h>
#include <test/jtx/ManualTimeKeeper.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/require.h>
#include <test/jtx/tags.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/detail/Pathfinder.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>

#include <functional>
#include <future>
#include <source_location>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

/** Wrapper that captures std::source_location when implicitly constructed.
    This solves the problem of combining std::source_location with variadic
    templates. The std::source_location default argument is evaluated at the
    call site when the wrapper is constructed via implicit conversion.

    This is a template struct that holds the value directly, allowing implicit
    conversion without template argument deduction issues via CTAD.
*/
template <class T>
struct WithSourceLocation
{
    T value;
    std::source_location loc;

    // Non-explicit constructor allows implicit conversion.
    // The default argument for loc is evaluated at the call site.
    WithSourceLocation(T v, std::source_location l = std::source_location::current())
        : value(std::move(v)), loc(l)
    {
    }
};

/** Designate accounts as no-ripple in Env::fund */
template <class... Args>
std::array<Account, 1 + sizeof...(Args)>
noripple(Account const& account, Args const&... args)
{
    return {{account, args...}};
}

inline FeatureBitset
testableAmendments()
{
    static FeatureBitset const kIds = [] {
        auto const& sa = allAmendments();
        std::vector<uint256> feats;
        feats.reserve(sa.size());
        for (auto const& [s, vote] : sa)
        {
            (void)vote;
            if (auto const f = getRegisteredFeature(s))
            {
                feats.push_back(*f);
            }
            else
            {
                Throw<std::runtime_error>("Unknown feature: " + s + "  in allAmendments.");
            }
        }
        return FeatureBitset(feats);
    }();
    return kIds;
}

//------------------------------------------------------------------------------

class SuiteLogs : public Logs
{
    beast::unit_test::Suite& suite_;

public:
    explicit SuiteLogs(beast::unit_test::Suite& suite) : Logs(beast::Severity::Error), suite_(suite)
    {
    }

    ~SuiteLogs() override = default;

    std::unique_ptr<beast::Journal::Sink>
    makeSink(std::string const& partition, beast::Severity threshold) override
    {
        return std::make_unique<SuiteJournalSink>(partition, threshold, suite_);
    }
};

//------------------------------------------------------------------------------

/** A transaction testing environment. */
class Env
{
public:
    beast::unit_test::Suite& test;

    Account const& master = Account::kMaster;

    /// Used by parseResult() and postConditions()
    struct ParsedResult
    {
        std::optional<TER> ter;
        // RPC errors tend to return either a "code" and a "message" (sometimes
        // with an "error" that corresponds to the "code"), or with an "error"
        // and an "exception". However, this structure allows all possible
        // combinations.
        std::optional<ErrorCodeI> rpcCode;
        std::string rpcMessage;
        std::string rpcError;
        std::string rpcException;
    };

private:
    struct AppBundle
    {
        Application* app = nullptr;
        std::unique_ptr<Application> owned;
        ManualTimeKeeper* timeKeeper = nullptr;
        std::thread thread;
        std::unique_ptr<AbstractClient> client;

        AppBundle() = default;
        AppBundle(
            beast::unit_test::Suite& suite,
            std::unique_ptr<Config> config,
            std::unique_ptr<Logs> logs,
            beast::Severity thresh);
        ~AppBundle();
    };

    AppBundle bundle_;

public:
    beast::Journal const journal;

    Env() = delete;
    Env&
    operator=(Env const&) = delete;
    Env(Env const&) = delete;

    /**
     * @brief Create Env using suite, Config pointer, and explicit features.
     *
     * This constructor will create an Env with the specified configuration
     * and takes ownership the passed Config pointer. Features will be enabled
     * according to rules described below (see next constructor).
     *
     * @param suite the current unit_test::suite
     * @param config The desired Config - ownership will be taken by moving
     * the pointer. See envconfig and related functions for common config
     * tweaks.
     * @param args with_only_features() to explicitly enable or
     * supported_features_except() to enable all and disable specific features.
     */
    // VFALCO Could wrap the suite::log in a Journal here
    Env(beast::unit_test::Suite& suite,
        std::unique_ptr<Config> config,
        FeatureBitset features,
        std::unique_ptr<Logs> logs = nullptr,
        beast::Severity thresh = beast::Severity::Error)
        : test(suite)
        , bundle_(suite, std::move(config), std::move(logs), thresh)
        , journal{bundle_.app->getJournal("Env")}
    {
        memoize(Account::kMaster);
        Pathfinder::initPathTable();
        foreachFeature(features, [&appFeats = app().config().features](uint256 const& f) {
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
     * @param suite the current unit_test::suite
     * @param args collection of features
     *
     */
    Env(beast::unit_test::Suite& suite,
        FeatureBitset features,
        std::unique_ptr<Logs> logs = nullptr)
        : Env(suite, envconfig(), features, std::move(logs))
    {
    }

    /**
     * @brief Create Env using suite and Config pointer.
     *
     * This constructor will create an Env with the specified configuration
     * and takes ownership the passed Config pointer. All supported amendments
     * are enabled by this version of the constructor.
     *
     * @param suite the current unit_test::suite
     * @param config The desired Config - ownership will be taken by moving
     * the pointer. See envconfig and related functions for common config
     * tweaks.
     */
    Env(beast::unit_test::Suite& suite,
        std::unique_ptr<Config> config,
        std::unique_ptr<Logs> logs = nullptr,
        beast::Severity thresh = beast::Severity::Error)
        : Env(suite, std::move(config), testableAmendments(), std::move(logs), thresh)
    {
    }

    /**
     * @brief Create Env with only the current test suite
     *
     * This constructor will create an Env with the standard
     * test Env configuration (from envconfig()) and all supported
     * amendments enabled.
     *
     * @param suite the current unit_test::suite
     */
    Env(beast::unit_test::Suite& suite, beast::Severity thresh = beast::Severity::Error)
        : Env(suite, envconfig(), nullptr, thresh)
    {
    }

    virtual ~Env() = default;

    Application&
    // NOLINTNEXTLINE(readability-make-member-function-const)
    app()
    {
        return *bundle_.app;
    }

    [[nodiscard]] Application const&
    app() const
    {
        return *bundle_.app;
    }

    ManualTimeKeeper&
    // NOLINTNEXTLINE(readability-make-member-function-const)
    timeKeeper()
    {
        return *bundle_.timeKeeper;
    }

    /** Returns the current network time

        @note This is manually advanced when ledgers
              close or by callers.
    */
    NetClock::time_point
    // NOLINTNEXTLINE(readability-make-member-function-const)
    now()
    {
        return timeKeeper().now();
    }

    /** Returns the connected client. */
    AbstractClient&
    // NOLINTNEXTLINE(readability-make-member-function-const)
    client()
    {
        return *bundle_.client;
    }

    /** Execute an RPC command.

        The command is examined and used to build
        the correct JSON as per the arguments.
    */
    template <class... Args>
    json::Value
    rpc(unsigned apiVersion,
        std::unordered_map<std::string, std::string> const& headers,
        std::string const& cmd,
        Args&&... args);

    template <class... Args>
    json::Value
    rpc(unsigned apiVersion, std::string const& cmd, Args&&... args);

    template <class... Args>
    json::Value
    rpc(std::unordered_map<std::string, std::string> const& headers,
        std::string const& cmd,
        Args&&... args);

    template <class... Args>
    json::Value
    rpc(std::string const& cmd, Args&&... args);

    /** Returns the current ledger.

        This is a non-modifiable snapshot of the
        open ledger at the moment of the call.
        Transactions applied after the call to open()
        will not be visible.

    */
    [[nodiscard]] std::shared_ptr<OpenView const>
    current() const
    {
        return app().getOpenLedger().current();
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

        @return true if no error, false if error
    */
    bool
    close(
        NetClock::time_point closeTime,
        std::optional<std::chrono::milliseconds> consensusDelay = std::nullopt);

    /** Close and advance the ledger.

        The time is calculated as the duration from
        the previous ledger closing time.

        @return true if no error, false if error
    */
    template <class Rep, class Period>
    bool
    close(std::chrono::duration<Rep, Period> const& elapsed)
    {
        // VFALCO Is this the correct time?
        return close(now() + elapsed);
    }

    /** Close and advance the ledger.

        The time is calculated as five seconds from
        the previous ledger closing time.

        @return true if no error, false if error
    */
    bool
    close()
    {
        // VFALCO Is this the correct time?
        return close(std::chrono::seconds(5));
    }

    /** Close and advance the ledger, then synchronize with the server's
        io_context to ensure all async operations initiated by the close have
        been started.

        This function performs the same ledger close as close(), but additionally
        ensures that all tasks posted to the server's io_context (such as
        WebSocket subscription message sends) have been initiated before returning.

        What it guarantees:
        - All async operations posted before syncClose() have been STARTED
        - For WebSocket sends: async_write_some() has been called
        - The actual I/O completion may still be pending (async)

        What it does NOT guarantee:
        - Async operations have COMPLETED
        - WebSocket messages have been received by clients
        - However, for localhost connections, the remaining latency is typically
          microseconds, making tests reliable

        Use this instead of close() when:
        - Test code immediately checks for subscription messages
        - Race conditions between test and worker threads must be avoided
        - Deterministic test behavior is required

        @param timeout Maximum time to wait for the barrier task to execute
        @return true if close succeeded and barrier executed within timeout,
                false otherwise
    */
    [[nodiscard]] bool
    syncClose(std::chrono::steady_clock::duration timeout = std::chrono::seconds{1})
    {
        XRPL_ASSERT(
            app().getNumberOfThreads() == 1,
            "syncClose() is only useful on an application with a single thread");
        auto const result = close();
        auto serverBarrier = std::make_shared<std::promise<void>>();
        auto future = serverBarrier->get_future();
        boost::asio::post(app().getIOContext(), [serverBarrier]() { serverBarrier->set_value(); });
        auto const status = future.wait_for(timeout);
        return result && status == std::future_status::ready;
    }

    /** Turn on JSON tracing.
        With no arguments, trace all
    */
    void
    trace(int howMany = -1)
    {
        trace_ = howMany;
    }

    /** Turn off JSON tracing. */
    void
    notrace()
    {
        trace_ = 0;
    }

    void
    setParseFailureExpected(bool b)
    {
        parseFailureExpected_ = b;
    }

    /** Turn off signature checks. */
    void
    disableSigs()
    {
        app().checkSigs(false);
    }

    // set rpc retries
    void
    setRetries(unsigned r = 5)
    {
        retries_ = r;
    }

    // get rpc retries
    [[nodiscard]] unsigned
    retries() const
    {
        return retries_;
    }

    /** Associate AccountID with account. */
    void
    memoize(Account const& account);

    /** Returns the Account given the AccountID. */
    /** @{ */
    [[nodiscard]] Account const&
    lookup(AccountID const& id) const;

    [[nodiscard]] Account const&
    lookup(std::string const& base58ID) const;
    /** @} */

    /** Returns the XRP balance on an account.
        Returns 0 if the account does not exist.
    */
    [[nodiscard]] PrettyAmount
    balance(Account const& account) const;

    /** Returns the next sequence number on account.
        Exceptions:
            Throws if the account does not exist
    */
    [[nodiscard]] std::uint32_t
    seq(Account const& account) const;

    /** Return the balance on an account.
        Returns 0 if the trust line does not exist.
    */
    // VFALCO NOTE This should return a unit-less amount
    [[nodiscard]] PrettyAmount
    balance(Account const& account, Asset const& asset) const;

    /** Returns the IOU limit on an account.
        Returns 0 if the trust line does not exist.
    */
    [[nodiscard]] PrettyAmount
    limit(Account const& account, Issue const& issue) const;

    /** Return the number of objects owned by an account.
     * Returns 0 if the account does not exist.
     */
    [[nodiscard]] std::uint32_t
    ownerCount(Account const& account) const;

    /** Return an account root.
        @return empty if the account does not exist.
    */
    [[nodiscard]] std::shared_ptr<SLE const>
    le(Account const& account) const;

    /** Return a ledger entry.
        @return empty if the ledger entry does not exist
    */
    [[nodiscard]] std::shared_ptr<SLE const>
    le(Keylet const& k) const;

    /** Create a JTx from parameters. */
    template <class JsonValue, class... FN>
    JTx
    jt(JsonValue&& jv, FN const&... fN)
    {
        JTx jt(std::forward<JsonValue>(jv));
        invoke(jt, fN...);
        autofill(jt);
        jt.stx = st(jt);
        return jt;
    }

    /** Create a JTx from parameters. */
    template <class JsonValue, class... FN>
    JTx
    jtnofill(JsonValue&& jv, FN const&... fN)
    {
        JTx jt(std::forward<JsonValue>(jv));
        invoke(jt, fN...);
        autofillSig(jt);
        jt.stx = st(jt);
        return jt;
    }

    /** Create JSON from parameters.
        This will apply funclets and autofill.
    */
    template <class JsonValue, class... FN>
    json::Value
    json(JsonValue&& jv, FN const&... fN)
    {
        auto tj = jt(std::forward<JsonValue>(jv), fN...);
        return std::move(tj.jv);
    }

    /** Check a set of requirements.

        The requirements are formed
        from condition functors.
    */
    template <class... Args>
    void
    require(Args const&... args)
    {
        jtx::required(args...)(*this);
    }

    /** Gets the TER result and `didApply` flag from a RPC Json result object.
     */
    static ParsedResult
    parseResult(json::Value const& jr);

    /** Submit an existing JTx.
        This calls postconditions.
    */
    virtual void
    submit(JTx const& jt, std::source_location const& loc = std::source_location::current());

    /** Use the submit RPC command with a provided JTx object.
        This calls postconditions.
    */
    void
    signAndSubmit(
        JTx const& jt,
        json::Value params = json::ValueType::Null,
        std::source_location const& loc = std::source_location::current());

    /** Check expected postconditions
        of JTx submission.
    */
    void
    postconditions(
        JTx const& jt,
        ParsedResult const& parsed,
        json::Value const& jr = json::Value(),
        std::source_location const& loc = std::source_location::current());

    /** Apply funclets and submit. */
    /** @{ */
    template <class... FN>
    Env&
    apply(WithSourceLocation<json::Value> jv, FN const&... fN)
    {
        submit(jt(std::move(jv.value), fN...), jv.loc);
        return *this;
    }

    template <class... FN>
    Env&
    apply(WithSourceLocation<JTx> jv, FN const&... fN)
    {
        submit(jt(std::move(jv.value), fN...), jv.loc);
        return *this;
    }

    template <class... FN>
    Env&
    operator()(WithSourceLocation<json::Value> jv, FN const&... fN)
    {
        return apply(std::move(jv), fN...);
    }

    template <class... FN>
    Env&
    operator()(WithSourceLocation<JTx> jv, FN const&... fN)
    {
        return apply(std::move(jv), fN...);
    }
    /** @} */

    /** Return the TER for the last JTx. */
    [[nodiscard]] TER
    ter() const
    {
        return ter_;
    }

    /** Return metadata for the last JTx.
     *
     *  NOTE: this has a side effect of closing the open ledger.
     *  The ledger will only be closed if it includes transactions.
     *
     *  Effects:
     *
     *      The open ledger is closed as if by a call
     *      to close(). The metadata for the last
     *      transaction ID, if any, is returned.
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
    [[nodiscard]] std::shared_ptr<STTx const>
    tx() const;

    void
    enableFeature(uint256 const feature);

    void
    disableFeature(uint256 const feature);

    [[nodiscard]] bool
    enabled(uint256 feature) const
    {
        return current()->rules().enabled(feature);
    }

private:
    void
    fund(bool setDefaultRipple, STAmount const& amount, Account const& account);

    void
    fundArg(STAmount const& amount, Account const& account)
    {
        fund(true, amount, account);
    }

    template <std::size_t N>
    void
    fundArg(STAmount const& amount, std::array<Account, N> const& list)
    {
        for (auto const& account : list)
            fund(false, amount, account);
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
    template <class Arg, class... Args>
    void
    fund(STAmount const& amount, Arg const& arg, Args const&... args)
    {
        fundArg(amount, arg);
        if constexpr (sizeof...(args) > 0)
            fund(amount, args...);
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
    trust(STAmount const& amount, Account const& account);

    template <class... Accounts>
    void
    trust(STAmount const& amount, Account const& to0, Account const& to1, Accounts const&... toN)
    {
        trust(amount, to0);
        trust(amount, to1, toN...);  // NOLINT(readability-suspicious-call-argument)
    }
    /** @} */

    /** Create a STTx from a JTx without sanitizing
        Use to inject bogus values into test transactions by first
        editing the JSON.
    */
    std::shared_ptr<STTx const>
    ust(JTx const& jt);

protected:
    int trace_ = 0;
    TestStopwatch stopwatch_;
    uint256 txid_;
    TER ter_ = tesSUCCESS;
    bool parseFailureExpected_ = false;
    unsigned retries_ = 5;

    json::Value
    doRpc(
        unsigned apiVersion,
        std::vector<std::string> const& args,
        std::unordered_map<std::string, std::string> const& headers = {});

    void
    autofillSig(JTx& jt);

    virtual void
    autofill(JTx& jt);

    /** Create a STTx from a JTx
        The framework requires that JSON is valid.
        On a parse error, the JSON is logged and
        an exception thrown.
        Throws:
            ParseError
    */
    std::shared_ptr<STTx const>
    st(JTx const& jt);

    // Invoke funclets on stx
    // Note: The STTx may not be modified
    template <class... FN>
    void
    invoke(STTx& stx, FN const&... fN)
    {
        (fN(*this, stx), ...);
    }

    // Invoke funclets on jt
    template <class... FN>
    void
    invoke(JTx& jt, FN const&... fN)
    {
        (fN(*this, jt), ...);
    }

    // Map of account IDs to Account
    std::unordered_map<AccountID, Account> map_;
};

template <class... Args>
json::Value
Env::rpc(
    unsigned apiVersion,
    std::unordered_map<std::string, std::string> const& headers,
    std::string const& cmd,
    Args&&... args)
{
    return doRpc(apiVersion, std::vector<std::string>{cmd, std::forward<Args>(args)...}, headers);
}

template <class... Args>
json::Value
Env::rpc(unsigned apiVersion, std::string const& cmd, Args&&... args)
{
    return rpc(
        apiVersion,
        std::unordered_map<std::string, std::string>(),
        cmd,
        std::forward<Args>(args)...);
}

template <class... Args>
json::Value
Env::rpc(
    std::unordered_map<std::string, std::string> const& headers,
    std::string const& cmd,
    Args&&... args)
{
    return doRpc(
        RPC::kApiCommandLineVersion,
        std::vector<std::string>{cmd, std::forward<Args>(args)...},
        headers);
}

template <class... Args>
json::Value
Env::rpc(std::string const& cmd, Args&&... args)
{
    return rpc(std::unordered_map<std::string, std::string>(), cmd, std::forward<Args>(args)...);
}

}  // namespace xrpl::test::jtx
