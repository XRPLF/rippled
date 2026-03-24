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
 * View-parameterized wrapper for AccountRoot ledger entries.
 *
 * AccountRoot<ReadView>  — read-only access to account data
 * AccountRoot<ApplyView> — read-write access, with insert/update/erase
 *                          and domain-specific write methods
 */
template <typename ViewT>
class AccountRoot : public SLEBase<ViewT>
{
    static constexpr bool is_writable = SLEBase<ViewT>::is_writable;

    AccountID const id_;

public:
    /** Constructor for read-only context */
    AccountRoot(AccountID const& id, ReadView const& view)
        requires(!is_writable)
        : SLEBase<ViewT>(view.read(keylet::account(id)), view), id_(id)
    {
    }

    /** Constructor for writable context */
    AccountRoot(AccountID const& id, ApplyView& view)
        requires is_writable
        : SLEBase<ViewT>(keylet::account(id), view), id_(id)
    {
    }

    /** Converting constructor: writable → read-only. */
    template <WritableView OtherViewT>
    AccountRoot(AccountRoot<OtherViewT> const& other)
        requires(!is_writable)
        : SLEBase<ViewT>(other), id_(other.id())
    {
    }

    /** Create an AccountRoot backed by a brand-new SLE
     *  (not yet inserted into the view).
     */
    [[nodiscard]] static AccountRoot
    makeNew(AccountID const& id, ApplyView& view)
        requires is_writable
    {
        return AccountRoot(id, view, std::make_shared<SLE>(keylet::account(id)));
    }

    AccountID const&
    id() const
    {
        return id_;
    }

    // --- Read-only domain methods (available on both specializations) ---

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

    // --- Write-only domain methods (compile-time gated) ---

    /** Adjust the owner count up or down. */
    void
    adjustOwnerCount(std::int32_t amount, beast::Journal j)
        requires is_writable;

private:
    // Private constructor only used by `makeNew`
    AccountRoot(AccountID const& id, ApplyView& view, std::shared_ptr<SLE> sle)
        requires is_writable
        : SLEBase<ViewT>(std::move(sle), view), id_(id)
    {
        this->insert();
    }
};

// CTAD deduction guide — bare AccountRoot(id, view) always deduces read-only.
// For writable access, use WritableAccountRoot(id, applyView) explicitly.
AccountRoot(AccountID const&, ReadView const&) -> AccountRoot<ReadView>;

// Backward-compatible aliases
using ReadOnlyAccountRoot = AccountRoot<ReadView>;
using WritableAccountRoot = AccountRoot<ApplyView>;

// Explicit instantiation declarations (definitions in .cpp)
extern template class AccountRoot<ReadView>;
extern template class AccountRoot<ApplyView>;

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
    AccountRoot<ReadView> const acct(accountId, view);
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
