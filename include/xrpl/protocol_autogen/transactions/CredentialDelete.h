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

class CredentialDeleteBuilder;

/**
 * @brief Transaction: CredentialDelete
 *
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
     * @brief Construct a CredentialDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CredentialDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CredentialDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfSubject (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getSubject() const
    {
        if (hasSubject())
        {
            return this->tx_->at(sfSubject);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSubject is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSubject() const
    {
        return this->tx_->isFieldPresent(sfSubject);
    }

    /**
     * @brief Get sfIssuer (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getIssuer() const
    {
        if (hasIssuer())
        {
            return this->tx_->at(sfIssuer);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuer is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuer() const
    {
        return this->tx_->isFieldPresent(sfIssuer);
    }

    /**
     * @brief Get sfCredentialType (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getCredentialType() const
    {
        return this->tx_->at(sfCredentialType);
    }
};

/**
 * @brief Builder for CredentialDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CredentialDeleteBuilder : public TransactionBuilderBase<CredentialDeleteBuilder>
{
public:
    /**
     * @brief Construct a new CredentialDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param credentialType The sfCredentialType field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CredentialDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_VL::type::value_type> const& credentialType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CredentialDeleteBuilder>(ttCREDENTIAL_DELETE, account, sequence, fee)
    {
        setCredentialType(credentialType);
    }

    /**
     * @brief Construct a CredentialDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CredentialDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCREDENTIAL_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for CredentialDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfSubject (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setSubject(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSubject] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialDeleteBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * @brief Build and return the CredentialDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CredentialDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CredentialDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
