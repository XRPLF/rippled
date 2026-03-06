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
class AccountDeleteBuilder;

/**
 * Transaction: AccountDelete
 * Type: ttACCOUNT_DELETE (21)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: mustDeleteAcct
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AccountDeleteBuilder to construct new transactions.
 */
class AccountDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttACCOUNT_DELETE;

    /**
     * Construct a AccountDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AccountDelete(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AccountDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDestination (soeREQUIRED)
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_.at(sfDestination);
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

    /**
     * Get sfCredentialIDs (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_.at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_.isFieldPresent(sfCredentialIDs);
    }
};

/**
 * Builder for AccountDelete transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AccountDeleteBuilder : public TransactionBuilderBase<AccountDeleteBuilder>
{
public:
    AccountDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AccountDeleteBuilder>(ttACCOUNT_DELETE, account, sequence, fee)
    {
        setDestination(destination);
    }

    AccountDeleteBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttACCOUNT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for AccountDeleteBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * Build and return the completed AccountDelete wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, AccountDelete>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, AccountDelete>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
