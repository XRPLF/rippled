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
class AMMDeleteBuilder;

/**
 * Transaction: AMMDelete
 * Type: ttAMM_DELETE (40)
 * Delegable: Delegation::delegable
 * Amendment: featureAMM
 * Privileges: mustDeleteAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMDeleteBuilder to construct new transactions.
 */
class AMMDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_DELETE;

    /**
     * Construct a AMMDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAsset (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset() const
    {
        return this->tx_.at(sfAsset);
    }

    /**
     * Get sfAsset2 (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getAsset2() const
    {
        return this->tx_.at(sfAsset2);
    }
};

/**
 * Builder for AMMDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMDeleteBuilder : public TransactionBuilderBase<AMMDeleteBuilder>
{
public:
    AMMDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset,
                     std::decay_t<typename SF_ISSUE::type::value_type> const& asset2)
        : TransactionBuilderBase<AMMDeleteBuilder>(account, sequence, fee, signingPubKey, ttAMM_DELETE)
    {
        setAsset(asset);
        setAsset2(asset2);
    }

    AMMDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttAMM_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for AMMDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAsset (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * Set sfAsset2 (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * Build and return the completed AMMDelete wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    AMMDelete
    build()
    {
        return AMMDelete(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions