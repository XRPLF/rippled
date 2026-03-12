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

class PermissionedDomainSetBuilder;

/**
 * @brief Transaction: PermissionedDomainSet
 *
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
     * @brief Construct a PermissionedDomainSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PermissionedDomainSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDomainID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_->at(sfDomainID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }
    /**
     * @brief Get sfAcceptedCredentials (soeREQUIRED)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getAcceptedCredentials() const
    {
        return this->tx_->getFieldArray(sfAcceptedCredentials);
    }
};

/**
 * @brief Builder for PermissionedDomainSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PermissionedDomainSetBuilder : public TransactionBuilderBase<PermissionedDomainSetBuilder>
{
public:
    /**
     * @brief Construct a new PermissionedDomainSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param acceptedCredentials The sfAcceptedCredentials field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PermissionedDomainSetBuilder(SF_ACCOUNT::type::value_type account,
                     STArray const& acceptedCredentials,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PermissionedDomainSetBuilder>(ttPERMISSIONED_DOMAIN_SET, account, sequence, fee)
    {
        setAcceptedCredentials(acceptedCredentials);
    }

    /**
     * @brief Construct a PermissionedDomainSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PermissionedDomainSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPERMISSIONED_DOMAIN_SET)
        {
            throw std::runtime_error("Invalid transaction type for PermissionedDomainSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainSetBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfAcceptedCredentials (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    PermissionedDomainSetBuilder&
    setAcceptedCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAcceptedCredentials, value);
        return *this;
    }

    /**
     * @brief Build and return the PermissionedDomainSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PermissionedDomainSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PermissionedDomainSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
