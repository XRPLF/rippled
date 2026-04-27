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

class DepositPreauthBuilder;

/**
 * @brief Transaction: DepositPreauth
 *
 * Type: ttDEPOSIT_PREAUTH (19)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DepositPreauthBuilder to construct new transactions.
 */
class DepositPreauth : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDEPOSIT_PREAUTH;

    /**
     * @brief Construct a DepositPreauth transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DepositPreauth(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DepositPreauth");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAuthorize (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAuthorize() const
    {
        if (hasAuthorize())
        {
            return this->tx_->at(sfAuthorize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuthorize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuthorize() const
    {
        return this->tx_->isFieldPresent(sfAuthorize);
    }

    /**
     * @brief Get sfUnauthorize (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getUnauthorize() const
    {
        if (hasUnauthorize())
        {
            return this->tx_->at(sfUnauthorize);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfUnauthorize is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasUnauthorize() const
    {
        return this->tx_->isFieldPresent(sfUnauthorize);
    }
    /**
     * @brief Get sfAuthorizeCredentials (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuthorizeCredentials() const
    {
        if (this->tx_->isFieldPresent(sfAuthorizeCredentials))
            return this->tx_->getFieldArray(sfAuthorizeCredentials);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuthorizeCredentials is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuthorizeCredentials() const
    {
        return this->tx_->isFieldPresent(sfAuthorizeCredentials);
    }
    /**
     * @brief Get sfUnauthorizeCredentials (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getUnauthorizeCredentials() const
    {
        if (this->tx_->isFieldPresent(sfUnauthorizeCredentials))
            return this->tx_->getFieldArray(sfUnauthorizeCredentials);
        return std::nullopt;
    }

    /**
     * @brief Check if sfUnauthorizeCredentials is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasUnauthorizeCredentials() const
    {
        return this->tx_->isFieldPresent(sfUnauthorizeCredentials);
    }
};

/**
 * @brief Builder for DepositPreauth transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DepositPreauthBuilder : public TransactionBuilderBase<DepositPreauthBuilder>
{
public:
    /**
     * @brief Construct a new DepositPreauthBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    DepositPreauthBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DepositPreauthBuilder>(ttDEPOSIT_PREAUTH, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a DepositPreauthBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    DepositPreauthBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttDEPOSIT_PREAUTH)
        {
            throw std::runtime_error("Invalid transaction type for DepositPreauthBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAuthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfUnauthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setUnauthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfUnauthorize] = value;
        return *this;
    }

    /**
     * @brief Set sfAuthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAuthorizeCredentials, value);
        return *this;
    }

    /**
     * @brief Set sfUnauthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setUnauthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfUnauthorizeCredentials, value);
        return *this;
    }

    /**
     * @brief Build and return the DepositPreauth wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    DepositPreauth
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return DepositPreauth{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
