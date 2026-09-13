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

class CouponScheduleBuilder;

/**
 * @brief Ledger Entry: CouponSchedule
 *
 * Type: ltCOUPON_SCHEDULE (0x0093)
 * RPC Name: coupon_schedule
 *
 * Immutable wrapper around SLE providing type-safe field access.
 * Use CouponScheduleBuilder to construct new ledger entries.
 */
class CouponSchedule : public LedgerEntryBase
{
public:
    static constexpr LedgerEntryType entryType = ltCOUPON_SCHEDULE;

    /**
     * @brief Construct a CouponSchedule ledger entry wrapper from an existing SLE object.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    explicit CouponSchedule(SLE::const_pointer sle)
        : LedgerEntryBase(std::move(sle))
    {
        // Verify ledger entry type
        if (sle_->getType() != entryType)
        {
            throw std::runtime_error("Invalid ledger entry type for CouponSchedule");
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
     * @brief Get sfOwner (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ACCOUNT::type::value_type
    getOwner() const
    {
        return this->sle_->at(sfOwner);
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
     * @brief Get sfMPTokenIssuanceID (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_UINT192::type::value_type
    getMPTokenIssuanceID() const
    {
        return this->sle_->at(sfMPTokenIssuanceID);
    }

    /**
     * @brief Get sfCouponAsset (SoeRequired)
     * @return The field value.
     */
    [[nodiscard]]
    SF_ISSUE::type::value_type
    getCouponAsset() const
    {
        return this->sle_->at(sfCouponAsset);
    }

    /**
     * @brief Get sfAccruedPerUnit (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getAccruedPerUnit() const
    {
        if (hasAccruedPerUnit())
            return this->sle_->at(sfAccruedPerUnit);
        return std::nullopt;
    }

    /**
     * @brief Check if sfAccruedPerUnit is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasAccruedPerUnit() const
    {
        return this->sle_->isFieldPresent(sfAccruedPerUnit);
    }

    /**
     * @brief Get sfPoolAmount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getPoolAmount() const
    {
        if (hasPoolAmount())
            return this->sle_->at(sfPoolAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfPoolAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasPoolAmount() const
    {
        return this->sle_->isFieldPresent(sfPoolAmount);
    }

    /**
     * @brief Get sfClaimantCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getClaimantCount() const
    {
        if (hasClaimantCount())
            return this->sle_->at(sfClaimantCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfClaimantCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasClaimantCount() const
    {
        return this->sle_->isFieldPresent(sfClaimantCount);
    }

    /**
     * @brief Get sfCouponCount (SoeDefault)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCouponCount() const
    {
        if (hasCouponCount())
            return this->sle_->at(sfCouponCount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCouponCount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCouponCount() const
    {
        return this->sle_->isFieldPresent(sfCouponCount);
    }

    /**
     * @brief Get sfLastCouponTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getLastCouponTime() const
    {
        if (hasLastCouponTime())
            return this->sle_->at(sfLastCouponTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfLastCouponTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasLastCouponTime() const
    {
        return this->sle_->isFieldPresent(sfLastCouponTime);
    }

    /**
     * @brief Get sfCouponAmount (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_AMOUNT::type::value_type>
    getCouponAmount() const
    {
        if (hasCouponAmount())
            return this->sle_->at(sfCouponAmount);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCouponAmount is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCouponAmount() const
    {
        return this->sle_->isFieldPresent(sfCouponAmount);
    }

    /**
     * @brief Get sfCouponInterval (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCouponInterval() const
    {
        if (hasCouponInterval())
            return this->sle_->at(sfCouponInterval);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCouponInterval is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCouponInterval() const
    {
        return this->sle_->isFieldPresent(sfCouponInterval);
    }

    /**
     * @brief Get sfFirstCouponTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getFirstCouponTime() const
    {
        if (hasFirstCouponTime())
            return this->sle_->at(sfFirstCouponTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfFirstCouponTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasFirstCouponTime() const
    {
        return this->sle_->isFieldPresent(sfFirstCouponTime);
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
     * @brief Get sfCallNoticePeriod (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getCallNoticePeriod() const
    {
        if (hasCallNoticePeriod())
            return this->sle_->at(sfCallNoticePeriod);
        return std::nullopt;
    }

    /**
     * @brief Check if sfCallNoticePeriod is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasCallNoticePeriod() const
    {
        return this->sle_->isFieldPresent(sfCallNoticePeriod);
    }

    /**
     * @brief Get sfEarliestCallTime (SoeOptional)
     * @return The field value, or std::nullopt if not present.
     */
    [[nodiscard]]
    protocol_autogen::Optional<SF_UINT32::type::value_type>
    getEarliestCallTime() const
    {
        if (hasEarliestCallTime())
            return this->sle_->at(sfEarliestCallTime);
        return std::nullopt;
    }

    /**
     * @brief Check if sfEarliestCallTime is present.
     * @return True if the field is present, false otherwise.
     */
    [[nodiscard]]
    bool
    hasEarliestCallTime() const
    {
        return this->sle_->isFieldPresent(sfEarliestCallTime);
    }
};

/**
 * @brief Builder for CouponSchedule ledger entries.
 *
 * Provides a fluent interface for constructing ledger entries with method chaining.
 * Uses STObject internally for flexible ledger entry construction.
 * Inherits common field setters from LedgerEntryBuilderBase.
 */
class CouponScheduleBuilder : public LedgerEntryBuilderBase<CouponScheduleBuilder>
{
public:
    /**
     * @brief Construct a new CouponScheduleBuilder with required fields.
     * @param previousTxnID The sfPreviousTxnID field value.
     * @param previousTxnLgrSeq The sfPreviousTxnLgrSeq field value.
     * @param ownerNode The sfOwnerNode field value.
     * @param owner The sfOwner field value.
     * @param account The sfAccount field value.
     * @param mPTokenIssuanceID The sfMPTokenIssuanceID field value.
     * @param couponAsset The sfCouponAsset field value.
     */
    CouponScheduleBuilder(std::decay_t<typename SF_UINT256::type::value_type> const& previousTxnID,std::decay_t<typename SF_UINT32::type::value_type> const& previousTxnLgrSeq,std::decay_t<typename SF_UINT64::type::value_type> const& ownerNode,std::decay_t<typename SF_ACCOUNT::type::value_type> const& owner,std::decay_t<typename SF_ACCOUNT::type::value_type> const& account,std::decay_t<typename SF_UINT192::type::value_type> const& mPTokenIssuanceID,std::decay_t<typename SF_ISSUE::type::value_type> const& couponAsset)
        : LedgerEntryBuilderBase<CouponScheduleBuilder>(ltCOUPON_SCHEDULE)
    {
        setPreviousTxnID(previousTxnID);
        setPreviousTxnLgrSeq(previousTxnLgrSeq);
        setOwnerNode(ownerNode);
        setOwner(owner);
        setAccount(account);
        setMPTokenIssuanceID(mPTokenIssuanceID);
        setCouponAsset(couponAsset);
    }

    /**
     * @brief Construct a CouponScheduleBuilder from an existing SLE object.
     * @param sle The existing ledger entry to copy from.
     * @throws std::runtime_error if the ledger entry type doesn't match.
     */
    CouponScheduleBuilder(SLE::const_pointer sle)
    {
        if (sle->at(sfLedgerEntryType) != ltCOUPON_SCHEDULE)
        {
            throw std::runtime_error("Invalid ledger entry type for CouponSchedule");
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
    CouponScheduleBuilder&
    setPreviousTxnID(std::decay_t<typename SF_UINT256::type::value_type> const& value)
    {
        object_[sfPreviousTxnID] = value;
        return *this;
    }

    /**
     * @brief Set sfPreviousTxnLgrSeq (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setPreviousTxnLgrSeq(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfPreviousTxnLgrSeq] = value;
        return *this;
    }

    /**
     * @brief Set sfOwnerNode (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setOwnerNode(std::decay_t<typename SF_UINT64::type::value_type> const& value)
    {
        object_[sfOwnerNode] = value;
        return *this;
    }

    /**
     * @brief Set sfOwner (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setOwner(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfOwner] = value;
        return *this;
    }

    /**
     * @brief Set sfAccount (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setAccount(std::decay_t<typename SF_ACCOUNT::type::value_type> const& value)
    {
        object_[sfAccount] = value;
        return *this;
    }

    /**
     * @brief Set sfMPTokenIssuanceID (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setMPTokenIssuanceID(std::decay_t<typename SF_UINT192::type::value_type> const& value)
    {
        object_[sfMPTokenIssuanceID] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponAsset (SoeRequired)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setCouponAsset(std::decay_t<typename SF_ISSUE::type::value_type> const& value)
    {
        object_[sfCouponAsset] = STIssue(sfCouponAsset, value);
        return *this;
    }

    /**
     * @brief Set sfAccruedPerUnit (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setAccruedPerUnit(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfAccruedPerUnit] = value;
        return *this;
    }

    /**
     * @brief Set sfPoolAmount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setPoolAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfPoolAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfClaimantCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setClaimantCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfClaimantCount] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponCount (SoeDefault)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setCouponCount(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCouponCount] = value;
        return *this;
    }

    /**
     * @brief Set sfLastCouponTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setLastCouponTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfLastCouponTime] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponAmount (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setCouponAmount(std::decay_t<typename SF_AMOUNT::type::value_type> const& value)
    {
        object_[sfCouponAmount] = value;
        return *this;
    }

    /**
     * @brief Set sfCouponInterval (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setCouponInterval(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCouponInterval] = value;
        return *this;
    }

    /**
     * @brief Set sfFirstCouponTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setFirstCouponTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfFirstCouponTime] = value;
        return *this;
    }

    /**
     * @brief Set sfExpiration (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setExpiration(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfExpiration] = value;
        return *this;
    }

    /**
     * @brief Set sfCallNoticePeriod (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setCallNoticePeriod(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfCallNoticePeriod] = value;
        return *this;
    }

    /**
     * @brief Set sfEarliestCallTime (SoeOptional)
     * @return Reference to this builder for method chaining.
     */
    CouponScheduleBuilder&
    setEarliestCallTime(std::decay_t<typename SF_UINT32::type::value_type> const& value)
    {
        object_[sfEarliestCallTime] = value;
        return *this;
    }

    /**
     * @brief Build and return the completed CouponSchedule wrapper.
     * @param index The ledger entry index.
     * @return The constructed ledger entry wrapper.
     */
    CouponSchedule
    build(uint256 const& index)
    {
        return CouponSchedule{std::make_shared<SLE>(std::move(object_), index)};
    }
};

}  // namespace xrpl::ledger_entries
