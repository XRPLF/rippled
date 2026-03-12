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
class MPTokenAuthorizeBuilder;

/**
 * Transaction: MPTokenAuthorize
 * Type: ttMPTOKEN_AUTHORIZE (57)
 * Delegable: Delegation::delegable
 * Amendment: featureMPTokensV1
 * Privileges: mustAuthorizeMPT
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MPTokenAuthorizeBuilder to construct new transactions.
 */
class MPTokenAuthorize : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMPTOKEN_AUTHORIZE;

    /**
     * Construct a MPTokenAuthorize transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MPTokenAuthorize(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenAuthorize");
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
};

/**
 * Builder for MPTokenAuthorize transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MPTokenAuthorizeBuilder : public TransactionBuilderBase<MPTokenAuthorizeBuilder>
{
public:
    MPTokenAuthorizeBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MPTokenAuthorizeBuilder>(ttMPTOKEN_AUTHORIZE, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    MPTokenAuthorizeBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMPTOKEN_AUTHORIZE)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenAuthorizeBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenAuthorizeBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * Set sfHolder (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    MPTokenAuthorizeBuilder&
    setHolder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfHolder] = value;
        return *this;
    }

    /**
     * Build and return the MPTokenAuthorize wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    MPTokenAuthorize
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MPTokenAuthorize{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
