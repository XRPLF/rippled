#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class XChainCreateBridgeBuilder;

/**
 * Transaction: XChainCreateBridge
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
     * Construct a XChainCreateBridge transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCreateBridge(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateBridge");
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
        return this->tx_.at(sfXChainBridge);
    }

    /**
     * Get sfSignatureReward (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getSignatureReward() const
    {
        return this->tx_.at(sfSignatureReward);
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
            return this->tx_.at(sfMinAccountCreateAmount);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMinAccountCreateAmount() const
    {
        return this->tx_.isFieldPresent(sfMinAccountCreateAmount);
    }
};

/**
 * Builder for XChainCreateBridge transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCreateBridgeBuilder : public TransactionBuilderBase<XChainCreateBridgeBuilder>
{
public:
    XChainCreateBridgeBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward)
        : TransactionBuilderBase<XChainCreateBridgeBuilder>(account, sequence, fee, signingPubKey, ttXCHAIN_CREATE_BRIDGE)
    {
        setXChainBridge(xChainBridge);
        setSignatureReward(signatureReward);
    }

    XChainCreateBridgeBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_CREATE_BRIDGE)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateBridgeBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Set sfMinAccountCreateAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateBridgeBuilder&
    setMinAccountCreateAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfMinAccountCreateAmount] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainCreateBridge wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    XChainCreateBridge
    build()
    {
        return XChainCreateBridge(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions