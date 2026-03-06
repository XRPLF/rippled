// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class PermissionedDomainSetBuilder;

/**
 * Transaction: PermissionedDomainSet
 * Type: ttPERMISSIONED_DOMAIN_SET (62)
 * Delegable: Delegation::delegable
 * Amendment: featurePermissionedDomains
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PermissionedDomainSetBuilder to construct new transactions.
 */
class PermissionedDomainSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPERMISSIONED_DOMAIN_SET;

    /**
     * Construct a PermissionedDomainSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PermissionedDomainSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_.at(sfDomainID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_.isFieldPresent(sfDomainID);
    }
    /**
     * Get sfAcceptedCredentials (soeREQUIRED)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    STArray const&
    getAcceptedCredentials() const
    {
        return this->tx_.getFieldArray(sfAcceptedCredentials);
    }
};

/**
 * Builder for PermissionedDomainSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PermissionedDomainSetBuilder : public TransactionBuilderBase<PermissionedDomainSetBuilder>
{
public:
    PermissionedDomainSetBuilder(SF_ACCOUNT::type::value_type account,
                     STArray const& acceptedCredentials,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PermissionedDomainSetBuilder>(ttPERMISSIONED_DOMAIN_SET, account, sequence, fee)
    {
        setAcceptedCredentials(acceptedCredentials);
    }

    PermissionedDomainSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttPERMISSIONED_DOMAIN_SET)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainSetBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfAcceptedCredentials (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainSetBuilder&
    setAcceptedCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAcceptedCredentials, value);
        return *this;
    }

    /**
     * Build and return the completed PermissionedDomainSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, PermissionedDomainSet>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, PermissionedDomainSet>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
