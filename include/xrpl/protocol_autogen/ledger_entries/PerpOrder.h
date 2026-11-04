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

class PerpOrderBuilder;

/**
 * @brief Ledger Entry: PerpOrder
 *
 * Type: ltPERP_ORDER (0x008E)
 * RPC Name: perp_order
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use PerpOrderBuilder to construct new ledger entries.
 */
class PerpOrder : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltPERP_ORDER;

    /**
     * @brief Construct a PerpOrder ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit PerpOrder(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpOrder");
        }
    }

    // Ledger entry-specific field getters

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

    /**
     * @brief Get sfAccount (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getAccount() const
    {
        return this->sle_->at(sfAccount);
    }

    /**
     * @brief Get sfSequence (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT32::type::value_type
    getSequence() const
    {
        return this->sle_->at(sfSequence);
    }

    /**
     * @brief Get sfMarginAccountID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarginAccountID() const
    {
        return this->sle_->at(sfMarginAccountID);
    }

    /**
     * @brief Get sfMarketID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getMarketID() const
    {
        return this->sle_->at(sfMarketID);
    }

    /**
     * @brief Get sfLimitPrice (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getLimitPrice() const
    {
        return this->sle_->at(sfLimitPrice);
    }

    /**
     * @brief Get sfSize (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_NUMBER::type::value_type
    getSize() const
    {
        return this->sle_->at(sfSize);
    }

    /**
     * @brief Get sfExpiration (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getExpiration() const
    {
        if (hasExpiration())
            return this->sle_->at(sfExpiration);
        return std::nullopt;
    }

    /**
     * @brief Check if sfExpiration is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasExpiration() const
    {
        return this->sle_->isFieldPresent(sfExpiration);
    }

    /**
     * @brief Get sfBuilder (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_ACCOUNT::type::value_type>
    getBuilder() const
    {
        if (hasBuilder())
            return this->sle_->at(sfBuilder);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilder is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilder() const
    {
        return this->sle_->isFieldPresent(sfBuilder);
    }

    /**
     * @brief Get sfBuilderFee (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getBuilderFee() const
    {
        if (hasBuilderFee())
            return this->sle_->at(sfBuilderFee);
        return std::nullopt;
    }

    /**
     * @brief Check if sfBuilderFee is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasBuilderFee() const
    {
        return this->sle_->isFieldPresent(sfBuilderFee);
    }

    /**
     * @brief Get sfBookDirectory (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT256::type::value_type
    getBookDirectory() const
    {
        return this->sle_->at(sfBookDirectory);
    }

    /**
     * @brief Get sfBookNode (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT64::type::value_type
    getBookNode() const
    {
        return this->sle_->at(sfBookNode);
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
};

/**
 * @brief Builder for PerpOrder ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class PerpOrderBuilder : public LedgerEntryBuilderBase<PerpOrderBuilder>
{
public:
    /**
     * @brief Construct a new PerpOrderBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param account The sfAccount field value.
     * @param sequence The sfSequence field value.
     * @param marginAccountID The sfMarginAccountID field value.
     * @param marketID The sfMarketID field value.
     * @param limitPrice The sfLimitPrice field value.
     * @param size The sfSize field value.
     * @param bookDirectory The sfBookDirectory field value.
     * @param bookNode The sfBookNode field value.
     * @param ownerNode The sfOwnerNode field value.
     */
    PerpOrderBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT32::type::value_type> const& sequence,std::decay_t<typename SF_UINT256::type::value_type> const& marginAccountID,std::decay_t<typename SF_UINT256::type::value_type> const& marketID,std::decay_t<typename SF_NUMBER::type::value_type> const& limitPrice,std::decay_t<typename SF_NUMBER::type::value_type> const& size,std::decay_t<typename SF_UINT256::type::value_type> const& bookDirectory,std::decay_t<typename SF_UINT64::type::value_type> const& bookNode,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode)
        : LedgerEntryBuilderBase<PerpOrderBuilder>(ltPERP_ORDER)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setAccount(account);
        setSequence(sequence);
        setMarginAccountID(marginAccountID);
        setMarketID(marketID);
        setLimitPrice(limitPrice);
        setSize(size);
        setBookDirectory(bookDirectory);
        setBookNode(bookNode);
        setOwnerNode(ownerNode);
    }

    /**
     * @brief Construct a PerpOrderBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    PerpOrderBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltPERP_ORDER)
        {
            throw std::runtime_error("Invalid ledger entry type for PerpOrder");
        }
        object_ = *sle;
    }

    /**
     * @brief Ledger entry-specific field setters
     */

    /**
     * @brief Set sfPreviousTxnID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfSequence (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setSequence(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfSequence] = value;
        return *this;
    }

    /**
     * @brief Set sfMarginAccountID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setMarginAccountID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarginAccountID] = value;
        return *this;
    }

    /**
     * @brief Set sfMarketID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setMarketID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfMarketID] = value;
        return *this;
    }

    /**
     * @brief Set sfLimitPrice (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setLimitPrice(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfLimitPrice] = value;
        return *this;
    }

    /**
     * @brief Set sfSize (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setSize(std::decay_t<typename SF_NUMBER::type::value_type> const& value)
    {
        object_[sfSize] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilder (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setBuilder(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfBuilder] = value;
        return *this;
    }

    /**
     * @brief Set sfBuilderFee (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setBuilderFee(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfBuilderFee] = value;
        return *this;
    }

    /**
     * @brief Set sfBookDirectory (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setBookDirectory(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfBookDirectory] = value;
        return *this;
    }

    /**
     * @brief Set sfBookNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setBookNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfBookNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    PerpOrderBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed PerpOrder wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    PerpOrder
    build(UInt256 const& index)
    {
        return PerpOrder{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
