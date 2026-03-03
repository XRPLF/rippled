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
class MPTokenIssuanceDestroyBuilder;

/**
 * Transaction: MPTokenIssuanceDestroy
 * Type: ttMPTOKEN_ISSUANCE_DESTROY (55)
 * Delegable: Delegation::delegable
 * Amendment: featureMPTokensV1
 * Privileges: destroyMPTIssuance
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MPTokenIssuanceDestroyBuilder to construct new transactions.
 */
class MPTokenIssuanceDestroy : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMPTOKEN_ISSUANCE_DESTROY;

    /**
     * Construct a MPTokenIssuanceDestroy transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MPTokenIssuanceDestroy(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceDestroy");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfMPTokenIssuanceID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_.at(sfMPTokenIssuanceID);
    }
};

/**
 * Builder for MPTokenIssuanceDestroy transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MPTokenIssuanceDestroyBuilder : public TransactionBuilderBase<MPTokenIssuanceDestroyBuilder>
{
public:
    MPTokenIssuanceDestroyBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID)
        : TransactionBuilderBase<MPTokenIssuanceDestroyBuilder>(account, sequence, fee, signingPubKey, ttMPTOKEN_ISSUANCE_DESTROY)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    MPTokenIssuanceDestroyBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttMPTOKEN_ISSUANCE_DESTROY)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceDestroyBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceDestroyBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * Build and return the completed MPTokenIssuanceDestroy wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    MPTokenIssuanceDestroy
    build()
    {
        return MPTokenIssuanceDestroy(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions