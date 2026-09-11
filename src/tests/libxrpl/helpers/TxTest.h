#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/protocol_autogen/ledger_entries/AccountRoot.h>
#include <xrpl/tx/applySteps.h>

#include <helpers/Account.h>
#include <helpers/IOU.h>
#include <helpers/TestServiceRegistry.h>

#include <cmath>
#include <concepts>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace xrpl::test {

//------------------------------------------------------------------------------
// Amount helpers
//------------------------------------------------------------------------------

/**
 * @brief Convert XRP to drops (integral types).
 * @param xrp The amount in XRP.
 * @return The equivalent amount in drops as XRPAmount.
 */
template <std::integral T>
constexpr XRPAmount
XRP(T xrp)  // NOLINT(readability-identifier-naming)
{
    return XRPAmount{static_cast<std::int64_t>(xrp) * kDropsPerXrp.drops()};
}

/**
 * @brief Convert XRP to drops (floating point types).
 * @param xrp The amount in XRP (may be fractional).
 * @return The equivalent amount in drops as XRPAmount.
 */
template <std::floating_point T>
XRPAmount
XRP(T xrp)
{
    return XRPAmount{static_cast<std::int64_t>(std::round(xrp * kDropsPerXrp.drops()))};
}

/**
 * @brief Convert XRP to drops (Number type).
 * @param xrp The amount in XRP as a Number.
 * @return The equivalent amount in drops as XRPAmount.
 */
inline XRPAmount
XRP(Number const& xrp)
{
    return XRPAmount{static_cast<std::int64_t>(xrp * kDropsPerXrp.drops())};
}

//------------------------------------------------------------------------------
// Flag helpers
//------------------------------------------------------------------------------

/**
 * @brief Convert AccountSet flag (asf) to LedgerState flag (lsf).
 * @param asf The AccountSet flag value.
 * @return The corresponding LedgerState flag.
 * @throws std::runtime_error if the flag is not supported.
 *
 * Supported flags:
 *   asfRequireDest, asfRequireAuth, asfDisallowXRP, asfDisableMaster,
 *   asfNoFreeze, asfGlobalFreeze, asfDefaultRipple, asfDepositAuth,
 *   asfAllowTrustLineClawback, asfDisallowIncomingCheck,
 *   asfDisallowIncomingNFTokenOffer, asfDisallowIncomingPayChan,
 *   asfDisallowIncomingTrustline, asfAllowTrustLineLocking
 */
constexpr std::uint32_t
asfToLsf(std::uint32_t asf)
{
    switch (asf)
    {
        case asfRequireDest:
            return lsfRequireDestTag;
        case asfRequireAuth:
            return lsfRequireAuth;
        case asfDisallowXRP:
            return lsfDisallowXRP;
        case asfDisableMaster:
            return lsfDisableMaster;
        case asfNoFreeze:
            return lsfNoFreeze;
        case asfGlobalFreeze:
            return lsfGlobalFreeze;
        case asfDefaultRipple:
            return lsfDefaultRipple;
        case asfDepositAuth:
            return lsfDepositAuth;
        case asfAllowTrustLineClawback:
            return lsfAllowTrustLineClawback;
        case asfDisallowIncomingCheck:
            return lsfDisallowIncomingCheck;
        case asfDisallowIncomingNFTokenOffer:
            return lsfDisallowIncomingNFTokenOffer;
        case asfDisallowIncomingPayChan:
            return lsfDisallowIncomingPayChan;
        case asfDisallowIncomingTrustline:
            return lsfDisallowIncomingTrustline;
        case asfAllowTrustLineLocking:
            return lsfAllowTrustLineLocking;
        default:
            throw std::runtime_error("Unknown asf flag");
    }
}

//------------------------------------------------------------------------------
// Feature helpers
//------------------------------------------------------------------------------

/**
 * @brief Returns all testable amendments.
 * @note This is similar to jtx::testable_amendments() but for the TxTest framework.
 */
FeatureBitset
allFeatures();

//------------------------------------------------------------------------------
// TxResult
//------------------------------------------------------------------------------

/**
 * @brief Result of a transaction submission in TxTest.
 *
 * Contains the TER code, whether the transaction was applied,
 * optional metadata, and a reference to the submitted transaction.
 * Use standard gtest macros (EXPECT_EQ, EXPECT_TRUE, etc.) to verify results.
 */
struct TxResult
{
    TER ter;                         ///< The transaction engine result code.
    bool applied;                    ///< Whether the transaction was applied to the ledger.
    std::optional<TxMeta> metadata;  ///< Transaction metadata, if available.
    std::shared_ptr<STTx const> tx;  ///< Pointer to the submitted transaction.
};

/**
 * @brief Result of a transaction submission that has been closed into a ledger.
 *
 * `TxResult::metadata` is always `std::nullopt`, because metadata is only built for a view
 * that is not open. This is what `TxTest::submitAndClose` returns instead: the result code
 * paired with the metadata that closing produced.
 */
struct ClosedResult
{
    TER ter;                     ///< The transaction engine result code.
    std::optional<TxMeta> meta;  ///< Metadata from the close, absent if none was produced.
};

/**
 * @brief A lightweight transaction testing harness.
 *
 * Unlike the JTx framework which requires a full Application and RPC layer,
 * TxTest applies transactions directly to an OpenView using the transactor
 * pipeline (preflight -> preclaim -> doApply).
 *
 * This makes it suitable for:
 * - Unit testing individual transactors
 * - Testing transaction validation logic
 * - Fast, focused tests without full server infrastructure
 *
 * @code
 *     TxTest env;
 *     env.submit(paymentTx).expectSuccess();
 *     env.submit(badTx).expectTer(tecNO_ENTRY);
 * @endcode
 */
class TxTest
{
public:
    /**
     * @brief Construct a TxTest environment.
     *
     * Creates a genesis ledger and an open view on top of it.
     *
     * @param features Optional set of features to enable. If not specified,
     *                 uses all testable amendments.
     * @param fees Optional fee settings. If not specified, uses
     *             `TestServiceRegistry::defaultFees()`. Applied to **both** the genesis
     *             ledger and the service registry, because transactors read limits from the
     *             registry (`ctx.registry.get().getFees()`) while `calculateBaseFee` reads
     *             `view.fees()` — a test whose fees disagree across the two is testing a
     *             state no ledger can be in.
     */
    explicit TxTest(
        std::optional<FeatureBitset> features = std::nullopt,
        std::optional<Fees> fees = std::nullopt);

    /**
     * @brief Check if a feature is enabled.
     * @param feature The feature to check.
     * @return True if the feature is enabled.
     */
    [[nodiscard]] bool
    isEnabled(uint256 const& feature) const;

    /**
     * @brief Get the current rules.
     * @return The current consensus rules.
     */
    [[nodiscard]] Rules const&
    getRules() const;

    /**
     * @brief Submit a transaction from a builder.
     *
     * Convenience overload that accepts transaction builders.
     * Automatically sets sequence and fee before submission.
     *
     * @tparam T A type derived from TransactionBuilderBase.
     * @param builder The transaction builder.
     * @param signer The account to sign with.
     * @param fee The fee to pay. The 10 drop default is below what some transactions
     *            require: an `EscrowCreate` carrying `sfBytecode` owes
     *            `base * 10 + 5 * bytecodeBytes` (`EscrowCreate::calculateBaseFee`), and an
     *            `EscrowFinish` carrying `sfGas` owes the allowance priced at `gasPrice`.
     *            Those submissions would fail on the fee rather than on whatever they meant
     *            to test, so they must pass one explicitly.
     * @return TxResult containing the result code, applied status, and metadata.
     */
    template <typename T>
        requires std::
            derived_from<std::decay_t<T>, transactions::TransactionBuilderBase<std::decay_t<T>>>
        [[nodiscard]] TxResult
        submit(T&& builder, Account const& signer, XRPAmount fee = XRPAmount{10})
    {
        auto const& obj = builder.getSTObject();
        auto accountId = obj[sfAccount];
        if (!obj.isFieldPresent(sfTicketSequence))
        {
            builder.setSequence(getAccountRoot(accountId).getSequence());
        }
        else
        {
            builder.setSequence(0);
        }
        builder.setFee(fee);
        return submit(builder.build(signer.pk(), signer.sk()).getSTTx());
    }

    /**
     * @brief Submit a transaction, then close the ledger and return its metadata.
     *
     * Metadata comes into being at `close`, not at `submit` (see `close`), so any assertion
     * about `sfGasUsed`, `sfVMReturnCode`, or a delivered amount needs this three-step
     * sequence rather than `submit` alone. Closing also advances time by one close
     * interval, which matters to a test sensitive to a `FinishAfter` or an expiry.
     *
     * @tparam T A type derived from TransactionBuilderBase.
     * @param builder The transaction builder.
     * @param signer The account to sign with.
     * @param fee The fee to pay; see `submit` for when the default is not enough.
     * @return The result code and the metadata produced by the close.
     */
    template <typename T>
        requires std::
            derived_from<std::decay_t<T>, transactions::TransactionBuilderBase<std::decay_t<T>>>
        [[nodiscard]] ClosedResult
        submitAndClose(T&& builder, Account const& signer, XRPAmount fee = XRPAmount{10})
    {
        auto const result = submit(std::forward<T>(builder), signer, fee);
        close();
        return ClosedResult{.ter = result.ter, .meta = getMetadata(result.tx->getTransactionID())};
    }

    /**
     * @brief Submit a transaction to the open ledger.
     *
     * Applies the transaction through the full transactor pipeline:
     * preflight -> preclaim -> doApply -> invariant checks
     *
     * Invariant checks are automatically run after doApply. If any
     * invariant fails, the result will be tecINVARIANT_FAILED.
     *
     * @param stx The transaction to submit.
     * @return TxResult containing the result code, applied status, and metadata.
     */
    [[nodiscard]] TxResult
    submit(std::shared_ptr<STTx const> stx);

    /**
     * @brief Create a new account in the ledger.
     *
     * Sends a Payment from the master account to create and fund the account.
     * Closes the ledger after creation. If accountFlags is non-zero, submits
     * an AccountSet transaction and closes again.
     *
     * @param account The account to create.
     * @param xrp The initial XRP balance.
     * @param accountFlags Optional account flags to set. Defaults to 0
     *        (no flags).
     */
    void
    createAccount(Account const& account, XRPAmount xrp, uint32_t accountFlags = 0);

    /**
     * @brief Get the account root object from the current open ledger.
     * @param id The account ID.
     * @return The AccountRoot ledger entry.
     * @throws std::runtime_error if the account does not exist.
     * @todo Once we make keylet strongly typed, we can ditch this method.
     */
    [[nodiscard]] ledger_entries::AccountRoot
    getAccountRoot(AccountID const& id) const;

    /**
     * @brief Get an account's owner count.
     * @param id The account ID.
     * @return The number of ledger objects the account owns.
     * @throws std::runtime_error if the account does not exist.
     */
    [[nodiscard]] std::uint32_t
    getOwnerCount(AccountID const& id) const;

    /**
     * @brief Get an account's XRP balance.
     *
     * The IOU overload of `getBalance` covers trust lines; this covers the account's own
     * drops, which is what a fee- or reserve-sensitive test needs to assert on.
     *
     * @param id The account ID.
     * @return The balance in drops.
     * @throws std::runtime_error if the account does not exist.
     */
    [[nodiscard]] XRPAmount
    getXrpBalance(AccountID const& id) const;

    /**
     * @brief Get the current open ledger view.
     * @return A mutable reference to the open ledger.
     */
    [[nodiscard]] OpenView&
    getOpenLedger();

    /**
     * @brief Get the current open ledger view (const).
     * @return A const reference to the open ledger.
     */
    [[nodiscard]] OpenView const&
    getOpenLedger() const;

    /**
     * @brief Get the closed (base) ledger view.
     * @return A const reference to the closed ledger.
     */
    [[nodiscard]] ReadView const&
    getClosedLedger() const;

    /**
     * @brief Close the current ledger.
     *
     * Creates a new closed ledger from the current open ledger.
     * All pending transactions are re-applied in canonical order.
     *
     * @note This is where transaction **metadata** comes into being: it is only built for a
     *       view that is not open (`ApplyStateTable::apply`), so `submit` cannot return any.
     *       Each closed transaction's metadata is retained for `getMetadata`.
     */
    void
    close();

    /**
     * @brief Get the metadata of a transaction in the most recently closed ledger.
     *
     * Metadata is a property of a *closed* ledger, so the sequence is submit → `close` →
     * `getMetadata`. Only the latest close is retained.
     *
     * @param txId The transaction's ID (`TxResult::tx->getTransactionID()`).
     * @return The metadata, or `std::nullopt` if that transaction was not in the last close.
     */
    [[nodiscard]] std::optional<TxMeta>
    getMetadata(uint256 const& txId) const;

    /**
     * @brief Advance time without closing the ledger.
     *
     * Useful for testing time-dependent features like escrow release
     * times or offer expirations.
     *
     * @param duration The amount of time to advance.
     */
    void
    advanceTime(NetClock::duration duration);

    /**
     * @brief Get the current ledger close time.
     * @return The current close time.
     */
    [[nodiscard]] NetClock::time_point
    getCloseTime() const;

    /**
     * @brief Get the balance of an IOU for an account.
     *
     * Returns the balance from the perspective of the specified account.
     * If the trust line doesn't exist, returns zero.
     *
     * @param account The account to check.
     * @param iou The IOU to check the balance for.
     * @return The balance as an STAmount.
     * @todo Once we make keylet strongly typed, we can ditch this method.
     */
    [[nodiscard]] STAmount
    getBalance(AccountID const& account, IOU const& iou) const;

    /**
     * @brief Get the service registry.
     *
     * Returns the concrete test type so a test can reach its setters — `setFees` in
     * particular, for the cases that need a limit to change *after* setup, which the
     * constructor's `fees` parameter cannot express.
     *
     * @return A reference to the service registry.
     */
    TestServiceRegistry&
    getServiceRegistry()
    {
        return registry_;
    }

private:
    TestServiceRegistry registry_;
    std::unordered_set<uint256, beast::Uhash<>> featureSet_;
    std::optional<Rules> rules_;
    std::shared_ptr<Ledger const> closedLedger_;
    std::shared_ptr<OpenView> openLedger_;

    /**
     * Transactions submitted to the open ledger, for canonical reordering on close.
     */
    std::vector<std::shared_ptr<STTx const>> pendingTxs_;

    /**
     * Metadata from the most recent close, keyed by transaction ID. Replaced each close.
     */
    std::map<uint256, TxMeta> closedMetadata_;

    /**
     * Current time (can be advanced arbitrarily for testing).
     */
    NetClock::time_point now_;
};

//------------------------------------------------------------------------------
// TxTest free helpers
//------------------------------------------------------------------------------

/**
 * @brief A ledger-close-time deadline `seconds` in the future.
 *
 * Time fields on the wire are `std::uint32_t` seconds since the XRPL epoch, while the
 * environment reports a `NetClock::time_point`. Every `CancelAfter` / `FinishAfter` needs
 * the same cast, and getting it wrong yields a deadline in the past — which a transactor
 * reports as `temBAD_EXPIRATION`, a failure that looks like the case under test.
 *
 * @param env The environment whose close time the deadline is relative to.
 * @param seconds How far past the current close time the deadline should sit.
 * @return The deadline, as a transaction field expects it.
 */
[[nodiscard]] std::uint32_t
closeTimeOffset(TxTest const& env, std::uint32_t seconds);

/**
 * @brief Create and fund several accounts with the same balance.
 *
 * @code
 *     createAccounts(env, XRP(5'000), alice, carol);
 * @endcode
 *
 * @param env The environment to create the accounts in.
 * @param xrp The initial balance for each account.
 * @param accounts The accounts to create.
 */
template <std::same_as<Account>... Accounts>
void
createAccounts(TxTest& env, XRPAmount xrp, Accounts const&... accounts)
{
    (env.createAccount(accounts, xrp), ...);
}

}  // namespace xrpl::test
