// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

class AccountDeleteBuilder;

/**
 * @brief Transaction: AccountDelete
 *
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
     * @brief Construct a AccountDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AccountDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AccountDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDestination (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getDestination() const
    {
        return this->tx_->at(sfDestination);
    }

    /**
     * @brief Get sfDestinationTag (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getDestinationTag() const
    {
        if (hasDestinationTag())
        {
            return this->tx_->at(sfDestinationTag);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDestinationTag is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDestinationTag() const
    {
        return this->tx_->isFieldPresent(sfDestinationTag);
    }

    /**
     * @brief Get sfCredentialIDs (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VECTOR256::type::value_type>
    getCredentialIDs() const
    {
        if (hasCredentialIDs())
        {
            return this->tx_->at(sfCredentialIDs);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCredentialIDs is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCredentialIDs() const
    {
        return this->tx_->isFieldPresent(sfCredentialIDs);
    }
};

/**
 * @brief Builder for AccountDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AccountDeleteBuilder : public TransactionBuilderBase<AccountDeleteBuilder>
{
public:
    /**
     * @brief Construct a new AccountDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param destination The sfDestination field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AccountDeleteBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& destination,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AccountDeleteBuilder>(ttACCOUNT_DELETE, account, sequence, fee)
    {
        setDestination(destination);
    }

    /**
     * @brief Construct a AccountDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AccountDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttACCOUNT_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for AccountDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDestination (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * @brief Set sfDestinationTag (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setDestinationTag(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfDestinationTag] = value;
        return *this;
    }

    /**
     * @brief Set sfCredentialIDs (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    AccountDeleteBuilder&
    setCredentialIDs(std::decay_t<typename SF_VECTOR256::type::value_type> const& value)
    {
        object_[sfCredentialIDs] = value;
        return *this;
    }

    /**
     * @brief Build and return the AccountDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AccountDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AccountDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
