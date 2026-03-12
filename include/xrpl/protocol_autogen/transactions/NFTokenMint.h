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
class NFTokenMintBuilder;

/**
 * Transaction: NFTokenMint
 * Type: ttNFTOKEN_MINT (25)
 * Delegable: Delegation::delegable
 * Amendment: uint256{}
 * Privileges: changeNFTCounts
 *
 * Immutable wrapper around STTx providing type-safe field access.
 * Use NFTokenMintBuilder to construct new transactions.
 */
class NFTokenMint : public TransactionBase
{
public:
    static constexpr xrpl::TxType txType = ttNFTOKEN_MINT;

    /**
     * Construct a NFTokenMint transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit NFTokenMint(std::shared_ptr<STTx const> tx)
        : TransactionBase(std::move(tx))
    {
        // Verify transaction type
        if (tx_->getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenMint");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfNFTokenTaxon (soeREQUIRED)
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getNFTokenTaxon() const
    {
        return this->tx_->at(sfNFTokenTaxon);
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
     * Get sfIssuer (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getIssuer() const
    {
        if (hasIssuer())
        {
            return this->tx_->at(sfIssuer);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasIssuer() const
    {
        return this->tx_->isFieldPresent(sfIssuer);
    }

    /**
     * Get sfURI (soeOPTIONAL)
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

    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_->isFieldPresent(sfURI);
    }

    /**
     * Get sfAmount (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAmount() const
    {
        if (hasAmount())
        {
            return this->tx_->at(sfAmount);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasAmount() const
    {
        return this->tx_->isFieldPresent(sfAmount);
    }

    /**
     * Get sfDestination (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getDestination() const
    {
        if (hasDestination())
        {
            return this->tx_->at(sfDestination);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDestination() const
    {
        return this->tx_->isFieldPresent(sfDestination);
    }

    /**
     * Get sfExpiration (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
        {
            return this->tx_->at(sfExpiration);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->tx_->isFieldPresent(sfExpiration);
    }
};

/**
 * Builder for NFTokenMint transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class NFTokenMintBuilder : public TransactionBuilderBase<NFTokenMintBuilder>
{
public:
    NFTokenMintBuilder(SF_ACCOUNT::type::value_type account,
                     std::decay_t<typename SF_UINT32::type::value_type> const& nFTokenTaxon,                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<NFTokenMintBuilder>(ttNFTOKEN_MINT, account, sequence, fee)
    {
        setNFTokenTaxon(nFTokenTaxon);
    }

    NFTokenMintBuilder(std::shared_ptr<STTx const> tx)
    {
        if (tx->getTxnType() != ttNFTOKEN_MINT)
        {
            throw std::runtime_error("Invalid transaction type for NFTokenMintBuilder");
        }
        object_ = *tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfNFTokenTaxon (soeREQUIRED)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setNFTokenTaxon(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfNFTokenTaxon] = value;
        return *this;
    }

    /**
     * Set sfTransferFee (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * Set sfIssuer (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * Set sfAmount (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAmount] = value;
        return *this;
    }

    /**
     * Set sfDestination (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setDestination(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfDestination] = value;
        return *this;
    }

    /**
     * Set sfExpiration (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    NFTokenMintBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * Build and return the NFTokenMint wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    NFTokenMint
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return NFTokenMint{std::make_shared<STTx>(std::move(object_))};
    }
};

}  // namespace xrpl::transactions
