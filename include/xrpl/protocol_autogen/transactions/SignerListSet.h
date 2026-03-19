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

class SignerListSetBuilder;

/**
 * @brief Transaction: SignerListSet
 *
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
     * @brief Construct a SignerListSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SignerListSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SignerListSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfSignerQuorum (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSignerQuorum() const
    {
        return this->tx_->at(sfSignerQuorum);
    }
    /**
     * @brief Get sfSignerEntries (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<std::reference_wrapper<STArray const>>
    getSignerEntries() const
    {
        if (this->tx_->isFieldPresent(sfSignerEntries))
            return this->tx_->getFieldArray(sfSignerEntries);
        return std::nullopt;
    }

    /**
     * @brief Check if sfSignerEntries is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSignerEntries() const
    {
        return this->tx_->isFieldPresent(sfSignerEntries);
    }
};

/**
 * @brief Builder for SignerListSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SignerListSetBuilder : public TransactionBuilderBase<SignerListSetBuilder>
{
public:
    /**
     * @brief Construct a new SignerListSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param signerQuorum The sfSignerQuorum field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SignerListSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& signerQuorum,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SignerListSetBuilder>(ttSIGNER_LIST_SET, account, sequence, fee)
    {
        setSignerQuorum(signerQuorum);
    }

    /**
     * @brief Construct a SignerListSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SignerListSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSIGNER_LIST_SET)
        {
            throw std::runtime_error("Invalid transaction type for SignerListSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfSignerQuorum (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    SignerListSetBuilder&
    setSignerQuorum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSignerQuorum] = value;
        return *this;
    }

    /**
     * @brief Set sfSignerEntries (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SignerListSetBuilder&
    setSignerEntries(STArray const& value)
    {
        object_.setFieldArray(sfSignerEntries, value);
        return *this;
    }

    /**
     * @brief Build and return the SignerListSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SignerListSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SignerListSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
