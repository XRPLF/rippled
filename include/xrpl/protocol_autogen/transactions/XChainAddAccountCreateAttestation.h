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
class XChainAddAccountCreateAttestationBuilder;

/**
 * Transaction: XChainAddAccountCreateAttestation
 * Type: ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION (46)
 * Delegable: Delegation::delegable
 * Amendment: featureXChainBridge
 * Privileges: createAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use XChainAddAccountCreateAttestationBuilder to construct new transactions.
 */
class XChainAddAccountCreateAttestation : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION;

    /**
     * Construct a XChainAddAccountCreateAttestation transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAddAccountCreateAttestation(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddAccountCreateAttestation");
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
     * Get sfAttestationSignerAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAttestationSignerAccount() const
    {
        return this->tx_.at(sfAttestationSignerAccount);
    }

    /**
     * Get sfPublicKey (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getPublicKey() const
    {
        return this->tx_.at(sfPublicKey);
    }

    /**
     * Get sfSignature (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getSignature() const
    {
        return this->tx_.at(sfSignature);
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

    /**
     * Get sfAmount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfAttestationRewardAccount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAttestationRewardAccount() const
    {
        return this->tx_.at(sfAttestationRewardAccount);
    }

    /**
     * Get sfWasLockingChainSend (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getWasLockingChainSend() const
    {
        return this->tx_.at(sfWasLockingChainSend);
    }

    /**
     * Get sfXChainAccountCreateCount (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainAccountCreateCount() const
    {
        return this->tx_.at(sfXChainAccountCreateCount);
    }

    /**
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_.at(sfDestination);
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
};

/**
 * Builder for XChainAddAccountCreateAttestation transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAddAccountCreateAttestationBuilder : public TransactionBuilderBase<XChainAddAccountCreateAttestationBuilder>
{
public:
    XChainAddAccountCreateAttestationBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& xChainBridge,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& attestationSignerAccount,
                     std::decay_t<typename SF_VL::type::value_type> const& publicKey,
                     std::decay_t<typename SF_VL::type::value_type> const& signature,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& otherChainSource,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& attestationRewardAccount,
                     std::decay_t<typename SF_UINT8::type::value_type> const& wasLockingChainSend,
                     std::decay_t<typename SF_UINT64::type::value_type> const& xChainAccountCreateCount,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& signatureReward)
        : TransactionBuilderBase<XChainAddAccountCreateAttestationBuilder>(account, sequence, fee, signingPubKey, ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION)
    {
        setXChainBridge(xChainBridge);
        setAttestationSignerAccount(attestationSignerAccount);
        setPublicKey(publicKey);
        setSignature(signature);
        setOtherChainSource(otherChainSource);
        setAmount(amount);
        setAttestationRewardAccount(attestationRewardAccount);
        setWasLockingChainSend(wasLockingChainSend);
        setXChainAccountCreateCount(xChainAccountCreateCount);
        setDestination(destination);
        setSignatureReward(signatureReward);
    }

    XChainAddAccountCreateAttestationBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddAccountCreateAttestationBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfAttestationSignerAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAttestationSignerAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationSignerAccount] = value;
        return *this;
    }

    /**
     * Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * Set sfSignature (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setSignature(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSignature] = value;
        return *this;
    }

    /**
     * Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfAttestationRewardAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setAttestationRewardAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationRewardAccount] = value;
        return *this;
    }

    /**
     * Set sfWasLockingChainSend (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setWasLockingChainSend(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWasLockingChainSend] = value;
        return *this;
    }

    /**
     * Set sfXChainAccountCreateCount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setXChainAccountCreateCount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainAccountCreateCount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfSignatureReward (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddAccountCreateAttestationBuilder&
    setSignatureReward(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfSignatureReward] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainAddAccountCreateAttestation wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    XChainAddAccountCreateAttestation
    build()
    {
        return XChainAddAccountCreateAttestation(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions