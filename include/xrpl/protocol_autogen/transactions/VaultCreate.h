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

class VaultCreateBuilder;

/**
 * @brief Transaction: VaultCreate
 *
 * Type: ttVAULT_CREATE (65)
 * Delegable: Delegation::notDelegable
 * Amendment: featureSingleAssetVault
 * Privileges: createPseudoAcct | createMPTIssuance | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultCreateBuilder to construct new transactions.
 */
class VaultCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_CREATE;

    /**
     * @brief Construct a VaultCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_->at(sfAsset);
    }

    /**
     * @brief Get sfAssetsMaximum (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsMaximum() const
    {
        if (hasAssetsMaximum())
        {
            return this->tx_->at(sfAssetsMaximum);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAssetsMaximum is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAssetsMaximum() const
    {
        return this->tx_->isFieldPresent(sfAssetsMaximum);
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
     * @brief Get sfWithdrawalPolicy (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getWithdrawalPolicy() const
    {
        if (hasWithdrawalPolicy())
        {
            return this->tx_->at(sfWithdrawalPolicy);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfWithdrawalPolicy is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasWithdrawalPolicy() const
    {
        return this->tx_->isFieldPresent(sfWithdrawalPolicy);
    }

    /**
     * @brief Get sfData (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_->at(sfData);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_->isFieldPresent(sfData);
    }

    /**
     * @brief Get sfScale (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
        {
            return this->tx_->at(sfScale);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->tx_->isFieldPresent(sfScale);
    }
};

/**
 * @brief Builder for VaultCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultCreateBuilder : public TransactionBuilderBase<VaultCreateBuilder>
{
public:
    /**
     * @brief Construct a new VaultCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param asset The sfAsset field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    VaultCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultCreateBuilder>(ttVAULT_CREATE, account, sequence, fee)
    {
        setAsset(asset);
    }

    /**
     * @brief Construct a VaultCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    VaultCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttVAULT_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for VaultCreateBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAsset (soeREQUIRED)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAssetsMaximum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setAssetsMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsMaximum] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenMetadata (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfWithdrawalPolicy (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setWithdrawalPolicy(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWithdrawalPolicy] = value;
        return *this;
    }

    /**
     * @brief Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfScale (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * @brief Build and return the VaultCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    VaultCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return VaultCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
