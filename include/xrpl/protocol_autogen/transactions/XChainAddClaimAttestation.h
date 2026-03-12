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

class XChainAddClaimAttestationBuilder;

/**
 * @brief Transaction: XChainAddClaimAttestation
 *
 * Type: ttXCHAIN_ADD_CLAIM_ATTESTATION (45)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: createAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainAddClaimAttestationBuilder to construct new transactions.
 */
class XChainAddClaimAttestation : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_ADD_CLAIM_ATTESTATION;

    /**
     * @brief Construct a XChainAddClaimAttestation transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAddClaimAttestation(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddClaimAttestation");
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
     * @brief Get sfAttestationSignerAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAttestationSignerAccount() const
    {
        return this->tx_->at(sfAttestationSignerAccount);
    }

    /**
     * @brief Get sfPublicKey (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->tx_->at(sfPublicKey);
    }

    /**
     * @brief Get sfSignature (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getSignature() const
    {
        return this->tx_->at(sfSignature);
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
     * @brief Get sfAttestationRewardAccount (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAttestationRewardAccount() const
    {
        return this->tx_->at(sfAttestationRewardAccount);
    }

    /**
     * @brief Get sfWasLockingChainSend (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getWasLockingChainSend() const
    {
        return this->tx_->at(sfWasLockingChainSend);
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
     * @brief Get sfDestination (soeOPTIONAL)
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
};

/**
 * @brief Builder for XChainAddClaimAttestation transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAddClaimAttestationBuilder : public TransactionBuilderBase<XChainAddClaimAttestationBuilder>
{
public:
    /**
     * @brief Construct a new XChainAddClaimAttestationBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param xChainBridge The sfXChainBridge field value.
     * @param attestationSignerAccount The sfAttestationSignerAccount field value.
     * @param publicKey The sfPublicKey field value.
     * @param signature The sfSignature field value.
     * @param otherChainSource The sfOtherChainSource field value.
     * @param amount The sfAmount field value.
     * @param attestationRewardAccount The sfAttestationRewardAccount field value.
     * @param wasLockingChainSend The sfWasLockingChainSend field value.
     * @param xChainClaimID The sfXChainClaimID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainAddClaimAttestationBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& attestationSignerAccount,                     std::decay_t<typename SF_VL::type::value_type> const& publicKey,                     std::decay_t<typename SF_VL::type::value_type> const& signature,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& otherChainSource,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& attestationRewardAccount,                     std::decay_t<typename SF_UINT8::type::value_type> const& wasLockingChainSend,                     std::decay_t<typename SF_UINT64::type::value_type> const& xChainClaimID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainAddClaimAttestationBuilder>(ttXCHAIN_ADD_CLAIM_ATTESTATION, account, sequence, fee)
    {
        setXChainBridge(xChainBridge);
        setAttestationSignerAccount(attestationSignerAccount);
        setPublicKey(publicKey);
        setSignature(signature);
        setOtherChainSource(otherChainSource);
        setAmount(amount);
        setAttestationRewardAccount(attestationRewardAccount);
        setWasLockingChainSend(wasLockingChainSend);
        setXChainClaimID(xChainClaimID);
    }

    /**
     * @brief Construct a XChainAddClaimAttestationBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainAddClaimAttestationBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_ADD_CLAIM_ATTESTATION)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddClaimAttestationBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfAttestationSignerAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAttestationSignerAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationSignerAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfSignature (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setSignature(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSignature] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAttestationRewardAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAttestationRewardAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationRewardAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfWasLockingChainSend (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setWasLockingChainSend(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWasLockingChainSend] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainAddClaimAttestation wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainAddClaimAttestation
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainAddClaimAttestation{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
