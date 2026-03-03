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
class XChainAddClaimAttestationBuilder;

/**
 * Transaction: XChainAddClaimAttestation
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
     * Construct a XChainAddClaimAttestation transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit XChainAddClaimAttestation(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddClaimAttestation");
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
     * Get sfXChainClaimID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getXChainClaimID() const
    {
        return this->tx_.at(sfXChainClaimID);
    }

    /**
     * Get sfDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_.at(sfDestination);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_.isFieldPresent(sfDestination);
    }
};

/**
 * Builder for XChainAddClaimAttestation transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class XChainAddClaimAttestationBuilder : public TransactionBuilderBase<XChainAddClaimAttestationBuilder>
{
public:
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

    XChainAddClaimAttestationBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttXCHAIN_ADD_CLAIM_ATTESTATION)
        {
            throw std::runtime_error("Invalid transaction type for XChainAddClaimAttestationBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfXChainBridge (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setXChainBridge(std::decay_t<typename SF_XCHAIN_BRIDGE::type::value_type> const& value)
    {
        object_[sfXChainBridge] = value;
        return *this;
    }

    /**
     * Set sfAttestationSignerAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAttestationSignerAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationSignerAccount] = value;
        return *this;
    }

    /**
     * Set sfPublicKey (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfPublicKey] = value;
        return *this;
    }

    /**
     * Set sfSignature (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setSignature(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfSignature] = value;
        return *this;
    }

    /**
     * Set sfOtherChainSource (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setOtherChainSource(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOtherChainSource] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfAttestationRewardAccount (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setAttestationRewardAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAttestationRewardAccount] = value;
        return *this;
    }

    /**
     * Set sfWasLockingChainSend (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setWasLockingChainSend(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWasLockingChainSend] = value;
        return *this;
    }

    /**
     * Set sfXChainClaimID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setXChainClaimID(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfXChainClaimID] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    XChainAddClaimAttestationBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Build and return the completed XChainAddClaimAttestation wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    XChainAddClaimAttestation
    build()
    {
        return XChainAddClaimAttestation(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
