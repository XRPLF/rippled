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

class NFTokenModifyBuilder;

/**
 * @brief Transaction: NFTokenModify
 *
 * Type: ttNFTOKEN_MODIFY (61)
 * Delegable: Delegation::delegable
 * Amendment: featureDynamicNFT
 * Privileges: noPriv
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenModifyBuilder to construct new transactions.
 */
class NFTokenModify : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_MODIFY;

    /**
     * @brief Construct a NFTokenModify transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenModify(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenModify");
        }
    }

    // Transaction-specific field getters

    /**
     * @brief Get sfNFTokenID (soeREQUIRED)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getNFTokenID() const
    {
        return this->tx_->at(sfNFTokenID);
    }

    /**
     * @brief Get sfOwner (soeOPTIONAL)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_->at(sfOwner);
        }
        return std::nullopt;
    }

    /**
     * @brief Check if sfOwner is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_->isFieldPresent(sfOwner);
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
};

/**
 * @brief Builder for NFTokenModify transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenModifyBuilder : public TransactionBuilderBase<NFTokenModifyBuilder>
{
public:
    /**
     * @brief Construct a new NFTokenModifyBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param nFTokenID The sfNFTokenID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    NFTokenModifyBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<NFTokenModifyBuilder>(ttNFTOKEN_MODIFY, account, sequence, fee)
    {
        setNFTokenID(nFTokenID);
    }

    /**
     * @brief Construct a NFTokenModifyBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    NFTokenModifyBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttNFTOKEN_MODIFY)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenModifyBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfNFTokenID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenModifyBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenModifyBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenModifyBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * @brief Build and return the NFTokenModify wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    NFTokenModify
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return NFTokenModify{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
