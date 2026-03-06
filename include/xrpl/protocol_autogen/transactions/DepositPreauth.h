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
class DepositPreauthBuilder;

/**
 * Transaction: DepositPreauth
 * Type: ttDEPOSIT_PREAUTH (19)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DepositPreauthBuilder to construct new transactions.
 */
class DepositPreauth : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDEPOSIT_PREAUTH;

    /**
     * Construct a DepositPreauth transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DepositPreauth(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DepositPreauth");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfAuthorize (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getAuthorize() const
    {
        if (hasAuthorize())
        {
            return this->tx_.at(sfAuthorize);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuthorize() const
    {
        return this->tx_.isFieldPresent(sfAuthorize);
    }

    /**
     * Get sfUnauthorize (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getUnauthorize() const
    {
        if (hasUnauthorize())
        {
            return this->tx_.at(sfUnauthorize);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasUnauthorize() const
    {
        return this->tx_.isFieldPresent(sfUnauthorize);
    }
    /**
     * Get sfAuthorizeCredentials (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getAuthorizeCredentials() const
    {
        if (this->tx_.isFieldPresent(sfAuthorizeCredentials))
            return this->tx_.getFieldArray(sfAuthorizeCredentials);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAuthorizeCredentials() const
    {
        return this->tx_.isFieldPresent(sfAuthorizeCredentials);
    }
    /**
     * Get sfUnauthorizeCredentials (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getUnauthorizeCredentials() const
    {
        if (this->tx_.isFieldPresent(sfUnauthorizeCredentials))
            return this->tx_.getFieldArray(sfUnauthorizeCredentials);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasUnauthorizeCredentials() const
    {
        return this->tx_.isFieldPresent(sfUnauthorizeCredentials);
    }
};

/**
 * Builder for DepositPreauth transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DepositPreauthBuilder : public TransactionBuilderBase<DepositPreauthBuilder>
{
public:
    DepositPreauthBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DepositPreauthBuilder>(ttDEPOSIT_PREAUTH, account, sequence, fee)
    {
    }

    DepositPreauthBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttDEPOSIT_PREAUTH)
        {
            throw std::runtime_error("Invalid transaction type for DepositPreauthBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfAuthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAuthorize] = value;
        return *this;
    }

    /**
     * Set sfUnauthorize (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setUnauthorize(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfUnauthorize] = value;
        return *this;
    }

    /**
     * Set sfAuthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setAuthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfAuthorizeCredentials, value);
        return *this;
    }

    /**
     * Set sfUnauthorizeCredentials (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DepositPreauthBuilder&
    setUnauthorizeCredentials(STArray const& value)
    {
        object_.setFieldArray(sfUnauthorizeCredentials, value);
        return *this;
    }

    /**
     * Build and return the completed DepositPreauth wrapper.
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, DepositPreauth>
    build()
    {
        return protocol_autogen::Owning<STTx, DepositPreauth>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
