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

// Forward declaration
class LoanSetBuilder;

/**
 * Transaction: LoanSet
 * Type: ttLOAN_SET (80)
 * Delegable: Delegation::delegable
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
     * Construct a LoanSet transaction wrapper from an existing STTx object.
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
     * Get sfLoanBrokerID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getLoanBrokerID() const
    {
        return this->tx_->at(sfLoanBrokerID);
    }

    /**
     * Get sfData (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_->isFieldPresent(sfData);
    }

    /**
     * Get sfCounterparty (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasCounterparty() const
    {
        return this->tx_->isFieldPresent(sfCounterparty);
    }
    /**
     * Get sfCounterpartySignature (soeOPTIONAL)
     * Note: This is an untyped field
     */
    [[nodiscard]]
    std::optional<STObject>
    getCounterpartySignature() const
    {
        if (this->tx_->isFieldPresent(sfCounterpartySignature))
            return this->tx_->getFieldObject(sfCounterpartySignature);
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCounterpartySignature() const
    {
        return this->tx_->isFieldPresent(sfCounterpartySignature);
    }

    /**
     * Get sfLoanOriginationFee (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasLoanOriginationFee() const
    {
        return this->tx_->isFieldPresent(sfLoanOriginationFee);
    }

    /**
     * Get sfLoanServiceFee (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasLoanServiceFee() const
    {
        return this->tx_->isFieldPresent(sfLoanServiceFee);
    }

    /**
     * Get sfLatePaymentFee (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasLatePaymentFee() const
    {
        return this->tx_->isFieldPresent(sfLatePaymentFee);
    }

    /**
     * Get sfClosePaymentFee (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasClosePaymentFee() const
    {
        return this->tx_->isFieldPresent(sfClosePaymentFee);
    }

    /**
     * Get sfOverpaymentFee (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasOverpaymentFee() const
    {
        return this->tx_->isFieldPresent(sfOverpaymentFee);
    }

    /**
     * Get sfInterestRate (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasInterestRate() const
    {
        return this->tx_->isFieldPresent(sfInterestRate);
    }

    /**
     * Get sfLateInterestRate (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasLateInterestRate() const
    {
        return this->tx_->isFieldPresent(sfLateInterestRate);
    }

    /**
     * Get sfCloseInterestRate (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasCloseInterestRate() const
    {
        return this->tx_->isFieldPresent(sfCloseInterestRate);
    }

    /**
     * Get sfOverpaymentInterestRate (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasOverpaymentInterestRate() const
    {
        return this->tx_->isFieldPresent(sfOverpaymentInterestRate);
    }

    /**
     * Get sfPrincipalRequested (soeREQUIRED)
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getPrincipalRequested() const
    {
        return this->tx_->at(sfPrincipalRequested);
    }

    /**
     * Get sfPaymentTotal (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasPaymentTotal() const
    {
        return this->tx_->isFieldPresent(sfPaymentTotal);
    }

    /**
     * Get sfPaymentInterval (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasPaymentInterval() const
    {
        return this->tx_->isFieldPresent(sfPaymentInterval);
    }

    /**
     * Get sfGracePeriod (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasGracePeriod() const
    {
        return this->tx_->isFieldPresent(sfGracePeriod);
    }
};

/**
 * Builder for LoanSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanSetBuilder : public TransactionBuilderBase<LoanSetBuilder>
{
public:
    LoanSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& loanBrokerID,                     std::decay_t<typename SF_NUMBER::type::value_type> const& principalRequested,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanSetBuilder>(ttLOAN_SET, account, sequence, fee)
    {
        setLoanBrokerID(loanBrokerID);
        setPrincipalRequested(principalRequested);
    }

    LoanSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttLOAN_SET)
        {
            throw std::runtime_error("Invalid transaction type for LoanSetBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfLoanBrokerID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Set sfCounterparty (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCounterparty(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfCounterparty] = value;
        return *this;
    }

    /**
     * Set sfCounterpartySignature (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCounterpartySignature(STObject const& value)
    {
        object_.setFieldObject(sfCounterpartySignature, value);
        return *this;
    }

    /**
     * Set sfLoanOriginationFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanOriginationFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanOriginationFee] = value;
        return *this;
    }

    /**
     * Set sfLoanServiceFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLoanServiceFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLoanServiceFee] = value;
        return *this;
    }

    /**
     * Set sfLatePaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLatePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLatePaymentFee] = value;
        return *this;
    }

    /**
     * Set sfClosePaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setClosePaymentFee(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfClosePaymentFee] = value;
        return *this;
    }

    /**
     * Set sfOverpaymentFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setOverpaymentFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentFee] = value;
        return *this;
    }

    /**
     * Set sfInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfInterestRate] = value;
        return *this;
    }

    /**
     * Set sfLateInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setLateInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLateInterestRate] = value;
        return *this;
    }

    /**
     * Set sfCloseInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setCloseInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseInterestRate] = value;
        return *this;
    }

    /**
     * Set sfOverpaymentInterestRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setOverpaymentInterestRate(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOverpaymentInterestRate] = value;
        return *this;
    }

    /**
     * Set sfPrincipalRequested (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPrincipalRequested(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfPrincipalRequested] = value;
        return *this;
    }

    /**
     * Set sfPaymentTotal (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPaymentTotal(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentTotal] = value;
        return *this;
    }

    /**
     * Set sfPaymentInterval (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setPaymentInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPaymentInterval] = value;
        return *this;
    }

    /**
     * Set sfGracePeriod (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanSetBuilder&
    setGracePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfGracePeriod] = value;
        return *this;
    }

    /**
     * Build and return the LoanSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
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
