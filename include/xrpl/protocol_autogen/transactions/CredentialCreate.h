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
class CredentialCreateBuilder;

/**
 * Transaction: CredentialCreate
 * Type: ttCREDENTIAL_CREATE (58)
 * Delegable: Delegation::delegable
 * Amendment: featureCredentials
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CredentialCreateBuilder to construct new transactions.
 */
class CredentialCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCREDENTIAL_CREATE;

    /**
     * Construct a CredentialCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CredentialCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CredentialCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfSubject (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getSubject() const
    {
        return this->tx_.at(sfSubject);
    }

    /**
     * Get sfCredentialType (soeREQUIRED)
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getCredentialType() const
    {
        return this->tx_.at(sfCredentialType);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_.at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_.isFieldPresent(sfExpiration);
    }

    /**
     * Get sfURI (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
        {
            return this->tx_.at(sfURI);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_.isFieldPresent(sfURI);
    }
};

/**
 * Builder for CredentialCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CredentialCreateBuilder : public TransactionBuilderBase<CredentialCreateBuilder>
{
public:
    CredentialCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& subject,                     std::decay_t<typename SF_VL::type::value_type> const& credentialType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CredentialCreateBuilder>(ttCREDENTIAL_CREATE, account, sequence, fee)
    {
        setSubject(subject);
        setCredentialType(credentialType);
    }

    CredentialCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCREDENTIAL_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for CredentialCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfSubject (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialCreateBuilder&
    setSubject(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSubject] = value;
        return *this;
    }

    /**
     * Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialCreateBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialCreateBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * Build and return the completed CredentialCreate wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, CredentialCreate>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, CredentialCreate>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
