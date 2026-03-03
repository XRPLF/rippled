#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

# cspell:words equalto

namespace xrpl::transactions {

// Forward declaration
class NFTokenBurnBuilder;

/**
 * Transaction: NFTokenBurn
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
     * Construct a NFTokenBurn transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenBurn(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenBurn");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfNFTokenID (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getNFTokenID() const
    {
        return this->tx_.at(sfNFTokenID);
    }

    /**
     * Get sfOwner (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getOwner() const
    {
        if (hasOwner())
        {
            return this->tx_.at(sfOwner);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasOwner() const
    {
        return this->tx_.isFieldPresent(sfOwner);
    }
};

/**
 * Builder for NFTokenBurn transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenBurnBuilder : public TransactionBuilderBase<NFTokenBurnBuilder>
{
public:
    NFTokenBurnBuilder(SF_ACCOUNT::type::value_type account,
                     SF_UINT32::type::value_type sequence,
                     SF_AMOUNT::type::value_type fee,
                     SF_VL::type::value_type signingPubKey,
                     std::decay_t<typename SF_UINT256::type::value_type> const& nFTokenID)
        : TransactionBuilderBase<NFTokenBurnBuilder>(account, sequence, fee, signingPubKey, ttNFTOKEN_BURN)
    {
        setNFTokenID(nFTokenID);
    }

    NFTokenBurnBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttNFTOKEN_BURN)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenBurnBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfNFTokenID (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenBurnBuilder&
    setNFTokenID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfNFTokenID] = value;
        return *this;
    }

    /**
     * Set sfOwner (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenBurnBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * Build and return the completed NFTokenBurn wrapper.
     * @return The constructed transaction wrapper.
     * @throws std::runtime_error if the JSON cannot be parsed into a valid transaction.
     */
    NFTokenBurn
    build()
    {
        return NFTokenBurn(STTx(std::move(object_)));
    }
};

}  // namespace xrpl::transactions