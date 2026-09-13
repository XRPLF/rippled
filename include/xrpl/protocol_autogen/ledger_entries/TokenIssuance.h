// This file is auto-generated. Do not edit.
#pragma once

#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol_autogen/LedgerEntryBase.h>
#include <xrpl/protocol_autogen/LedgerEntryBuilderBase.h>
#include <xrpl/json/json_value.h>

#include <stdexcept>
#include <optional>

namespace xrpl::ledger_entries {

class TokenIssuanceBuilder;

/**
 * @brief Ledger Entry: TokenIssuance
 *
 * Type: ltTOKEN_ISSUANCE (0x0092)
 * RPC Name: token_issuance
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use TokenIssuanceBuilder to construct new ledger entries.
 */
class TokenIssuance : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltTOKEN_ISSUANCE;

    /**
     * @brief Construct a TokenIssuance ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit TokenIssuance(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for TokenIssuance");
        }
    }

    // Ledger entry-specific field getters

    /**
     * @brief Get sfIssuer (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getIssuer() const
    {
        return this->sle_->at(sfIssuer);
    }

    /**
     * @brief Get sfCurrency (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_CURRENCY::type::value_type
    getCurrency() const
    {
        return this->sle_->at(sfCurrency);
    }

    /**
     * @brief Get sfMaximumAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT64::type::value_type>
    getMaximumAmount() const
    {
        if (hasMaximumAmount())
            return this->sle_->at(sfMaximumAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMaximumAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMaximumAmount() const
    {
        return this->sle_->isFieldPresent(sfMaximumAmount);
    }

    /**
     * @brief Get sfIssuedAmount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_NUMBER::type::value_type>
    getIssuedAmount() const
    {
        if (hasIssuedAmount())
            return this->sle_->at(sfIssuedAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfIssuedAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasIssuedAmount() const
    {
        return this->sle_->isFieldPresent(sfIssuedAmount);
    }

    /**
     * @brief Get sfTokenScale (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT8::type::value_type>
    getTokenScale() const
    {
        if (hasTokenScale())
            return this->sle_->at(sfTokenScale);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTokenScale is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTokenScale() const
    {
        return this->sle_->isFieldPresent(sfTokenScale);
    }

    /**
     * @brief Get sfMPTokenIssuanceID (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT192::type::value_type>
    getMPTokenIssuanceID() const
    {
        if (hasMPTokenIssuanceID())
            return this->sle_->at(sfMPTokenIssuanceID);
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
        return this->sle_->isFieldPresent(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfTransferFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT16::type::value_type>
    getTransferFee() const
    {
        if (hasTransferFee())
            return this->sle_->at(sfTransferFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfTransferFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasTransferFee() const
    {
        return this->sle_->isFieldPresent(sfTransferFee);
    }

    /**
     * @brief Get sfMPTokenMetadata (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_VL::type::value_type>
    getMPTokenMetadata() const
    {
        if (hasMPTokenMetadata())
            return this->sle_->at(sfMPTokenMetadata);
        return std::nullopt;
    }

    /**
     * @brief Check if sfMPTokenMetadata is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasMPTokenMetadata() const
    {
        return this->sle_->isFieldPresent(sfMPTokenMetadata);
    }

    /**
     * @brief Get sfOwnerNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getOwnerNode() const
    {
        return this->sle_->at(sfOwnerNode);
    }

    /**
     * @brief Get sfPreviousTxnID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getPreviousTxnID() const
    {
        return this->sle_->at(sfPreviousTxnID);
    }

    /**
     * @brief Get sfPreviousTxnLgrSeq (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getPreviousTxnLgrSeq() const
    {
        return this->sle_->at(sfPreviousTxnLgrSeq);
    }
};

/**
 * @brief Builder for TokenIssuance ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class TokenIssuanceBuilder : public LedgerEntryBuilderBase<TokenIssuanceBuilder>
{
public:
    /**
     * @brief Construct a new TokenIssuanceBuilder with required fields.
     * @param issuer The sfIssuer field value.
     * @param currency The sfCurrency field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     */
    TokenIssuanceBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& issuer,std::decay_t<typename SF_CURRENCY::type::value_type> const& currency,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq)
        : LedgerEntryBuilderBase<TokenIssuanceBuilder>(ltTOKEN_ISSUANCE)
    {
        setIssuer(issuer);
        setCurrency(currency);
        setOwnerNode(ownerNode);
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
    }

    /**
     * @brief Construct a TokenIssuanceBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    TokenIssuanceBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltTOKEN_ISSUANCE)
        {
            throw std::runtime_error("Invalid ledger entry type for TokenIssuance");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfIssuer (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setIssuer(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfIssuer] = value;
        return *this;
    }

    /**
     * @brief Set sfCurrency (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setCurrency(std::decay_t<typename SF_CURRENCY::type::value_type> const& value)
    {
        object_[sfCurrency] = value;
        return *this;
    }

    /**
     * @brief Set sfMaximumAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setMaximumAmount(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfMaximumAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfIssuedAmount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setIssuedAmount(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfIssuedAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfTokenScale (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setTokenScale(std::decay_t<typename SF_UINT8::type::value_type> const& value)
    {
        object_[sfTokenScale] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfTransferFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setTransferFee(std::decay_t<typename SF_UINT16::type::value_type> const& value)
    {
        object_[sfTransferFee] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenMetadata (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setMPTokenMetadata(std::decay_t<typename SF_VL::type::value_type> const& value)
    {
        object_[sfMPTokenMetadata] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    TokenIssuanceBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed TokenIssuance wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    TokenIssuance
    build(uint256 const& index)
    {
        return TokenIssuance{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
