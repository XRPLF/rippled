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
class VaultClawbackBuilder;

/**
 * Transaction: VaultClawback
 * Type: ttVAULT_CLAWBACK (70)
 * Delegable: Delegation::delegable
 * Amendment: featureSingleAssetVault
 * Privileges: mayDeleteMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultClawbackBuilder to construct new transactions.
 */
class VaultClawback : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_CLAWBACK;

    /**
     * Construct a VaultClawback transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultClawback(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultClawback");
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
     * Get sfHolder (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getHolder() const
    {
        return this->tx_.at(sfHolder);
    }

    /**
     * Get sfAmount (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_.at(sfAmount);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_.isFieldPresent(sfAmount);
    }
};

/**
 * Builder for VaultClawback transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultClawbackBuilder : public TransactionBuilderBase<VaultClawbackBuilder>
{
public:
    VaultClawbackBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& holder)
        : TransactionBuilderBase<VaultClawbackBuilder>(account, sequence, fee, signingPubKey, ttVAULT_CLAWBACK)
    {
        setVaultID(vaultID);
        setHolder(holder);
    }

    VaultClawbackBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttVAULT_CLAWBACK)
        {
            throw std::runtime_error("Invalid transaction type for VaultClawbackBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Set sfHolder (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultClawbackBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Build and return the completed VaultClawback wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    VaultClawback
    build()
    {
        return VaultClawback(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions