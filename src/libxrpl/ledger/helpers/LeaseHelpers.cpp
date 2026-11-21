#include <xrpl/ledger/helpers/LeaseHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep

#include <algorithm>
#include <cstdint>
#include <optional>

namespace xrpl {

namespace {

// The whole drops needed to clear a claim: a claim with a part of a drop is
// rounded up, so a claim paid in full reaches 0.
XRPAmount
claimDrops(Number const& claim)
{
    if (claim <= beast::kZero)
        return XRPAmount{0};
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    return XRPAmount{claim};
}

// The claim after a payment of `paid` drops; never below 0.
Number
claimAfter(Number const& claim, XRPAmount paid)
{
    Number const left = claim - Number{paid.drops()};
    return left > beast::kZero ? left : Number{0};
}

}  // namespace

bool
isLeaseLoan(SLE const& loan)
{
    return loan.isFlag(lsfLoanLease);
}

bool
isLoanPaidOff(SLE const& loan)
{
    return loan.at(sfPaymentRemaining) == 0 && !loan.isFlag(lsfLoanDefault);
}

SLE::const_pointer
leaseLoanOfSponsor(ReadView const& view, AccountID const& sponsorID)
{
    auto const sponsorSle = view.read(keylet::account(sponsorID));
    if (!sponsorSle || !sponsorSle->isFieldPresent(sfLoanID))
        return nullptr;
    auto loan = view.read(keylet::loan(sponsorSle->at(sfLoanID)));
    if (!loan || !isLeaseLoan(*loan) || loan->at(~sfAccount) != sponsorID)
        return nullptr;
    return loan;
}

std::optional<AccountID>
leaseActingSponsor(ReadView const& view, AccountID const& sponsorID)
{
    auto const loan = leaseLoanOfSponsor(view, sponsorID);
    if (!loan)
        return std::nullopt;
    if (!loan->isFlag(lsfLoanDefault))
        return loan->at(sfBorrower);
    auto const broker = view.read(keylet::loanBroker(loan->at(sfLoanBrokerID)));
    if (!broker)
        return std::nullopt;  // LCOV_EXCL_LINE
    return broker->at(sfOwner);
}

XRPAmount
leaseLocked(SLE const& leaseAccount, SLE const& loan)
{
    std::int64_t const count = leaseAccount.at(sfSponsoringAccountCount);
    XRPAmount const unit{Number{loan.at(~sfLeaseUnit).value_or(Number{0})}};
    return XRPAmount{count * unit.drops()};
}

XRPAmount
leaseFree(SLE const& leaseAccount, SLE const& loan)
{
    XRPAmount const balance = leaseAccount.at(sfBalance).value().xrp();
    XRPAmount const locked = leaseLocked(leaseAccount, loan);
    return balance > locked ? balance - locked : XRPAmount{0};
}

TER
leaseRecover(ApplyView& view, SLE::Ref loan, SLE::Ref broker, SLE::Ref vault, beast::Journal j)
{
    auto const leaseID = loan->at(~sfAccount);
    if (!leaseID)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const leaseSle = view.peek(keylet::account(*leaseID));
    if (!leaseSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    XRPAmount const free = leaseFree(*leaseSle, *loan);
    if (free == beast::kZero)
        return tesSUCCESS;

    Number const claimVault = loan->at(sfLeaseClaimVault);
    Number const claimCover = loan->at(sfLeaseClaimCover);
    XRPAmount const toVault = std::min(free, claimDrops(claimVault));
    XRPAmount const toCover = std::min(free - toVault, claimDrops(claimCover));
    XRPAmount const toBorrower = free - toVault - toCover;

    if (toVault > beast::kZero)
    {
        if (auto const ter = accountSend(
                view,
                *leaseID,
                vault->at(sfAccount),
                STAmount{toVault},
                j,
                {},
                WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
        if (auto const ter = adjustVaultCash(vault, STAmount{toVault}, j); !isTesSuccess(ter))
            return ter;  // LCOV_EXCL_LINE
        view.update(vault);
        loan->at(sfLeaseClaimVault) = claimAfter(claimVault, toVault);
    }

    if (toCover > beast::kZero)
    {
        if (auto const ter = accountSend(
                view,
                *leaseID,
                broker->at(sfAccount),
                STAmount{toCover},
                j,
                {},
                WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
        broker->at(sfCoverAvailable) += Number{toCover.drops()};
        view.update(broker);
        loan->at(sfLeaseClaimCover) = claimAfter(claimCover, toCover);
    }

    if (toBorrower > beast::kZero)
    {
        if (auto const ter = accountSend(
                view,
                *leaseID,
                loan->at(sfBorrower),
                STAmount{toBorrower},
                j,
                {},
                WaiveTransferFee::Yes);
            !isTesSuccess(ter))
            return ter;
    }

    view.update(loan);
    JLOG(j.debug()) << "Lease recovery: vault " << toVault << ", cover " << toCover << ", borrower "
                    << toBorrower;
    return tesSUCCESS;
}

TER
leasePayFreeToBorrower(ApplyView& view, SLE::Ref loan, beast::Journal j)
{
    auto const leaseID = loan->at(~sfAccount);
    if (!leaseID)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const leaseSle = view.read(keylet::account(*leaseID));
    if (!leaseSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    XRPAmount const free = leaseFree(*leaseSle, *loan);
    if (free == beast::kZero)
        return tesSUCCESS;

    return accountSend(
        view, *leaseID, loan->at(sfBorrower), STAmount{free}, j, {}, WaiveTransferFee::Yes);
}

}  // namespace xrpl
