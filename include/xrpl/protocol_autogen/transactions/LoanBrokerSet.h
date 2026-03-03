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
class LoanBrokerSetBuilder;

/**
 * Transaction: LoanBrokerSet
 * Type: ttLOAN_BROKER_SET (74)
 * Delegable: Delegation::delegable
 * Amendment: featureLendingProtocol
 * Privileges: createPseudoAcct | mayAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use LoanBrokerSetBuilder to construct new transactions.
 */
class LoanBrokerSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttLOAN_BROKER_SET;

    /**
     * Construct a LoanBrokerSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit LoanBrokerSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfVaultID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getVaultID() const
    {
        return this->tx_.at(sfVaultID);
    }

    /**
     * Get sfLoanBrokerID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getLoanBrokerID() const
    {
        if (hasLoanBrokerID())
        {
            return this->tx_.at(sfLoanBrokerID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasLoanBrokerID() const
    {
        return this->tx_.isFieldPresent(sfLoanBrokerID);
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
            return this->tx_.at(sfData);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_.isFieldPresent(sfData);
    }

    /**
     * Get sfManagementFeeRate (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getManagementFeeRate() const
    {
        if (hasManagementFeeRate())
        {
            return this->tx_.at(sfManagementFeeRate);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasManagementFeeRate() const
    {
        return this->tx_.isFieldPresent(sfManagementFeeRate);
    }

    /**
     * Get sfDebtMaximum (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getDebtMaximum() const
    {
        if (hasDebtMaximum())
        {
            return this->tx_.at(sfDebtMaximum);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDebtMaximum() const
    {
        return this->tx_.isFieldPresent(sfDebtMaximum);
    }

    /**
     * Get sfCoverRateMinimum (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateMinimum() const
    {
        if (hasCoverRateMinimum())
        {
            return this->tx_.at(sfCoverRateMinimum);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCoverRateMinimum() const
    {
        return this->tx_.isFieldPresent(sfCoverRateMinimum);
    }

    /**
     * Get sfCoverRateLiquidation (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCoverRateLiquidation() const
    {
        if (hasCoverRateLiquidation())
        {
            return this->tx_.at(sfCoverRateLiquidation);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasCoverRateLiquidation() const
    {
        return this->tx_.isFieldPresent(sfCoverRateLiquidation);
    }
};

/**
 * Builder for LoanBrokerSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class LoanBrokerSetBuilder : public TransactionBuilderBase<LoanBrokerSetBuilder>
{
public:
    LoanBrokerSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& vaultID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<LoanBrokerSetBuilder>(ttLOAN_BROKER_SET, account, sequence, fee)
    {
        setVaultID(vaultID);
    }

    LoanBrokerSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttLOAN_BROKER_SET)
        {
            throw std::runtime_error("Invalid transaction type for LoanBrokerSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfVaultID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setVaultID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfVaultID] = value;
        return *this;
    }

    /**
     * Set sfLoanBrokerID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setLoanBrokerID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfLoanBrokerID] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Set sfManagementFeeRate (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setManagementFeeRate(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfManagementFeeRate] = value;
        return *this;
    }

    /**
     * Set sfDebtMaximum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setDebtMaximum(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfDebtMaximum] = value;
        return *this;
    }

    /**
     * Set sfCoverRateMinimum (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setCoverRateMinimum(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateMinimum] = value;
        return *this;
    }

    /**
     * Set sfCoverRateLiquidation (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    LoanBrokerSetBuilder&
    setCoverRateLiquidation(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCoverRateLiquidation] = value;
        return *this;
    }

    /**
     * Build and return the completed LoanBrokerSet wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    LoanBrokerSet
    build()
    {
        return LoanBrokerSet(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions
