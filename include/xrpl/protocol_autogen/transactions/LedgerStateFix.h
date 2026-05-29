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

class LedgerStateFixBuilder;

/**
 * @brief Transaction: LedgerStateFix
 *
 * Type: ttLEDGER_STATE_FIX (53)
 * Delegable: Delegation::Delegable
 * Amendment: fixNFTokenPageLinks
 * Privileges: NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LedgerStateFixBuilder to construct new transactions.
 */
class LedgerStateFix : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLEDGER_STATE_FIX;

    /**
     * @brief Construct a LedgerStateFix transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LedgerStateFix(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LedgerStateFix");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLedgerFixType (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT16::type::value_type
    getLedgerFixType() const
    {
        return this->tx_->at(sfLedgerFixType);
    }

    /**
     * @brief Get sfOwner (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_->at(sfOwner);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_->isFieldPresent(sfOwner);
    }

    /**
     * @brief Get sfBookDirectory (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getBookDirectory() const
    {
        if (hasBookDirectory())
        {
            return this->tx_->at(sfBookDirectory);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBookDirectory is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBookDirectory() const
    {
        return this->tx_->isFieldPresent(sfBookDirectory);
    }
};

/**
 * @brief Builder for LedgerStateFix transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LedgerStateFixBuilder : public TransactionBuilderBase<LedgerStateFixBuilder>
{
public:
    /**
     * @brief Construct a new LedgerStateFixBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param ledgerFixType The sfLedgerFixType field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LedgerStateFixBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT16::type::value_type> const& ledgerFixType,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LedgerStateFixBuilder>(ttLEDGER_STATE_FIX, account, sequence, fee)
    {
        setLedgerFixType(ledgerFixType);
    }

    /**
     * @brief Construct a LedgerStateFixBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LedgerStateFixBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLEDGER_STATE_FIX)
        {
            throw std::runtime_error("Invalid transaction type for LedgerStateFixBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLedgerFixType (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    LedgerStateFixBuilder&
    setLedgerFixType(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfLedgerFixType] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LedgerStateFixBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfBookDirectory (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    LedgerStateFixBuilder&
    setBookDirectory(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBookDirectory] = value;
        return *this;
    }

    /**
     * @brief Build and return the LedgerStateFix wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LedgerStateFix
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LedgerStateFix{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
