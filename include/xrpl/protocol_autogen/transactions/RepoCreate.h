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

class RepoCreateBuilder;

/**
 * @brief Transaction: RepoCreate
 *
 * Type: ttREPO_CREATE (114)
 * Delegable: Delegation::Delegable
 * Amendment: featureRepo
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use RepoCreateBuilder to construct new transactions.
 */
class RepoCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttREPO_CREATE;

    /**
     * @brief Construct a RepoCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit RepoCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for RepoCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfCounterparty (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getCounterparty() const
    {
        return this->tx_->at(sfCounterparty);
    }

    /**
     * @brief Get sfCollateralAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getCollateralAmount() const
    {
        return this->tx_->at(sfCollateralAmount);
    }

    /**
     * @brief Get sfPurchasePrice (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return The field value.
     */
    [[nodiscard]]
    SF_AMOUNT::type::value_type
    getPurchasePrice() const
    {
        return this->tx_->at(sfPurchasePrice);
    }

    /**
     * @brief Get sfInterestRate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getInterestRate() const
    {
        return this->tx_->at(sfInterestRate);
    }

    /**
     * @brief Get sfExpiration (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getExpiration() const
    {
        return this->tx_->at(sfExpiration);
    }

    /**
     * @brief Get sfMaturityDate (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getMaturityDate() const
    {
        return this->tx_->at(sfMaturityDate);
    }

    /**
     * @brief Get sfGracePeriod (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getGracePeriod() const
    {
        return this->tx_->at(sfGracePeriod);
    }

    /**
     * @brief Get sfData (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_->at(sfData);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfData is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_->isFieldPresent(sfData);
    }
};

/**
 * @brief Builder for RepoCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class RepoCreateBuilder : public TransactionBuilderBase<RepoCreateBuilder>
{
public:
    /**
     * @brief Construct a new RepoCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param counterparty The sfCounterparty field value.
     * @param collateralAmount The sfCollateralAmount field value.
     * @param purchasePrice The sfPurchasePrice field value.
     * @param interestRate The sfInterestRate field value.
     * @param expiration The sfExpiration field value.
     * @param maturityDate The sfMaturityDate field value.
     * @param gracePeriod The sfGracePeriod field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    RepoCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_ACCOUNT::type::value_type> const& counterparty,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& collateralAmount,                     std::decay_t<typename SF_AMOUNT::type::value_type> const& purchasePrice,                     std::decay_t<typename SF_UINT32::type::value_type> const& interestRate,                     std::decay_t<typename SF_UINT32::type::value_type> const& expiration,                     std::decay_t<typename SF_UINT32::type::value_type> const& maturityDate,                     std::decay_t<typename SF_UINT32::type::value_type> const& gracePeriod,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<RepoCreateBuilder>(ttREPO_CREATE, account, sequence, fee)
    {
        setCounterparty(counterparty);
        setCollateralAmount(collateralAmount);
        setPurchasePrice(purchasePrice);
        setInterestRate(interestRate);
        setExpiration(expiration);
        setMaturityDate(maturityDate);
        setGracePeriod(gracePeriod);
    }

    /**
     * @brief Construct a RepoCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    RepoCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttREPO_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for RepoCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfCounterparty (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfCollateralAmount (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setCollateralAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfCollateralAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfPurchasePrice (SoeRequired)
     * @note This field supports MPT (Multi-Purpose Token) amounts.
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setPurchasePrice(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfPurchasePrice] = value;
        return *this;
    }

    /**
     * @brief Set sfInterestRate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfMaturityDate (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setMaturityDate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMaturityDate] = value;
        return *this;
    }

    /**
     * @brief Set sfGracePeriod (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfData (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    RepoCreateBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Build and return the RepoCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    RepoCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return RepoCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
