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
class SignerListSetBuilder;

/**
 * Transaction: SignerListSet
 * Type: ttSIGNER_LIST_SET (12)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SignerListSetBuilder to construct new transactions.
 */
class SignerListSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSIGNER_LIST_SET;

    /**
     * Construct a SignerListSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SignerListSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SignerListSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfSignerQuorum (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerQuorum() const
    {
        return this->tx_.at(sfSignerQuorum);
    }
    /**
     * Get sfSignerEntries (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getSignerEntries() const
    {
        if (this->tx_.isFieldPresent(sfSignerEntries))
            return this->tx_.getFieldArray(sfSignerEntries);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasSignerEntries() const
    {
        return this->tx_.isFieldPresent(sfSignerEntries);
    }
};

/**
 * Builder for SignerListSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SignerListSetBuilder : public TransactionBuilderBase<SignerListSetBuilder>
{
public:
    SignerListSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& signerQuorum,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SignerListSetBuilder>(ttSIGNER_LIST_SET, account, sequence, fee)
    {
        setSignerQuorum(signerQuorum);
    }

    SignerListSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttSIGNER_LIST_SET)
        {
            throw std::runtime_error("Invalid transaction type for SignerListSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfSignerQuorum (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListSetBuilder&
    setSignerQuorum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerQuorum] = value;
        return *this;
    }

    /**
     * Set sfSignerEntries (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SignerListSetBuilder&
    setSignerEntries(STArray const& value)
    {
        object_.setFieldArray(sfSignerEntries, value);
        return *this;
    }

    /**
     * Build and return the completed SignerListSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, SignerListSet>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, SignerListSet>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
