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

class TokenIssuanceSetBuilder;

/**
 * @brief Transaction: TokenIssuanceSet
 *
 * Type: ttTOKEN_ISSUANCE_SET (105)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureTokenIssuance
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use TokenIssuanceSetBuilder to construct new transactions.
 */
class TokenIssuanceSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttTOKEN_ISSUANCE_SET;

    /**
     * @brief Construct a TokenIssuanceSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit TokenIssuanceSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for TokenIssuanceSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCurrency (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_CURRENCY::type::value_type
    getCurrency() const
    {
        return this->tx_->at(sfCurrency);
    }

    /**
     * @brief Get sfMPTokenIssuanceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT192::type::value_type>
    getMPTokenIssuanceID() const
    {
        if (hasMPTokenIssuanceID())
        {
            return this->tx_->at(sfMPTokenIssuanceID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenIssuanceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenIssuanceID() const
    {
        return this->tx_->isFieldPresent(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfTransferFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTransferFee() const
    {
        if (hasTransferFee())
        {
            return this->tx_->at(sfTransferFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferFee() const
    {
        return this->tx_->isFieldPresent(sfTransferFee);
    }

    /**
     * @brief Get sfMPTokenMetadata (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
        {
            return this->tx_->at(sfMPTokenMetadata);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenMetadata is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->tx_->isFieldPresent(sfMPTokenMetadata);
    }
};

/**
 * @brief Builder for TokenIssuanceSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class TokenIssuanceSetBuilder : public TransactionBuilderBase<TokenIssuanceSetBuilder>
{
public:
    /**
     * @brief Construct a new TokenIssuanceSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param currency The sfCurrency field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    TokenIssuanceSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_CURRENCY::type::value_type> const& currency,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<TokenIssuanceSetBuilder>(ttTOKEN_ISSUANCE_SET, account, sequence, fee)
    {
        setCurrency(currency);
    }

    /**
     * @brief Construct a TokenIssuanceSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    TokenIssuanceSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttTOKEN_ISSUANCE_SET)
        {
            throw std::runtime_error("Invalid transaction type for TokenIssuanceSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCurrency (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceSetBuilder&
    setCurrency(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfCurrency] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceSetBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceSetBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenMetadata (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceSetBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * @brief Build and return the TokenIssuanceSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    TokenIssuanceSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return TokenIssuanceSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
