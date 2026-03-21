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

class XChainCommitBuilder;

/**
 * @brief Transaction: XChainCommit
 *
 * Type: ttXCHAIN_COMMIT (42)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainCommitBuilder to construct new transactions.
 */
class XChainCommit : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_COMMIT;

    /**
     * @brief Construct a XChainCommit transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCommit(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCommit");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfXChainBridge (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->tx_->at(sfXChainBridge);
    }

    /**
     * @brief Get sfXChainClaimID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->tx_->at(sfXChainClaimID);
    }

    /**
     * @brief Get sfAmount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_->at(sfAmount);
    }

    /**
     * @brief Get sfOtherChainDestination (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOtherChainDestination() const
    {
        if (hasOtherChainDestination())
        {
            return this->tx_->at(sfOtherChainDestination);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOtherChainDestination is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOtherChainDestination() const
    {
        return this->tx_->isFieldPresent(sfOtherChainDestination);
    }
};

/**
 * @brief Builder for XChainCommit transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCommitBuilder : public TransactionBuilderBase<XChainCommitBuilder>
{
public:
    /**
     * @brief Construct a new XChainCommitBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param xChainBridge The sfXChainBridge field value.
     * @param xChainClaimID The sfXChainClaimID field value.
     * @param amount The sfAmount field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainCommitBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_UINT64::type::value_type> const& xChainClaimID,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainCommitBuilder>(ttXCHAIN_COMMIT, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setXChainClaimID(xChainClaimID);
        setAmount(amount);
    }

    /**
     * @brief Construct a XChainCommitBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainCommitBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_COMMIT)
        {
            throw std::runtime_error("Invalid transaction type for XChainCommitBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainCommitBuilder&
    setOtherChainDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainDestination] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainCommit wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainCommit
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainCommit{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
