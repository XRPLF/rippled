// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/Owning.h>
#include <xrpl/protocol_autogen/TransactionBase.h>
#include <xrpl/protocol_autogen/TransactionBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::transactions {

// Forward declaration
class DIDSetBuilder;

/**
 * Transaction: DIDSet
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
     * Construct a DIDSet transaction wrapper from an existing STTx object.
     * @throws std::runtime_error if the transaction type doesn't match.
     */
    explicit DIDSet(STTx const& tx)
        : TransactionBase(tx)
    {
        // Verify transaction type
        if (tx.getTxnType() != txType)
        {
            throw std::runtime_error("Invalid transaction type for DIDSet");
        }
    }

    // Transaction-specific field getters

    /**
     * Get sfDIDDocument (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getDIDDocument() const
    {
        if (hasDIDDocument())
        {
            return this->tx_.at(sfDIDDocument);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasDIDDocument() const
    {
        return this->tx_.isFieldPresent(sfDIDDocument);
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
            return this->tx_.at(sfURI);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasURI() const
    {
        return this->tx_.isFieldPresent(sfURI);
    }

    /**
     * Get sfData (soeOPTIONAL)
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getData() const
    {
        if (hasData())
        {
            return this->tx_.at(sfData);
        }
        return std::nullopt;
    }

    [[nodiscard]]
    bool
    hasData() const
    {
        return this->tx_.isFieldPresent(sfData);
    }
};

/**
 * Builder for DIDSet transactions.
 * Provides a fluent interface for constructing transactions with method chaining.
 * Uses Json::Value internally for flexible transaction construction.
 * Inherits common field setters from TransactionBuilderBase.
 */
class DIDSetBuilder : public TransactionBuilderBase<DIDSetBuilder>
{
public:
    DIDSetBuilder(SF_ACCOUNT::type::value_type account,
                    std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                    std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt
)
        : TransactionBuilderBase<DIDSetBuilder>(ttDID_SET, account, sequence, fee)
    {
    }

    DIDSetBuilder(STTx const& tx)
    {
        if (tx.getTxnType() != ttDID_SET)
        {
            throw std::runtime_error("Invalid transaction type for DIDSetBuilder");
        }
        object_ = tx;
    }

    // Transaction-specific field setters

    /**
     * Set sfDIDDocument (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setDIDDocument(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfDIDDocument] = value;
        return *this;
    }

    /**
     * Set sfURI (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setURI(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfURI] = value;
        return *this;
    }

    /**
     * Set sfData (soeOPTIONAL)
     * @return Reference to this builder for method chaining.
     */
    DIDSetBuilder&
    setData(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfData] = value;
        return *this;
    }

    /**
     * Build and return the completed DIDSet wrapper.
     * @param publicKey The public key for signing
     * @param secretKey The secret key for signing
     * @return The constructed transaction wrapper.
     */
    protocol_autogen::Owning<STTx, DIDSet>
    build(PublicKey const& publicKey, SecretKey const& secretKey)
    {
        sign(publicKey, secretKey);
        return protocol_autogen::Owning<STTx, DIDSet>{STTx{std::move(object_)}};
    }
};

}  // namespace xrpl::transactions
