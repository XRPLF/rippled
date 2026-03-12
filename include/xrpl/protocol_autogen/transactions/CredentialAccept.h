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

class CredentialAcceptBuilder;

/**
 * @brief Transaction: CredentialAccept
 *
 * Type: ttCREDENTIAL_ACCEPT (59)
 * Delegable: Delegation::delegable
 * Amendment: featureCredentials
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use CredentialAcceptBuilder to construct new transactions.
 */
class CredentialAccept : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttCREDENTIAL_ACCEPT;

    /**
     * @brief Construct a CredentialAccept transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit CredentialAccept(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for CredentialAccept");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfIssuer (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->tx_->at(sfIssuer);
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
 * @brief Builder for CredentialAccept transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class CredentialAcceptBuilder : public TransactionBuilderBase<CredentialAcceptBuilder>
{
public:
    /**
     * @brief Construct a new CredentialAcceptBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param issuer The sfIssuer field value.
     * @param credentialType The sfCredentialType field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    CredentialAcceptBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& issuer,                     std::decay_t<typename SF_VL::type::value_type> const& credentialType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<CredentialAcceptBuilder>(ttCREDENTIAL_ACCEPT, account, sequence, fee)
    {
        setIssuer(issuer);
        setCredentialType(credentialType);
    }

    /**
     * @brief Construct a CredentialAcceptBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    CredentialAcceptBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttCREDENTIAL_ACCEPT)
        {
            throw std::runtime_error("Invalid transaction type for CredentialAcceptBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfIssuer (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialAcceptBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialType (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    CredentialAcceptBuilder&
    setCredentialType(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfCredentialType] = value;
        return *this;
    }

    /**
     * @brief Build and return the CredentialAccept wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    CredentialAccept
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return CredentialAccept{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
