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

class XChainCreateClaimIDBuilder;

/**
 * @brief Transaction: XChainCreateClaimID
 *
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
     * @brief Construct a XChainCreateClaimID transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainCreateClaimID(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateClaimID");
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
     * @brief Get sfOtherChainSource (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOtherChainSource() const
    {
        return this->tx_->at(sfOtherChainSource);
    }
};

/**
 * @brief Builder for XChainCreateClaimID transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainCreateClaimIDBuilder : public TransactionBuilderBase<XChainCreateClaimIDBuilder>
{
public:
    /**
     * @brief Construct a new XChainCreateClaimIDBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param xChainBridge The sfXChainBridge field value.
     * @param signatureReward The sfSignatureReward field value.
     * @param otherChainSource The sfOtherChainSource field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainCreateClaimIDBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& otherChainSource,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainCreateClaimIDBuilder>(ttXCHAIN_CREATE_CLAIM_ID, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setSignatureReward(signatureReward);
        setOtherChainSource(otherChainSource);
    }

    /**
     * @brief Construct a XChainCreateClaimIDBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainCreateClaimIDBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_CREATE_CLAIM_ID)
        {
            throw std::runtime_error("Invalid transaction type for XChainCreateClaimIDBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainCreateClaimIDBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainCreateClaimID wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainCreateClaimID
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainCreateClaimID{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
