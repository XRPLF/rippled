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

class RepoAcceptBuilder;

/**
 * @brief Transaction: RepoAccept
 *
 * Type: ttREPO_ACCEPT (115)
 * Delegable: Delegation::Delegable
 * Amendment: featureRepo
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use RepoAcceptBuilder to construct new transactions.
 */
class RepoAccept : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttREPO_ACCEPT;

    /**
     * @brief Construct a RepoAccept transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit RepoAccept(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for RepoAccept");
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
 * @brief Builder for RepoAccept transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class RepoAcceptBuilder : public TransactionBuilderBase<RepoAcceptBuilder>
{
public:
    /**
     * @brief Construct a new RepoAcceptBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param repoID The sfRepoID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    RepoAcceptBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& repoID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<RepoAcceptBuilder>(ttREPO_ACCEPT, account, sequence, fee)
    {
        setRepoID(repoID);
    }

    /**
     * @brief Construct a RepoAcceptBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    RepoAcceptBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttREPO_ACCEPT)
        {
            throw std::runtime_error("Invalid transaction type for RepoAcceptBuilder");
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
    RepoAcceptBuilder&
    setRepoID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfRepoID] = value;
        return *this;
    }

    /**
     * @brief Build and return the RepoAccept wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    RepoAccept
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return RepoAccept{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
