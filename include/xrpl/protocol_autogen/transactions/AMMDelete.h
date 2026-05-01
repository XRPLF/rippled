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

class AMMDeleteBuilder;

/**
 * @brief Transaction: AMMDelete
 *
 * Type: ttAMM_DELETE (40)
 * Delegable: Delegation::Delegable
 * Amendment: featureAMM
 * Privileges: MustDeleteAcct | MayDeleteMpt
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use AMMDeleteBuilder to construct new transactions.
 */
class AMMDelete : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttAMM_DELETE;

    /**
     * @brief Construct a AMMDelete transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit AMMDelete(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for AMMDelete");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfAsset (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ISSUE::type::value_type>
    getAsset() const
    {
        if (hasAsset())
        {
            return this->tx_->at(sfAsset);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAsset is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAsset() const
    {
        return this->tx_->isFieldPresent(sfAsset);
    }

    /**
     * @brief Get sfAsset2 (SoeRequired)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ISSUE::type::value_type>
    getAsset2() const
    {
        if (hasAsset2())
        {
            return this->tx_->at(sfAsset2);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAsset2 is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAsset2() const
    {
        return this->tx_->isFieldPresent(sfAsset2);
    }
};

/**
 * @brief Builder for AMMDelete transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class AMMDeleteBuilder : public TransactionBuilderBase<AMMDeleteBuilder>
{
public:
    /**
     * @brief Construct a new AMMDeleteBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    AMMDeleteBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<AMMDeleteBuilder>(ttAMM_DELETE, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a AMMDeleteBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    AMMDeleteBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttAMM_DELETE)
        {
            throw std::runtime_error("Invalid transaction type for AMMDeleteBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset] = STIssue(sfAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAsset2 (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    AMMDeleteBuilder&
    setAsset2(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfAsset2] = STIssue(sfAsset2, value);
        return *this;
    }

    /**
     * @brief Build and return the AMMDelete wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    AMMDelete
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return AMMDelete{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
