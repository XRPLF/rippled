#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <set>
#include <vector>

namespace xrpl {

/**
 * Read-only wrapper for AccountRoot ledger entries.
 *
 * Provides read-only access to account data.
 */
class AccountRoot : public ReadOnlySLE
{
protected:
    AccountID const id_;

public:
    AccountRoot(AccountID const& id, ReadView const& view)
        : ReadOnlySLE(view.read(keylet::account(id)), view), id_(id)
    {
    }

    AccountID const&
    id() const
    {
        return id_;
    }

    /** Check if the issuer has the global freeze flag set.
        @return true if the account has global freeze set
    */
    [[nodiscard]] bool
    isGlobalFrozen() const;

    /** Returns IOU issuer transfer fee as Rate. Rate specifies
     * the fee as fractions of 1 billion. For example, 1% transfer rate
     * is represented as 1,010,000,000.
     */
    [[nodiscard]] Rate
    transferRate() const;

    // Calculate liquid XRP balance for an account.
    // This function may be used to calculate the amount of XRP that
    // the holder is able to freely spend. It subtracts reserve requirements.
    //
    // ownerCountAdj adjusts the owner count in case the caller calculates
    // before ledger entries are added or removed. Positive to add, negative
    // to subtract.
    //
    // @param ownerCountAdj positive to add to count, negative to reduce count.
    [[nodiscard]] XRPAmount
    xrpLiquid(std::int32_t ownerCountAdj, beast::Journal j) const;

    /** Checks the destination and tag.

       - Checks that the SLE is not null.
       - If the SLE requires a destination tag, checks that there is a tag.
    */
    [[nodiscard]] TER
    checkDestinationAndTag(bool hasDestinationTag) const;

    /** Returns true if and only if sleAcct is a pseudo-account or specific
        pseudo-accounts in pseudoFieldFilter.

        Returns false if sleAcct is:
        - NOT a pseudo-account OR
        - NOT a ltACCOUNT_ROOT OR
        - null pointer
    */
    [[nodiscard]] bool
    isPseudoAccount(std::set<SField const*> const& pseudoFieldFilter = {}) const;

    [[nodiscard]] bool
    operator==(AccountRoot const& other) const
    {
        return id_ == other.id_;
    }

    [[nodiscard]] bool
    operator==(AccountID const& other) const
    {
        return id_ == other;
    }
};

/**
 * Writable wrapper for AccountRoot ledger entries.
 *
 * Provides read-write access to account data.
 * Inherits from AccountRoot to reuse read-only methods,
 * and adds write capabilities.
 */
class WritableAccountRoot : public AccountRoot, public WritableSLE
{
public:
    WritableAccountRoot(AccountID const& id, ApplyView& view)
        : AccountRoot(id, view), WritableSLE(keylet::account(id), view)
    {
    }

    /** Create a WritableAccountRoot backed by a brand-new SLE
     *  (not yet inserted into the view).
     */
    [[nodiscard]] static WritableAccountRoot
    makeNew(AccountID const& id, ApplyView& view)
    {
        return WritableAccountRoot(id, view, std::make_shared<SLE>(keylet::account(id)));
    }

private:
    // This is a private constructor only used by `makeNew`
    WritableAccountRoot(AccountID const& id, ApplyView& view, std::shared_ptr<SLE> sle)
        : AccountRoot(id, view), WritableSLE(std::move(sle), view)
    {
        insert();
    }

public:
    // Resolve ambiguity: use writable operator-> for non-const, read-only for const
    using WritableSLE::operator->;
    using AccountRoot::operator->;
    using WritableSLE::operator*;
    using AccountRoot::operator*;

    /** Adjust the owner count up or down. */
    void
    adjustOwnerCount(std::int32_t amount, beast::Journal j);
};

/** Generate a pseudo-account address from a pseudo owner key.
    @param pseudoOwnerKey The key to generate the address from
    @return The generated account ID
*/
AccountID
pseudoAccountAddress(ReadView const& view, uint256 const& pseudoOwnerKey);

/** Returns the list of fields that define an ACCOUNT_ROOT as a pseudo-account
    if set.

    The list is constructed during initialization and is const after that.
    Pseudo-account designator fields MUST be maintained by including the
    SField::sMD_PseudoAccount flag in the SField definition.
*/
[[nodiscard]] std::vector<SField const*> const&
getPseudoAccountFields();

/** Returns true if and only if sleAcct is a pseudo-account or specific
    pseudo-accounts in pseudoFieldFilter.

    Returns false if sleAcct is:
    - NOT a pseudo-account OR
    - NOT a ltACCOUNT_ROOT OR
    - null pointer
*/
[[nodiscard]] bool
isPseudoAccount(
    std::shared_ptr<SLE const> sleAcct,
    std::set<SField const*> const& pseudoFieldFilter = {});

/** Convenience overload that reads the account from the view. */
[[nodiscard]] inline bool
isPseudoAccount(
    ReadView const& view,
    AccountID const& accountId,
    std::set<SField const*> const& pseudoFieldFilter = {})
{
    AccountRoot const acct(accountId, view);
    if (!acct)
        return false;
    return acct.isPseudoAccount(pseudoFieldFilter);
}

/**
 * Create pseudo-account, storing pseudoOwnerKey into ownerField.
 *
 * The list of valid ownerField is maintained in AccountRootHelpers.cpp and
 * the caller to this function must perform necessary amendment check(s)
 * before using a field. The amendment check is **not** performed in
 * createPseudoAccount.
 */
[[nodiscard]] Expected<std::shared_ptr<SLE>, TER>
createPseudoAccount(ApplyView& view, uint256 const& pseudoOwnerKey, SField const& ownerField);

}  // namespace xrpl
