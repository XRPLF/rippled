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

class PasskeyListSetBuilder;

/**
 * @brief Transaction: PasskeyListSet
 *
 * Type: ttPASSKEY_LIST_SET (92)
 * Delegable: Delegation::Delegable
 * Amendment: featurePasskey
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use PasskeyListSetBuilder to construct new transactions.
 */
class PasskeyListSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttPASSKEY_LIST_SET;

    /**
     * @brief Construct a PasskeyListSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit PasskeyListSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for PasskeyListSet");
        }
    }

    // Transaction-specific field getters
    /**
     * @brief Get sfPasskeys (SoeRequired)
     * @note This is an untyped field.
     * @return The field value.
     */
    [[nodiscard]]
    STArray const&
    getPasskeys() const
    {
        return this->tx_->getFieldArray(sfPasskeys);
    }
};

/**
 * @brief Builder for PasskeyListSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class PasskeyListSetBuilder : public TransactionBuilderBase<PasskeyListSetBuilder>
{
public:
    /**
     * @brief Construct a new PasskeyListSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param passkeys The sfPasskeys field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    PasskeyListSetBuilder(SF_ACCOUNT::type::value_type account,
                     STArray const& passkeys,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<PasskeyListSetBuilder>(ttPASSKEY_LIST_SET, account, sequence, fee)
    {
        setPasskeys(passkeys);
    }

    /**
     * @brief Construct a PasskeyListSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    PasskeyListSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttPASSKEY_LIST_SET)
        {
            throw std::runtime_error("Invalid transaction type for PasskeyListSetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfPasskeys (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PasskeyListSetBuilder&
    setPasskeys(STArray const& value)
    {
        object_.setFieldArray(sfPasskeys, value);
        return *this;
    }

    /**
     * @brief Build and return the PasskeyListSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    PasskeyListSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return PasskeyListSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
