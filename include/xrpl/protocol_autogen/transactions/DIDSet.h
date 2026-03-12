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

class DIDSetBuilder;

/**
 * @brief Transaction: DIDSet
 *
 * Type: ttDID_SET (49)
 * Delegable: Delegation::delegable
 * Amendment: featureDID
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use DIDSetBuilder to construct new transactions.
 */
class DIDSet : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttDID_SET;

    /**
     * @brief Construct a DIDSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DIDSet(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DIDSet");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfDIDDocument (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getDIDDocument() const
    {
        if (hasDIDDocument())
        {
            return this->tx_->at(sfDIDDocument);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfDIDDocument is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasDIDDocument() const
    {
        return this->tx_->isFieldPresent(sfDIDDocument);
    }

    /**
     * @brief Get sfURI (soeOPTIONAL)
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
};

/**
 * @brief Builder for DIDSet transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DIDSetBuilder : public TransactionBuilderBase<DIDSetBuilder>
{
public:
    /**
     * @brief Construct a new DIDSetBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    DIDSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DIDSetBuilder>(ttDID_SET, account, sequence, fee)
    {
    }

    /**
     * @brief Construct a DIDSetBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    DIDSetBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttDID_SET)
        {
            throw std::runtime_error("Invalid transaction type for DIDSetBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfDIDDocument (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setDIDDocument(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDIDDocument] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * @brief Build and return the DIDSet wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    DIDSet
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return DIDSet{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
