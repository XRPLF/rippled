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

class XChainAccountCreateCommitBuilder;

/**
 * @brief Transaction: XChainAccountCreateCommit
 *
 * Type: ttXCHAIN_ACCOUNT_CREATE_COMMIT (44)
 * Delegable: Delegation::Delegable
 * Amendment: featureXChainBridge
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainAccountCreateCommitBuilder to construct new transactions.
 */
class XChainAccountCreateCommit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_ACCOUNT_CREATE_COMMIT;

    /**
     * @brief Construct a XChainAccountCreateCommit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAccountCreateCommit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAccountCreateCommit");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfXChainBridge (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_XCHAIN_BRIDGE::type::value_type>
    getXChainBridge() const
    {
        if (hasXChainBridge())
        {
            return this->tx_->at(sfXChainBridge);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainBridge is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainBridge() const
    {
        return this->tx_->isFieldPresent(sfXChainBridge);
    }

    /**
     * @brief Get sfDestination (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_->at(sfDestination);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestination is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_->isFieldPresent(sfDestination);
    }

    /**
     * @brief Get sfAmount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }

    /**
     * @brief Get sfSignatureReward (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getSignatureReward() const
    {
        if (hasSignatureReward())
        {
            return this->tx_->at(sfSignatureReward);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSignatureReward is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSignatureReward() const
    {
        return this->tx_->isFieldPresent(sfSignatureReward);
    }
};

/**
 * @brief Builder for XChainAccountCreateCommit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAccountCreateCommitBuilder : public TransactionBuilderBase<XChainAccountCreateCommitBuilder>
{
public:
    /**
     * @brief Construct a new XChainAccountCreateCommitBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainAccountCreateCommitBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainAccountCreateCommitBuilder>(ttXCHAIN_ACCOUNT_CREATE_COMMIT, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a XChainAccountCreateCommitBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainAccountCreateCommitBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_ACCOUNT_CREATE_COMMIT)
        {
            throw std::runtime_error("Invalid transaction type for XChainAccountCreateCommitBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAccountCreateCommitBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainAccountCreateCommit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainAccountCreateCommit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainAccountCreateCommit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
