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

class SponsorshipTransferBuilder;

/**
 * @brief Transaction: SponsorshipTransfer
 *
 * Type: ttSPONSORSHIP_TRANSFER (85)
 * Delegable: Delegation::delegable
 * Amendment: featureSponsor
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use SponsorshipTransferBuilder to construct new transactions.
 */
class SponsorshipTransfer : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttSPONSORSHIP_TRANSFER;

    /**
     * @brief Construct a SponsorshipTransfer transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit SponsorshipTransfer(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for SponsorshipTransfer");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfObjectID (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT256::type::value_type>
    getObjectID() const
    {
        if (hasObjectID())
        {
            return this->tx_->at(sfObjectID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfObjectID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasObjectID() const
    {
        return this->tx_->isFieldPresent(sfObjectID);
    }

    /**
     * @brief Get sfSponsee (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getSponsee() const
    {
        if (hasSponsee())
        {
            return this->tx_->at(sfSponsee);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfSponsee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasSponsee() const
    {
        return this->tx_->isFieldPresent(sfSponsee);
    }
};

/**
 * @brief Builder for SponsorshipTransfer transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class SponsorshipTransferBuilder : public TransactionBuilderBase<SponsorshipTransferBuilder>
{
public:
    /**
     * @brief Construct a new SponsorshipTransferBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    SponsorshipTransferBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<SponsorshipTransferBuilder>(ttSPONSORSHIP_TRANSFER, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a SponsorshipTransferBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    SponsorshipTransferBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttSPONSORSHIP_TRANSFER)
        {
            throw std::runtime_error("Invalid transaction type for SponsorshipTransferBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfObjectID (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipTransferBuilder&
    setObjectID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfObjectID] = value;
        return *this;
    }

    /**
     * @brief Set sfSponsee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    SponsorshipTransferBuilder&
    setSponsee(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfSponsee] = value;
        return *this;
    }

    /**
     * @brief Build and return the SponsorshipTransfer wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    SponsorshipTransfer
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return SponsorshipTransfer{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
