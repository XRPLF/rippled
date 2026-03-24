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

class UNLModifyBuilder;

/**
 * @brief Transaction: UNLModify
 *
 * Type: ttUNL_MODIFY (102)
 * Delegable: Delegation::notDelegable
 * Amendment: uint256{}
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use UNLModifyBuilder to construct new transactions.
 */
class UNLModify : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttUNL_MODIFY;

    /**
     * @brief Construct a UNLModify transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit UNLModify(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for UNLModify");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfUNLModifyDisabling (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getUNLModifyDisabling() const
    {
        return this->tx_->at(sfUNLModifyDisabling);
    }

    /**
     * @brief Get sfLedgerSequence (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getLedgerSequence() const
    {
        return this->tx_->at(sfLedgerSequence);
    }

    /**
     * @brief Get sfUNLModifyValidator (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getUNLModifyValidator() const
    {
        return this->tx_->at(sfUNLModifyValidator);
    }
};

/**
 * @brief Builder for UNLModify transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class UNLModifyBuilder : public TransactionBuilderBase<UNLModifyBuilder>
{
public:
    /**
     * @brief Construct a new UNLModifyBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param uNLModifyDisabling The sfUNLModifyDisabling field value.
     * @param ledgerSequence The sfLedgerSequence field value.
     * @param uNLModifyValidator The sfUNLModifyValidator field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    UNLModifyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT8::type::value_type> const& uNLModifyDisabling,                     std::decay_t<typename SF_UINT32::type::value_type> const& ledgerSequence,                     std::decay_t<typename SF_VL::type::value_type> const& uNLModifyValidator,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<UNLModifyBuilder>(ttUNL_MODIFY, account, sequence, fee)
    {
        setUNLModifyDisabling(uNLModifyDisabling);
        setLedgerSequence(ledgerSequence);
        setUNLModifyValidator(uNLModifyValidator);
    }

    /**
     * @brief Construct a UNLModifyBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    UNLModifyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttUNL_MODIFY)
        {
            throw std::runtime_error("Invalid transaction type for UNLModifyBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfUNLModifyDisabling (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setUNLModifyDisabling(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfUNLModifyDisabling] = value;
        return *this;
    }

    /**
     * @brief Set sfLedgerSequence (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setLedgerSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLedgerSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfUNLModifyValidator (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    UNLModifyBuilder&
    setUNLModifyValidator(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfUNLModifyValidator] = value;
        return *this;
    }

    /**
     * @brief Build and return the UNLModify wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    UNLModify
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return UNLModify{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
