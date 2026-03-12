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

// Forward declaration
class XChainModifyBridgeBuilder;

/**
 * Transaction: XChainModifyBridge
 * Type: ttXCHAIN_MODIFY_BRIDGE (47)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainModifyBridgeBuilder to construct new transactions.
 */
class XChainModifyBridge : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_MODIFY_BRIDGE;

    /**
     * Construct a XChainModifyBridge transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainModifyBridge(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainModifyBridge");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfXChainBridge (soeREQUIRED)
     */
    [[nodiscard]]
    SF_XCHAIN_BRIDGE::type::value_type
    getXChainBridge() const
    {
        return this->tx_->at(sfXChainBridge);
    }

    /**
     * Get sfSignatureReward (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasSignatureReward() const
    {
        return this->tx_->isFieldPresent(sfSignatureReward);
    }

    /**
     * Get sfMinAccountCreateAmount (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasMinAccountCreateAmount() const
    {
        return this->tx_->isFieldPresent(sfMinAccountCreateAmount);
    }
};

/**
 * Builder for XChainModifyBridge transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainModifyBridgeBuilder : public TransactionBuilderBase<XChainModifyBridgeBuilder>
{
public:
    XChainModifyBridgeBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainModifyBridgeBuilder>(ttXCHAIN_MODIFY_BRIDGE, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
    }

    XChainModifyBridgeBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_MODIFY_BRIDGE)
        {
            throw std::runtime_error("Invalid transaction type for XChainModifyBridgeBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainModifyBridgeBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainModifyBridgeBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Set sfMinAccountCreateAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainModifyBridgeBuilder&
    setMinAccountCreateAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMinAccountCreateAmount] = value;
        return *this;
    }

    /**
     * Build and return the XChainModifyBridge wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    XChainModifyBridge
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainModifyBridge{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
