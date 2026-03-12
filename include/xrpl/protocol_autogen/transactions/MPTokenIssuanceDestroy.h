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

class MPTokenIssuanceDestroyBuilder;

/**
 * @brief Transaction: MPTokenIssuanceDestroy
 *
 * Type: ttMPTOKEN_ISSUANCE_DESTROY (55)
 * Delegable: Delegation::delegable
 * Amendment: featureMPTokensV1
 * Privileges: destroyMPTIssuance
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use MPTokenIssuanceDestroyBuilder to construct new transactions.
 */
class MPTokenIssuanceDestroy : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttMPTOKEN_ISSUANCE_DESTROY;

    /**
     * @brief Construct a MPTokenIssuanceDestroy transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit MPTokenIssuanceDestroy(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceDestroy");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMPTokenIssuanceID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->tx_->at(sfMPTokenIssuanceID);
    }
};

/**
 * @brief Builder for MPTokenIssuanceDestroy transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class MPTokenIssuanceDestroyBuilder : public TransactionBuilderBase<MPTokenIssuanceDestroyBuilder>
{
public:
    /**
     * @brief Construct a new MPTokenIssuanceDestroyBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    MPTokenIssuanceDestroyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<MPTokenIssuanceDestroyBuilder>(ttMPTOKEN_ISSUANCE_DESTROY, account, sequence, fee)
    {
        setMPTokenIssuanceID(mPTokenIssuanceID);
    }

    /**
     * @brief Construct a MPTokenIssuanceDestroyBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    MPTokenIssuanceDestroyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttMPTOKEN_ISSUANCE_DESTROY)
        {
            throw std::runtime_error("Invalid transaction type for MPTokenIssuanceDestroyBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfMPTokenIssuanceID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    MPTokenIssuanceDestroyBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Build and return the MPTokenIssuanceDestroy wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    MPTokenIssuanceDestroy
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return MPTokenIssuanceDestroy{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
