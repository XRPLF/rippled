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
class VaultSetBuilder;

/**
 * Transaction: VaultSet
 * Type: ttVAULT_SET (66)
 * Delegable: Delegation::delegable
 * Amendment: featureSingleAssetVault
 * Privileges: mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultSetBuilder to construct new transactions.
 */
class VaultSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_SET;

    /**
     * Construct a VaultSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfVaultID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->tx_.at(sfVaultID);
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
};

/**
 * Builder for VaultSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultSetBuilder : public TransactionBuilderBase<VaultSetBuilder>
{
public:
    VaultSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<VaultSetBuilder>(ttVAULT_SET, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    VaultSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttVAULT_SET)
        {
            throw std::runtime_error("Invalid transaction type for VaultSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultSetBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Set sfAssetsMaximum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultSetBuilder&
    setAssetsMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfAssetsMaximum] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultSetBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Build and return the completed VaultSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, VaultSet>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, VaultSet>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
