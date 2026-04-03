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

class MPTokenIssuanceCreateBuilder;

/**
 * @brief Transaction: MPTokenIssuanceCreate
 *
 * Type: ttMPTOKEN_ISSUANCE_CREATE (54)
 * Delegable: Delegation::delegable
 * Amendment: featureMPTokensV1
 * Privileges: createMPTIssuance
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MPTokenIssuanceCreateBuilder to construct new transactions.
 */
class MPTokenIssuanceCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMPTOKEN_ISSUANCE_CREATE;

    /**
     * @brief Construct a MPTokenIssuanceCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MPTokenIssuanceCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAssetScale (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getAssetScale() const
    {
        if (hasAssetScale())
        {
            return this->tx_->at(sfAssetScale);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetScale() const
    {
        return this->tx_->isFieldPresent(sfAssetScale);
    }

    /**
     * @brief Get sfTransferFee (soeOPTIONAL)
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
     * @brief Get sfMaximumAmount (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getMaximumAmount() const
    {
        if (hasMaximumAmount())
        {
            return this->tx_->at(sfMaximumAmount);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaximumAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaximumAmount() const
    {
        return this->tx_->isFieldPresent(sfMaximumAmount);
    }

    /**
     * @brief Get sfMPTokenMetadata (soeOPTIONAL)
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
     * @brief Get sfMutableFlags (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMutableFlags() const
    {
        if (hasMutableFlags())
        {
            return this->tx_->at(sfMutableFlags);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMutableFlags is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMutableFlags() const
    {
        return this->tx_->isFieldPresent(sfMutableFlags);
    }
};

/**
 * @brief Builder for MPTokenIssuanceCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MPTokenIssuanceCreateBuilder : public TransactionBuilderBase<MPTokenIssuanceCreateBuilder>
{
public:
    /**
     * @brief Construct a new MPTokenIssuanceCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    MPTokenIssuanceCreateBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MPTokenIssuanceCreateBuilder>(ttMPTOKEN_ISSUANCE_CREATE, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a MPTokenIssuanceCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    MPTokenIssuanceCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMPTOKEN_ISSUANCE_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAssetScale (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setAssetScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfAssetScale] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * @brief Set sfMaximumAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setMaximumAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMaximumAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenMetadata (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfMutableFlags (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceCreateBuilder&
    setMutableFlags(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMutableFlags] = value;
        return *this;
    }

    /**
     * @brief Build and return the MPTokenIssuanceCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    MPTokenIssuanceCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MPTokenIssuanceCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
