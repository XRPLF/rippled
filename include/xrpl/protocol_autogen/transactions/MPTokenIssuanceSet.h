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
class MPTokenIssuanceSetBuilder;

/**
 * Transaction: MPTokenIssuanceSet
 * Type: ttMPTOKEN_ISSUANCE_SET (56)
 * Delegable: Delegation::delegable
 * Amendment: featureMPTokensV1
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MPTokenIssuanceSetBuilder to construct new transactions.
 */
class MPTokenIssuanceSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMPTOKEN_ISSUANCE_SET;

    /**
     * Construct a MPTokenIssuanceSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MPTokenIssuanceSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfMPTokenIssuanceID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_->at(sfMPTokenIssuanceID);
    }

    /**
     * Get sfHolder (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getHolder() const
    {
        if (hasHolder())
        {
            return this->tx_->at(sfHolder);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasHolder() const
    {
        return this->tx_->isFieldPresent(sfHolder);
    }

    /**
     * Get sfDomainID (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getDomainID() const
    {
        if (hasDomainID())
        {
            return this->tx_->at(sfDomainID);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }

    /**
     * Get sfMPTokenMetadata (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
        {
            return this->tx_->at(sfMPTokenMetadata);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->tx_->isFieldPresent(sfMPTokenMetadata);
    }

    /**
     * Get sfTransferFee (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTransferFee() const
    {
        if (hasTransferFee())
        {
            return this->tx_->at(sfTransferFee);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasTransferFee() const
    {
        return this->tx_->isFieldPresent(sfTransferFee);
    }

    /**
     * Get sfMutableFlags (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getMutableFlags() const
    {
        if (hasMutableFlags())
        {
            return this->tx_->at(sfMutableFlags);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasMutableFlags() const
    {
        return this->tx_->isFieldPresent(sfMutableFlags);
    }
};

/**
 * Builder for MPTokenIssuanceSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MPTokenIssuanceSetBuilder : public TransactionBuilderBase<MPTokenIssuanceSetBuilder>
{
public:
    MPTokenIssuanceSetBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MPTokenIssuanceSetBuilder>(ttMPTOKEN_ISSUANCE_SET, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    MPTokenIssuanceSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMPTOKEN_ISSUANCE_SET)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceSetBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * Set sfHolder (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * Set sfDomainID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * Set sfMPTokenMetadata (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * Set sfTransferFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * Set sfMutableFlags (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceSetBuilder&
    setMutableFlags(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfMutableFlags] = value;
        return *this;
    }

    /**
     * Build and return the MPTokenIssuanceSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    MPTokenIssuanceSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MPTokenIssuanceSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
