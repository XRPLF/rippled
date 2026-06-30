#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/SignerUtils.h>
#include <test/jtx/amount.h>
#include <test/jtx/owners.h>
#include <test/jtx/tags.h>

#include <xrpl/protocol/TxFlags.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <utility>

/** @brief Helpers for constructing Batch test transactions. */
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

/** @brief Adds an inner Batch transaction to a JTx and autofills it. */
class Inner
{
private:
    json::Value txn_;
    std::uint32_t seq_;
    std::optional<std::uint32_t> ticket_;

public:
    Inner(
        json::Value txn,
        std::uint32_t const& sequence,
        std::optional<std::uint32_t> const& ticket = std::nullopt)
        : txn_(std::move(txn)), seq_(sequence), ticket_(ticket)
    {
        txn_[jss::SigningPubKey] = "";
        txn_[jss::Sequence] = seq_;
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

/** @brief Sets the Batch transaction signers on a JTx. */
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

/** @brief Sets a nested multi-signature for a Batch transaction on a JTx. */
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
