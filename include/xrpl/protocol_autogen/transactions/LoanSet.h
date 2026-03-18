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

class LoanSetBuilder;

/**
 * @brief Transaction: LoanSet
 *
 * Type: ttLOAN_SET (80)
 * Delegable: Delegation::notDelegable
 * Amendment: featureLendingProtocol
 * Privileges: mayAuthorizeMPT | mustModifyVault
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanSetBuilder to construct new transactions.
 */
class LoanSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_SET;

    /**
     * @brief Construct a LoanSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfLoanBrokerID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->tx_->at(sfLoanBrokerID);
    }

    /**
     * @brief Get sfData (soeOPTIONAL)
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

    /**
     * @brief Get sfCounterparty (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getCounterparty() const
    {
        if (hasCounterparty())
        {
            return this->tx_->at(sfCounterparty);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterparty is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterparty() const
    {
        return this->tx_->isFieldPresent(sfCounterparty);
    }
    /**
     * @brief Get sfCounterpartySignature (soeOPTIONAL)
     * @note This is an untyped field.
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    std::optional<STObject>
    getCounterpartySignature() const
    {
        if (this->tx_->isFieldPresent(sfCounterpartySignature))
            return this->tx_->getFieldObject(sfCounterpartySignature);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCounterpartySignature is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCounterpartySignature() const
    {
        return this->tx_->isFieldPresent(sfCounterpartySignature);
    }

    /**
     * @brief Get sfLoanOriginationFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLoanOriginationFee() const
    {
        if (hasLoanOriginationFee())
        {
            return this->tx_->at(sfLoanOriginationFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLoanOriginationFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLoanOriginationFee() const
    {
        return this->tx_->isFieldPresent(sfLoanOriginationFee);
    }

    /**
     * @brief Get sfLoanServiceFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLoanServiceFee() const
    {
        if (hasLoanServiceFee())
        {
            return this->tx_->at(sfLoanServiceFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLoanServiceFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLoanServiceFee() const
    {
        return this->tx_->isFieldPresent(sfLoanServiceFee);
    }

    /**
     * @brief Get sfLatePaymentFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getLatePaymentFee() const
    {
        if (hasLatePaymentFee())
        {
            return this->tx_->at(sfLatePaymentFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLatePaymentFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLatePaymentFee() const
    {
        return this->tx_->isFieldPresent(sfLatePaymentFee);
    }

    /**
     * @brief Get sfClosePaymentFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getClosePaymentFee() const
    {
        if (hasClosePaymentFee())
        {
            return this->tx_->at(sfClosePaymentFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfClosePaymentFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasClosePaymentFee() const
    {
        return this->tx_->isFieldPresent(sfClosePaymentFee);
    }

    /**
     * @brief Get sfOverpaymentFee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOverpaymentFee() const
    {
        if (hasOverpaymentFee())
        {
            return this->tx_->at(sfOverpaymentFee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOverpaymentFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOverpaymentFee() const
    {
        return this->tx_->isFieldPresent(sfOverpaymentFee);
    }

    /**
     * @brief Get sfInterestRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getInterestRate() const
    {
        if (hasInterestRate())
        {
            return this->tx_->at(sfInterestRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfInterestRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasInterestRate() const
    {
        return this->tx_->isFieldPresent(sfInterestRate);
    }

    /**
     * @brief Get sfLateInterestRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLateInterestRate() const
    {
        if (hasLateInterestRate())
        {
            return this->tx_->at(sfLateInterestRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfLateInterestRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLateInterestRate() const
    {
        return this->tx_->isFieldPresent(sfLateInterestRate);
    }

    /**
     * @brief Get sfCloseInterestRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCloseInterestRate() const
    {
        if (hasCloseInterestRate())
        {
            return this->tx_->at(sfCloseInterestRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfCloseInterestRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCloseInterestRate() const
    {
        return this->tx_->isFieldPresent(sfCloseInterestRate);
    }

    /**
     * @brief Get sfOverpaymentInterestRate (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getOverpaymentInterestRate() const
    {
        if (hasOverpaymentInterestRate())
        {
            return this->tx_->at(sfOverpaymentInterestRate);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOverpaymentInterestRate is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOverpaymentInterestRate() const
    {
        return this->tx_->isFieldPresent(sfOverpaymentInterestRate);
    }

    /**
     * @brief Get sfPrincipalRequested (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPrincipalRequested() const
    {
        return this->tx_->at(sfPrincipalRequested);
    }

    /**
     * @brief Get sfPaymentTotal (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPaymentTotal() const
    {
        if (hasPaymentTotal())
        {
            return this->tx_->at(sfPaymentTotal);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPaymentTotal is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPaymentTotal() const
    {
        return this->tx_->isFieldPresent(sfPaymentTotal);
    }

    /**
     * @brief Get sfPaymentInterval (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getPaymentInterval() const
    {
        if (hasPaymentInterval())
        {
            return this->tx_->at(sfPaymentInterval);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfPaymentInterval is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPaymentInterval() const
    {
        return this->tx_->isFieldPresent(sfPaymentInterval);
    }

    /**
     * @brief Get sfGracePeriod (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getGracePeriod() const
    {
        if (hasGracePeriod())
        {
            return this->tx_->at(sfGracePeriod);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfGracePeriod is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasGracePeriod() const
    {
        return this->tx_->isFieldPresent(sfGracePeriod);
    }
};

/**
 * @brief Builder for LoanSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanSetBuilder : public TransactionBuilderBase<LoanSetBuilder>
{
public:
    /**
     * @brief Construct a new LoanSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param loanBrokerID The sfLoanBrokerID field value.
     * @param principalRequested The sfPrincipalRequested field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    LoanSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                     std::decay_t<typename SF_NUMBER::type::value_type> const& principalRequested,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanSetBuilder>(ttLOAN_SET, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
        setPrincipalRequested(principalRequested);
    }

    /**
     * @brief Construct a LoanSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    LoanSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_SET)
        {
            throw std::runtime_error("Invalid transaction type for LoanSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * @brief Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterparty (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * @brief Set sfCounterpartySignature (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * @brief Set sfLoanOriginationFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanOriginationFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanOriginationFee] = value;
        return *this;
    }

    /**
     * @brief Set sfLoanServiceFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanServiceFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanServiceFee] = value;
        return *this;
    }

    /**
     * @brief Set sfLatePaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLatePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLatePaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfClosePaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setClosePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfClosePaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfOverpaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setOverpaymentFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentFee] = value;
        return *this;
    }

    /**
     * @brief Set sfInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfLateInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLateInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLateInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfCloseInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCloseInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfOverpaymentInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setOverpaymentInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentInterestRate] = value;
        return *this;
    }

    /**
     * @brief Set sfPrincipalRequested (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPrincipalRequested(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPrincipalRequested] = value;
        return *this;
    }

    /**
     * @brief Set sfPaymentTotal (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPaymentTotal(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentTotal] = value;
        return *this;
    }

    /**
     * @brief Set sfPaymentInterval (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPaymentInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentInterval] = value;
        return *this;
    }

    /**
     * @brief Set sfGracePeriod (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * @brief Build and return the LoanSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    LoanSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return LoanSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
