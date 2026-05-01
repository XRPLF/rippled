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

class XChainAddAccountCreateAttestationBuilder;

/**
 * @brief Transaction: XChainAddAccountCreateAttestation
 *
 * Type: ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION (46)
 * Delegable: Delegation::Delegable
 * Amendment: featureXChainBridge
 * Privileges: CreateAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainAddAccountCreateAttestationBuilder to construct new transactions.
 */
class XChainAddAccountCreateAttestation : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION;

    /**
     * @brief Construct a XChainAddAccountCreateAttestation transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAddAccountCreateAttestation(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddAccountCreateAttestation");
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
     * @brief Get sfAttestationSignerAccount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAttestationSignerAccount() const
    {
        if (hasAttestationSignerAccount())
        {
            return this->tx_->at(sfAttestationSignerAccount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAttestationSignerAccount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAttestationSignerAccount() const
    {
        return this->tx_->isFieldPresent(sfAttestationSignerAccount);
    }

    /**
     * @brief Get sfPublicKey (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getPublicKey() const
    {
        if (hasPublicKey())
        {
            return this->tx_->at(sfPublicKey);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPublicKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPublicKey() const
    {
        return this->tx_->isFieldPresent(sfPublicKey);
    }

    /**
     * @brief Get sfSignature (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getSignature() const
    {
        if (hasSignature())
        {
            return this->tx_->at(sfSignature);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSignature is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSignature() const
    {
        return this->tx_->isFieldPresent(sfSignature);
    }

    /**
     * @brief Get sfOtherChainSource (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOtherChainSource() const
    {
        if (hasOtherChainSource())
        {
            return this->tx_->at(sfOtherChainSource);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOtherChainSource is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOtherChainSource() const
    {
        return this->tx_->isFieldPresent(sfOtherChainSource);
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
     * @brief Get sfAttestationRewardAccount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAttestationRewardAccount() const
    {
        if (hasAttestationRewardAccount())
        {
            return this->tx_->at(sfAttestationRewardAccount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAttestationRewardAccount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAttestationRewardAccount() const
    {
        return this->tx_->isFieldPresent(sfAttestationRewardAccount);
    }

    /**
     * @brief Get sfWasLockingChainSend (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getWasLockingChainSend() const
    {
        if (hasWasLockingChainSend())
        {
            return this->tx_->at(sfWasLockingChainSend);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfWasLockingChainSend is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWasLockingChainSend() const
    {
        return this->tx_->isFieldPresent(sfWasLockingChainSend);
    }

    /**
     * @brief Get sfXChainAccountCreateCount (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getXChainAccountCreateCount() const
    {
        if (hasXChainAccountCreateCount())
        {
            return this->tx_->at(sfXChainAccountCreateCount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfXChainAccountCreateCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasXChainAccountCreateCount() const
    {
        return this->tx_->isFieldPresent(sfXChainAccountCreateCount);
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
 * @brief Builder for XChainAddAccountCreateAttestation transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAddAccountCreateAttestationBuilder : public TransactionBuilderBase<XChainAddAccountCreateAttestationBuilder>
{
public:
    /**
     * @brief Construct a new XChainAddAccountCreateAttestationBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    XChainAddAccountCreateAttestationBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<XChainAddAccountCreateAttestationBuilder>(ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a XChainAddAccountCreateAttestationBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    XChainAddAccountCreateAttestationBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddAccountCreateAttestationBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfXChainBridge (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * @brief Set sfAttestationSignerAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAttestationSignerAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationSignerAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfPublicKey (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfSignature (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setSignature(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSignature] = value;
        return *this;
    }

    /**
     * @brief Set sfOtherChainSource (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * @brief Set sfAmount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfAttestationRewardAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAttestationRewardAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationRewardAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfWasLockingChainSend (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setWasLockingChainSend(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWasLockingChainSend] = value;
        return *this;
    }

    /**
     * @brief Set sfXChainAccountCreateCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * @brief Set sfDestination (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfSignatureReward (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * @brief Build and return the XChainAddAccountCreateAttestation wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    XChainAddAccountCreateAttestation
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return XChainAddAccountCreateAttestation{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
