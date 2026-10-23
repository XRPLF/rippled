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

class BeneficiarySetBuilder;

/**
 * @brief Transaction: BeneficiarySet
 *
 * Type: ttBENEFICIARY_SET (119)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureBeneficiary
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BeneficiarySetBuilder to construct new transactions.
 */
class BeneficiarySet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBENEFICIARY_SET;

    /**
     * @brief Construct a BeneficiarySet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit BeneficiarySet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for BeneficiarySet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfBeneficiary (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getBeneficiary() const
    {
        if (hasBeneficiary())
        {
            return this->tx_->at(sfBeneficiary);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfBeneficiary is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBeneficiary() const
    {
        return this->tx_->isFieldPresent(sfBeneficiary);
    }

    /**
     * @brief Get sfTimeLock (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getTimeLock() const
    {
        if (hasTimeLock())
        {
            return this->tx_->at(sfTimeLock);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfTimeLock is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTimeLock() const
    {
        return this->tx_->isFieldPresent(sfTimeLock);
    }
};

/**
 * @brief Builder for BeneficiarySet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BeneficiarySetBuilder : public TransactionBuilderBase<BeneficiarySetBuilder>
{
public:
    /**
     * @brief Construct a new BeneficiarySetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BeneficiarySetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BeneficiarySetBuilder>(ttBENEFICIARY_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a BeneficiarySetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BeneficiarySetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBENEFICIARY_SET)
        {
            throw std::runtime_error("Invalid transaction type for BeneficiarySetBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfBeneficiary (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BeneficiarySetBuilder&
    setBeneficiary(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBeneficiary] = value;
        return *this;
    }

    /**
     * @brief Set sfTimeLock (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BeneficiarySetBuilder&
    setTimeLock(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfTimeLock] = value;
        return *this;
    }

    /**
     * @brief Build and return the BeneficiarySet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    BeneficiarySet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return BeneficiarySet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
