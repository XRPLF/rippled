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

class BallotCreateBuilder;

/**
 * @brief Transaction: BallotCreate
 *
 * Type: ttBALLOT_CREATE (106)
 * Delegable: Delegation::NotDelegable
 * Amendment: featureConfidentialVoting
 * Privileges: Privilege::NoPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use BallotCreateBuilder to construct new transactions.
 */
class BallotCreate : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttBALLOT_CREATE;

    /**
     * @brief Construct a BallotCreate transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit BallotCreate(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for BallotCreate");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfMPTokenIssuanceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT192::type::value_type>
    getMPTokenIssuanceID() const
    {
        if (hasMPTokenIssuanceID())
        {
            return this->tx_->at(sfMPTokenIssuanceID);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenIssuanceID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenIssuanceID() const
    {
        return this->tx_->isFieldPresent(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfDomainID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
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

    /**
     * @brief Check if sfDomainID is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDomainID() const
    {
        return this->tx_->isFieldPresent(sfDomainID);
    }

    /**
     * @brief Get sfDigest (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getDigest() const
    {
        return this->tx_->at(sfDigest);
    }

    /**
     * @brief Get sfURI (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getURI() const
    {
        if (hasURI())
        {
            return this->tx_->at(sfURI);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfURI is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_->isFieldPresent(sfURI);
    }

    /**
     * @brief Get sfOptionCount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT8::type::value_type
    getOptionCount() const
    {
        return this->tx_->at(sfOptionCount);
    }

    /**
     * @brief Get sfTallyPublicKey (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getTallyPublicKey() const
    {
        return this->tx_->at(sfTallyPublicKey);
    }

    /**
     * @brief Get sfAuditorEncryptionKey (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getAuditorEncryptionKey() const
    {
        if (hasAuditorEncryptionKey())
        {
            return this->tx_->at(sfAuditorEncryptionKey);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfAuditorEncryptionKey is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAuditorEncryptionKey() const
    {
        return this->tx_->isFieldPresent(sfAuditorEncryptionKey);
    }

    /**
     * @brief Get sfOpenTime (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getOpenTime() const
    {
        return this->tx_->at(sfOpenTime);
    }

    /**
     * @brief Get sfCloseTime (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getCloseTime() const
    {
        return this->tx_->at(sfCloseTime);
    }

    /**
     * @brief Get sfZKProof (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_VL::type::value_type
    getZKProof() const
    {
        return this->tx_->at(sfZKProof);
    }
};

/**
 * @brief Builder for BallotCreate transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses STObject internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class BallotCreateBuilder : public TransactionBuilderBase<BallotCreateBuilder>
{
public:
    /**
     * @brief Construct a new BallotCreateBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param digest The sfDigest field value.
     * @param optionCount The sfOptionCount field value.
     * @param tallyPublicKey The sfTallyPublicKey field value.
     * @param openTime The sfOpenTime field value.
     * @param closeTime The sfCloseTime field value.
     * @param zKProof The sfZKProof field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    BallotCreateBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& digest,                     std::decay_t<typename SF_UINT8::type::value_type> const& optionCount,                     std::decay_t<typename SF_VL::type::value_type> const& tallyPublicKey,                     std::decay_t<typename SF_UINT32::type::value_type> const& openTime,                     std::decay_t<typename SF_UINT32::type::value_type> const& closeTime,                     std::decay_t<typename SF_VL::type::value_type> const& zKProof,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<BallotCreateBuilder>(ttBALLOT_CREATE, account, sequence, fee)
    {
        setDigest(digest);
        setOptionCount(optionCount);
        setTallyPublicKey(tallyPublicKey);
        setOpenTime(openTime);
        setCloseTime(closeTime);
        setZKProof(zKProof);
    }

    /**
     * @brief Construct a BallotCreateBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    BallotCreateBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttBALLOT_CREATE)
        {
            throw std::runtime_error("Invalid transaction type for BallotCreateBuilder");
        }
        object_ = *tx;
    }

    /**
     * @brief Transaction-specific field setters
     */

    /**
     * @brief Set sfMPTokenIssuanceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfDomainID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setDomainID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDomainID] = value;
        return *this;
    }

    /**
     * @brief Set sfDigest (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setDigest(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfDigest] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfOptionCount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setOptionCount(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfOptionCount] = value;
        return *this;
    }

    /**
     * @brief Set sfTallyPublicKey (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setTallyPublicKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfTallyPublicKey] = value;
        return *this;
    }

    /**
     * @brief Set sfAuditorEncryptionKey (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setAuditorEncryptionKey(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfAuditorEncryptionKey] = value;
        return *this;
    }

    /**
     * @brief Set sfOpenTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setOpenTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfOpenTime] = value;
        return *this;
    }

    /**
     * @brief Set sfCloseTime (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setCloseTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCloseTime] = value;
        return *this;
    }

    /**
     * @brief Set sfZKProof (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    BallotCreateBuilder&
    setZKProof(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfZKProof] = value;
        return *this;
    }

    /**
     * @brief Build and return the BallotCreate wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    BallotCreate
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return BallotCreate{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
