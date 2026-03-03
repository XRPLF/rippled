#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class VaultCreateBuilder;

/**
 * Transaction: VaultCreate
 * Type: ttVAULT_CREATE (65)
 * Delegable: Delegation::delegable
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
     * Construct a VaultCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultCreate(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAsset (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_.at(sfAsset);
    }

    /**
     * Get sfAssetsMaximum (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getAssetsMaximum() const
    {
        if (hasAssetsMaximum())
        {
            return this->tx_.at(sfAssetsMaximum);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAssetsMaximum() const
    {
        return this->tx_.isFieldPresent(sfAssetsMaximum);
    }

    /**
     * Get sfMPTokenMetadata (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
        {
            return this->tx_.at(sfMPTokenMetadata);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->tx_.isFieldPresent(sfMPTokenMetadata);
    }

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
     * Get sfWithdrawalPolicy (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getWithdrawalPolicy() const
    {
        if (hasWithdrawalPolicy())
        {
            return this->tx_.at(sfWithdrawalPolicy);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasWithdrawalPolicy() const
    {
        return this->tx_.isFieldPresent(sfWithdrawalPolicy);
    }

    /**
     * Get sfData (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_.at(sfData);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_.isFieldPresent(sfData);
    }

    /**
     * Get sfScale (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getScale() const
    {
        if (hasScale())
        {
            return this->tx_.at(sfScale);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasScale() const
    {
        return this->tx_.isFieldPresent(sfScale);
    }
};

/**
 * Builder for VaultCreate transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultCreateBuilder : public TransactionBuilderBase<VaultCreateBuilder>
{
public:
    VaultCreateBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset)
        : TransactionBuilderBase<VaultCreateBuilder>(account, sequence, fee, signingPubKey, ttVAULT_CREATE)
    {
        setAsset(asset);
    }

    VaultCreateBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttVAULT_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for VaultCreateBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAssetsMaximum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setAssetsMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsMaximum] = value;
        return *this;
    }

    /**
     * Set sfMPTokenMetadata (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfWithdrawalPolicy (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setWithdrawalPolicy(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfWithdrawalPolicy] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Set sfScale (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultCreateBuilder&
    setScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfScale] = value;
        return *this;
    }

    /**
     * Build and return the completed VaultCreate wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    VaultCreate
    build()
    {
        return VaultCreate(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions