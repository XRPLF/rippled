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
class VaultDeleteBuilder;

/**
 * Transaction: VaultDelete
 * Type: ttVAULT_DELETE (67)
 * Delegable: Delegation::delegable
 * Amendment: featureSingleAssetVault
 * Privileges: mustDeleteAcct | destroyMPTIssuance | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use VaultDeleteBuilder to construct new transactions.
 */
class VaultDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttVAULT_DELETE;

    /**
     * Construct a VaultDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit VaultDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for VaultDelete");
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
};

/**
 * Builder for VaultDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class VaultDeleteBuilder : public TransactionBuilderBase<VaultDeleteBuilder>
{
public:
    VaultDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID)
        : TransactionBuilderBase<VaultDeleteBuilder>(account, sequence, fee, signingPubKey, ttVAULT_DELETE)
    {
        setVaultID(vaultID);
    }

    VaultDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttVAULT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for VaultDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    VaultDeleteBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Build and return the completed VaultDelete wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    VaultDelete
    build()
    {
        return VaultDelete(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions