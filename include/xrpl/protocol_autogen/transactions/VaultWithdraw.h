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
class VaultWithdrawBuilder;

/**
 * Transaction: VaultWithdraw
 * Type: ttVAULT_WITHDRAW (69)
 * Delegable: Delegation::delegable
 * Amendment: featureSingleAssetVault
 * Privileges: mayDeleteMPT | mayAuthorizeMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultWithdrawBuilder to construct new transactions.
 */
class VaultWithdraw : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_WITHDRAW;

    /**
     * Construct a VaultWithdraw transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultWithdraw(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultWithdraw");
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
     * Get sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getAmount() const
    {
        return this->tx_.at(sfAmount);
    }

    /**
     * Get sfDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_.at(sfDestination);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_.isFieldPresent(sfDestination);
    }

    /**
     * Get sfDestinationTag (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_.at(sfDestinationTag);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_.isFieldPresent(sfDestinationTag);
    }
};

/**
 * Builder for VaultWithdraw transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultWithdrawBuilder : public TransactionBuilderBase<VaultWithdrawBuilder>
{
public:
    VaultWithdrawBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,
                     std::decay_t<typename SF_AMOUNT::type::value_type> const& amount)
        : TransactionBuilderBase<VaultWithdrawBuilder>(account, sequence, fee, signingPubKey, ttVAULT_WITHDRAW)
    {
        setVaultID(vaultID);
        setAmount(amount);
    }

    VaultWithdrawBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttVAULT_WITHDRAW)
        {
            throw std::runtime_error("Invalid transaction type for VaultWithdrawBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultWithdrawBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeREQUIRED)
     * Note: This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    VaultWithdrawBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultWithdrawBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    VaultWithdrawBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Build and return the completed VaultWithdraw wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    VaultWithdraw
    build()
    {
        return VaultWithdraw(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions