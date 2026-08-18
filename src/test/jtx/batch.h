#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/SignerUtils.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

/**
 * @brief Helpers for constructing Batch test transactions.
 */
namespace xrpl::test::jtx::batch {

/**
 * @brief Calculate the expected outer Batch transaction fee.
 *
 * @param env The test environment providing ledger fee settings.
 * @param numSigners Number of outer transaction signers.
 * @param txns Number of inner transactions in the batch.
 * @return The expected Batch fee.
 */
XRPAmount
calcBatchFee(jtx::Env const& env, uint32_t const& numSigners, uint32_t const& txns = 0);

/**
 * @brief Calculate the expected Batch fee when inner transactions are
 * confidential MPT transactions.
 *
 * @param env The test environment providing ledger fee settings.
 * @param numSigners Number of outer transaction signers.
 * @param txns Number of confidential MPT inner transactions in the batch.
 * @return The expected Batch fee including confidential transaction fee
 *         multipliers.
 */
XRPAmount
calcConfidentialBatchFee(jtx::Env const& env, uint32_t const& numSigners, uint32_t const& txns = 0);

/**
 * @brief Build an outer Batch transaction JSON object.
 *
 * @param account The account submitting the outer Batch transaction.
 * @param seq The sequence number for the outer Batch transaction.
 * @param fee The fee to set on the outer Batch transaction.
 * @param flags The transaction flags to set.
 * @return The outer Batch transaction JSON object.
 */
json::Value
outer(jtx::Account const& account, uint32_t seq, STAmount const& fee, std::uint32_t flags);

/**
 * @brief Submit a Batch transaction like env(...), returning its ids.
 *
 * Builds and signs the Batch (via env.jt) so its transaction ids can be read
 * before it is submitted: once a Batch is queued it is not in the open
 * ledger, so env.tx() cannot be used to recover the ids afterwards.
 *
 * @param env The test environment.
 * @param fN The same functors passed to env(...): batch::outer, batch::Inner,
 *        batch::Sig, Ter, etc.
 * @return The outer Batch id followed by every inner transaction id. The
 *         caller does not need to know which is which, only that they all
 *         get applied (see closeEnv).
 */
template <class... FN>
std::vector<uint256>
callEnv(jtx::Env& env, FN const&... fN)
{
    auto const jt = env.jt(fN...);
    std::vector<uint256> txids{jt.stx->getTransactionID()};
    auto const inner = jt.stx->getBatchTransactionIDs();
    txids.insert(txids.end(), inner.begin(), inner.end());
    env(jt);
    return txids;
}

/**
 * @brief Result of draining the queue with closeEnv.
 */
struct BatchResult
{
    // True if every requested transaction landed in a closed ledger.
    bool allApplied{false};

    // Any requested transaction ids that never made it into a ledger.
    std::vector<uint256> notApplied;

    // Number of ledger closes performed.
    int iterations{0};

    // Size of the transaction queue after draining.
    std::size_t txCount{0};
};

/**
 * @brief Close the ledger repeatedly until every txid is applied.
 *
 * Because env.close() drives ledger_accept asynchronously, queued Batch and
 * inner transactions can take several closes to drain on a loaded machine.
 * This closes the ledger (pausing briefly between closes) up to maxRetries
 * times, stopping once every transaction in txids has been applied to a
 * closed ledger. It does not itself assert anything; the caller inspects the
 * returned BatchResult and verifies it with BEAST_EXPECT(S).
 *
 * @param env The test environment.
 * @param txids The transaction ids that must be applied (e.g. from callEnv).
 * @param maxRetries Maximum number of ledger closes to perform.
 * @return A BatchResult describing which txids applied, the number of closes
 *         performed and the final queue size.
 */
BatchResult
closeEnv(jtx::Env& env, std::vector<uint256> const& txids, int maxRetries = 20);

/**
 * @brief Adds an inner Batch transaction to a JTx and autofills it.
 */
class Inner
{
private:
    json::Value txn_;
    std::optional<std::uint32_t> ticket_;

public:
    Inner(
        json::Value txn,
        std::uint32_t const& sequence,
        std::optional<std::uint32_t> const& ticket = std::nullopt)
        : txn_(std::move(txn)), ticket_(ticket)
    {
        txn_[jss::SigningPubKey] = "";
        txn_[jss::Sequence] = sequence;
        txn_[jss::Fee] = "0";
        txn_[jss::Flags] = txn_[jss::Flags].asUInt() | tfInnerBatchTxn;

        // Optionally set ticket sequence
        if (ticket_.has_value())
        {
            txn_[jss::Sequence] = 0;
            txn_[sfTicketSequence.jsonName] = *ticket_;
        }
    }

    void
    operator()(Env&, JTx& jtx) const;

    json::Value&
    operator[](json::StaticString const& key)
    {
        return txn_[key];
    }

    void
    removeMember(json::StaticString const& key)
    {
        txn_.removeMember(key);
    }

    [[nodiscard]] json::Value const&
    getTxn() const
    {
        return txn_;
    }
};

/**
 * @brief Sets the Batch transaction signers on a JTx.
 */
class Sig
{
public:
    std::vector<Reg> signers;

    Sig(std::vector<Reg> s) : signers(std::move(s))
    {
        sortSigners(signers);
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Sig(AccountType&& a0, Accounts&&... aN)
        : signers{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}
    {
        sortSigners(signers);
    }

    void
    operator()(Env&, JTx& jt) const;
};

/**
 * @brief Sets a nested multi-signature for a Batch transaction on a JTx.
 */
class Msig
{
public:
    Account master;
    std::vector<Reg> signers;

    Msig(Account masterAccount, std::vector<Reg> s)
        : master(std::move(masterAccount)), signers(std::move(s))
    {
        sortSigners(signers);
    }

    template <class AccountType, class... Accounts>
        requires std::convertible_to<AccountType, Reg>
    explicit Msig(Account masterAccount, AccountType&& a0, Accounts&&... aN)
        : master(std::move(masterAccount))
        , signers{std::forward<AccountType>(a0), std::forward<Accounts>(aN)...}
    {
        sortSigners(signers);
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx::batch
