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
class XChainCreateClaimIDBuilder;

/**
 * Transaction: XChainCreateClaimID
 * Type: ttXCHAIN_CREATE_CLAIM_ID (41)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainCreateClaimIDBuilder to construct new transactions.
 */
class XChainCreateClaimID : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_CREATE_CLAIM_ID;

    /**
     * Construct a XChainCreateClaimID transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCreateClaimID(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateClaimID");
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
     * Get sfOtherChainSource (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOtherChainSource() const
    {
        return this->tx_.at(sfOtherChainSource);
    }
};

/**
 * Builder for XChainCreateClaimID transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCreateClaimIDBuilder : public TransactionBuilderBase<XChainCreateClaimIDBuilder>
{
public:
    XChainCreateClaimIDBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& otherChainSource)
        : TransactionBuilderBase<XChainCreateClaimIDBuilder>(account, sequence, fee, signingPubKey, ttXCHAIN_CREATE_CLAIM_ID)
    {
        setXChainBridge(xChainBridge);
        setSignatureReward(signatureReward);
        setOtherChainSource(otherChainSource);
    }

    XChainCreateClaimIDBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_CREATE_CLAIM_ID)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateClaimIDBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainCreateClaimID wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    XChainCreateClaimID
    build()
    {
        return XChainCreateClaimID(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions