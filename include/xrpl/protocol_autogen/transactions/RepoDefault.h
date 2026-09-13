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

class RepoDefaultBuilder;

/**
 * @brief Transaction: RepoDefault
 *
 * Type: ttREPO_DEFAULT (118)
 * Delegable: Delegation::Delegable
 * Amendment: featureRepo
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use RepoDefaultBuilder to construct new transactions.
 */
class RepoDefault : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttREPO_DEFAULT;

    /**
     * @brief Construct a RepoDefault transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit RepoDefault(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for RepoDefault");
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
 * @brief Builder for RepoDefault transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class RepoDefaultBuilder : public TransactionBuilderBase<RepoDefaultBuilder>
{
public:
    /**
     * @brief Construct a new RepoDefaultBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param repoID The sfRepoID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    RepoDefaultBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& repoID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<RepoDefaultBuilder>(ttREPO_DEFAULT, account, sequence, fee)
    {
        setRepoID(repoID);
    }

    /**
     * @brief Construct a RepoDefaultBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    RepoDefaultBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttREPO_DEFAULT)
        {
            throw std::runtime_error("Invalid transaction type for RepoDefaultBuilder");
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
    RepoDefaultBuilder&
    setRepoID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfRepoID] = value;
        return *this;
    }

    /**
     * @brief Build and return the RepoDefault wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    RepoDefault
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return RepoDefault{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
