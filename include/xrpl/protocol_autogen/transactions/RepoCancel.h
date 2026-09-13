// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

class RepoCancelBuilder;

/**
 * @brief Transaction: RepoCancel
 *
 * Type: ttREPO_CANCEL (116)
 * Delegable: Delegation::Delegable
 * Amendment: featureRepo
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use RepoCancelBuilder to construct new transactions.
 */
class RepoCancel : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttREPO_CANCEL;

    /**
     * @brief Construct a RepoCancel transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit RepoCancel(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for RepoCancel");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfRepoID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getRepoID() const
    {
        return this->tx_->at(sfRepoID);
    }
};

/**
 * @brief Builder for RepoCancel transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class RepoCancelBuilder : public TransactionBuilderBase<RepoCancelBuilder>
{
public:
    /**
     * @brief Construct a new RepoCancelBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param repoID The sfRepoID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    RepoCancelBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& repoID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<RepoCancelBuilder>(ttREPO_CANCEL, account, sequence, fee)
    {
        setRepoID(repoID);
    }

    /**
     * @brief Construct a RepoCancelBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    RepoCancelBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttREPO_CANCEL)
        {
            throw std::runtime_error("Invalid transaction type for RepoCancelBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfRepoID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCancelBuilder&
    setRepoID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfRepoID] = value;
        return *this;
    }

    /**
     * @brief Build and return the RepoCancel wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    RepoCancel
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return RepoCancel{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
