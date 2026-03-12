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

class XChainCreateBridgeBuilder;

/**
 * @brief Transaction: XChainCreateBridge
 *
 * Type: ttXCHAIN_CREATE_BRIDGE (48)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainCreateBridgeBuilder to construct new transactions.
 */
class XChainCreateBridge : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_CREATE_BRIDGE;

    /**
     * @brief Construct a XChainCreateBridge transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCreateBridge(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateBridge");
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
     * @brief Get sfSignatureReward (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->tx_->at(sfSignatureReward);
    }

    /**
     * @brief Get sfMinAccountCreateAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getMinAccountCreateAmount() const
    {
        if (hasMinAccountCreateAmount())
        {
            return this->tx_->at(sfMinAccountCreateAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMinAccountCreateAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMinAccountCreateAmount() const
    {
        return this->tx_->isFieldPresent(sfMinAccountCreateAmount);
    }
};

/**
 * @brief Builder for XChainCreateBridge transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCreateBridgeBuilder : public TransactionBuilderBase<XChainCreateBridgeBuilder>
{
public:
    /**
     * @brief Construct a new XChainCreateBridgeBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param xChainBridge The sfXChainBridge field value.
     * @param signatureReward The sfSignatureReward field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainCreateBridgeBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainCreateBridgeBuilder>(ttXCHAIN_CREATE_BRIDGE, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setSignatureReward(signatureReward);
    }

    /**
     * @brief Construct a XChainCreateBridgeBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainCreateBridgeBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_CREATE_BRIDGE)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateBridgeBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Set sfMinAccountCreateAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setMinAccountCreateAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMinAccountCreateAmount] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainCreateBridge wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainCreateBridge
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainCreateBridge{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
