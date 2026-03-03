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
class DelegateSetBuilder;

/**
 * Transaction: DelegateSet
 * Type: ttDELEGATE_SET (64)
 * Delegable: Delegation::notDelegable
 * Amendment: featurePermissionDelegationV1_1
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DelegateSetBuilder to construct new transactions.
 */
class DelegateSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDELEGATE_SET;

    /**
     * Construct a DelegateSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DelegateSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DelegateSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAuthorize (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAuthorize() const
    {
        return this->tx_.at(sfAuthorize);
    }
    /**
     * Get sfPermissions (soeREQUIRED)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    STArray const&
    getPermissions() const
    {
        return this->tx_.getFieldArray(sfPermissions);
    }
};

/**
 * Builder for DelegateSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DelegateSetBuilder : public TransactionBuilderBase<DelegateSetBuilder>
{
public:
    DelegateSetBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& authorize,
                     STArray const& permissions)
        : TransactionBuilderBase<DelegateSetBuilder>(account, sequence, fee, signingPubKey, ttDELEGATE_SET)
    {
        setAuthorize(authorize);
        setPermissions(permissions);
    }

    DelegateSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttDELEGATE_SET)
        {
            throw std::runtime_error("Invalid transaction type for DelegateSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAuthorize (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateSetBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * Set sfPermissions (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    DelegateSetBuilder&
    setPermissions(STArray const& value)
    {
        object_.setFieldArray(sfPermissions, value);
        return *this;
    }

    /**
     * Build and return the completed DelegateSet wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    DelegateSet
    build()
    {
        return DelegateSet(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions