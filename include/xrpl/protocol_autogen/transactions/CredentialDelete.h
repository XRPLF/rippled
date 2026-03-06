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
class CredentialDeleteBuilder;

/**
 * Transaction: CredentialDelete
 * Type: ttCREDENTIAL_DELETE (60)
 * Delegable: Delegation::delegable
 * Amendment: featureCredentials
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CredentialDeleteBuilder to construct new transactions.
 */
class CredentialDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCREDENTIAL_DELETE;

    /**
     * Construct a CredentialDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CredentialDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CredentialDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfSubject (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getSubject() const
    {
        if (hasSubject())
        {
            return this->tx_.at(sfSubject);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSubject() const
    {
        return this->tx_.isFieldPresent(sfSubject);
    }

    /**
     * Get sfIssuer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getIssuer() const
    {
        if (hasIssuer())
        {
            return this->tx_.at(sfIssuer);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasIssuer() const
    {
        return this->tx_.isFieldPresent(sfIssuer);
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
};

/**
 * Builder for CredentialDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CredentialDeleteBuilder : public TransactionBuilderBase<CredentialDeleteBuilder>
{
public:
    CredentialDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_VL::type::value_type> const& credentialType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CredentialDeleteBuilder>(ttCREDENTIAL_DELETE, account, sequence, fee)
    {
        setCredentialType(credentialType);
    }

    CredentialDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttCREDENTIAL_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for CredentialDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfSubject (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setSubject(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSubject] = value;
        return *this;
    }

    /**
     * Set sfIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * Build and return the completed CredentialDelete wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, CredentialDelete>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, CredentialDelete>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
