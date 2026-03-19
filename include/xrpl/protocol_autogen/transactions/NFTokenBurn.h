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

class NFTokenBurnBuilder;

/**
 * @brief Transaction: NFTokenBurn
 *
 * Type: ttNFTOKEN_BURN (26)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: changeNFTCounts
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenBurnBuilder to construct new transactions.
 */
class NFTokenBurn : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_BURN;

    /**
     * @brief Construct a NFTokenBurn transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenBurn(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenBurn");
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
};

/**
 * @brief Builder for NFTokenBurn transactions.
 *
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenBurnBuilder : public TransactionBuilderBase<NFTokenBurnBuilder>
{
public:
    /**
     * @brief Construct a new NFTokenBurnBuilder with required fields.
     * @param account The account initiating the transaction.
     * @param nFTokenID The sfNFTokenID field value.
     * @param sequence Optional sequence number for the transaction.
     * @param fee Optional fee for the transaction.
     */
    NFTokenBurnBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<NFTokenBurnBuilder>(ttNFTOKEN_BURN, account, sequence, fee)
    {
        setNFTokenID(nFTokenID);
    }

    /**
     * @brief Construct a NFTokenBurnBuilder from an existing STTx object.
     * @param tx The existing transaction to copy from.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    NFTokenBurnBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttNFTOKEN_BURN)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenBurnBuilder");
        }
        object_ = *tx;
    }

    /** @brief Transaction-specific field setters */

    /**
     * @brief Set sfNFTokenID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenBurnBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenBurnBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Build and return the NFTokenBurn wrapper.
     * @param publicKey The public key for signing.
     * @param secretKey The secret key for signing.
     * @return The constructed transaction wrapper.
     */
    NFTokenBurn
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return NFTokenBurn{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
