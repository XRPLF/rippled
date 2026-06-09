#include <xrpl/tx/ApplyContext.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/InvariantCheck.h>

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace {

template <class... Checkers>
struct InvariantCheckTypes
{
};

using RoutedInvariantCheckTypes = InvariantCheckTypes<
    TransactionFeeCheck,
    AccountRootsNotDeleted,
    AccountRootsDeletedClean,
    LedgerEntryTypesMatch,
    XRPBalanceChecks,
    XRPNotCreated,
    NoXRPTrustLines,
    NoDeepFreezeTrustLinesWithoutFreeze,
    TransfersNotFrozen,
    NoBadOffers,
    NoZeroEscrow,
    ValidNewAccountRoot,
    ValidNFTokenPage,
    NFTokenCountTracking,
    ValidClawback,
    ValidMPTIssuance,
    ValidPermissionedDomain,
    ValidPermissionedDEX,
    ValidBookDirectory,
    ValidAMM,
    NoModifiedUnmodifiableFields,
    ValidPseudoAccounts,
    ValidLoanBroker,
    ValidLoan,
    ValidVault,
    ValidMPTPayment,
    ValidAmounts,
    ValidMPTTransfer>;

template <class Checker, class... Checkers>
consteval std::size_t
countInvariantCheck(InvariantCheckTypes<Checkers...>)
{
    return (std::size_t{0} + ... + (std::is_same_v<Checker, Checkers> ? 1 : 0));
}

template <std::size_t... Is>
consteval bool
allInvariantChecksAreRouted(std::index_sequence<Is...>)
{
    return (
        (countInvariantCheck<std::tuple_element_t<Is, InvariantChecks>>(
             RoutedInvariantCheckTypes{}) == 1) &&
        ...);
}

static_assert(
    allInvariantChecksAreRouted(std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{}),
    "Every invariant check must be routed exactly once.");

template <class... Checkers>
void
visitInvariantChecks(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    (..., std::get<Checkers>(checkers).visitEntry(isDelete, before, after));
}

[[nodiscard]] std::optional<LedgerEntryType>
entryTypeForSpecificInvariants(SLE::const_ref before, SLE::const_ref after)
{
    if (before && after && before->getType() != after->getType())
    {
        // LedgerEntryTypesMatch runs as a broad invariant and reports this.
        return std::nullopt;
    }

    if (after)
        return after->getType();
    if (before)
        return before->getType();
    return std::nullopt;
}

void
visitTypeSpecificInvariantChecks(
    InvariantChecks& checkers,
    LedgerEntryType type,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    switch (type)
    {
        case ltACCOUNT_ROOT:
            visitInvariantChecks<
                AccountRootsNotDeleted,
                AccountRootsDeletedClean,
                XRPBalanceChecks,
                TransfersNotFrozen,
                ValidNewAccountRoot,
                NFTokenCountTracking,
                ValidAMM,
                ValidPseudoAccounts,
                ValidLoanBroker,
                ValidVault>(checkers, isDelete, before, after);
            break;
        case ltRIPPLE_STATE:
            visitInvariantChecks<
                NoXRPTrustLines,
                NoDeepFreezeTrustLinesWithoutFreeze,
                TransfersNotFrozen,
                ValidMPTIssuance,
                ValidAMM,
                ValidLoanBroker,
                ValidVault>(checkers, isDelete, before, after);
            break;
        case ltOFFER:
            visitInvariantChecks<NoBadOffers, ValidPermissionedDEX>(
                checkers, isDelete, before, after);
            break;
        case ltESCROW:
            visitInvariantChecks<NoZeroEscrow>(checkers, isDelete, before, after);
            break;
        case ltNFTOKEN_PAGE:
            visitInvariantChecks<ValidNFTokenPage>(checkers, isDelete, before, after);
            break;
        case ltMPTOKEN_ISSUANCE:
            visitInvariantChecks<NoZeroEscrow, ValidMPTIssuance, ValidVault, ValidMPTPayment>(
                checkers, isDelete, before, after);
            break;
        case ltMPTOKEN:
            visitInvariantChecks<
                NoZeroEscrow,
                ValidClawback,
                ValidMPTIssuance,
                ValidAMM,
                ValidLoanBroker,
                ValidVault,
                ValidMPTPayment,
                ValidMPTTransfer>(checkers, isDelete, before, after);
            break;
        case ltPERMISSIONED_DOMAIN:
            visitInvariantChecks<ValidPermissionedDomain>(checkers, isDelete, before, after);
            break;
        case ltDIR_NODE:
            visitInvariantChecks<ValidPermissionedDEX, ValidBookDirectory>(
                checkers, isDelete, before, after);
            break;
        case ltAMM:
            visitInvariantChecks<ValidAMM>(checkers, isDelete, before, after);
            break;
        case ltLOAN_BROKER:
            visitInvariantChecks<ValidLoanBroker>(checkers, isDelete, before, after);
            break;
        case ltLOAN:
            visitInvariantChecks<ValidLoan>(checkers, isDelete, before, after);
            break;
        case ltVAULT:
            visitInvariantChecks<ValidVault>(checkers, isDelete, before, after);
            break;
        default:
            break;
    }
}

void
visitBroadInvariantChecks(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    visitInvariantChecks<
        LedgerEntryTypesMatch,
        XRPNotCreated,
        NoModifiedUnmodifiableFields,
        ValidAmounts>(checkers, isDelete, before, after);
}

}  // namespace

ApplyContext::ApplyContext(
    ServiceRegistry& registry,
    OpenView& base,
    std::optional<uint256 const> const& parentBatchId,
    STTx const& tx,
    TER preclaimResult,
    XRPAmount baseFee,
    ApplyFlags flags,
    beast::Journal journal)
    : registry(registry)
    , tx(tx)
    , preclaimResult(preclaimResult)
    , baseFee(baseFee)
    , journal(journal)
    , base_(base)
    , flags_(flags)
    , parentBatchId_(parentBatchId)
{
    XRPL_ASSERT(
        parentBatchId.has_value() == ((flags_ & TapBatch) == TapBatch),
        "Parent Batch ID should be set if batch apply flag is set");
    view_.emplace(&base_, flags_);
}

void
ApplyContext::discard()
{
    view_.emplace(&base_, flags_);
}

std::optional<TxMeta>
ApplyContext::apply(TER ter)
{
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) view_ emplaced in constructor
    return view_->apply(base_, tx, ter, parentBatchId_, (flags_ & TapDryRun) != 0u, journal);
}

std::size_t
ApplyContext::size()
{
    return view_->size();  // NOLINT(bugprone-unchecked-optional-access)
}

void
ApplyContext::visit(
    std::function<void(uint256 const&, bool, SLE::const_ref, SLE::const_ref)> const& func)
{
    view_->visit(base_, func);  // NOLINT(bugprone-unchecked-optional-access)
}

TER
ApplyContext::failInvariantCheck(TER const result)
{
    // If we already failed invariant checks before and we are now attempting to
    // only charge a fee, and even that fails the invariant checks something is
    // very wrong. We switch to tefINVARIANT_FAILED, which does NOT get included
    // in a ledger.

    return (result == tecINVARIANT_FAILED || result == tefINVARIANT_FAILED)
        ? TER{tefINVARIANT_FAILED}
        : TER{tecINVARIANT_FAILED};
}

template <std::size_t... Is>
TER
ApplyContext::checkInvariantsHelper(
    TER const result,
    XRPAmount const fee,
    std::index_sequence<Is...>)
{
    try
    {
        auto checkers = getInvariantChecks();

        // call each check's per-entry method
        visit([&checkers](
                  uint256 const&, bool isDelete, SLE::const_ref before, SLE::const_ref after) {
            visitBroadInvariantChecks(checkers, isDelete, before, after);

            if (auto const type = entryTypeForSpecificInvariants(before, after))
                visitTypeSpecificInvariantChecks(checkers, *type, isDelete, before, after);
        });

        // Note: do not replace this logic with a `...&&` fold expression.
        // The fold expression will only run until the first check fails (it
        // short-circuits). While the logic is still correct, the log
        // message won't be. Every failed invariant should write to the log,
        // not just the first one.
        std::array<bool, sizeof...(Is)> const finalizers{{std::get<Is>(checkers).finalize(
            tx, result, fee, *view_, journal)...}};  // NOLINT(bugprone-unchecked-optional-access)

        // call each check's finalizer to see that it passes
        if (!std::all_of(finalizers.cbegin(), finalizers.cend(), [](auto const& b) { return b; }))
        {
            JLOG(journal.fatal()) << "Transaction has failed one or more global invariants: "
                                  << to_string(tx.getJson(JsonOptions::Values::None));

            return failInvariantCheck(result);
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(journal.fatal()) << "Transaction caused an exception in a global invariant"
                              << ", ex: " << ex.what()
                              << ", tx: " << to_string(tx.getJson(JsonOptions::Values::None));

        return failInvariantCheck(result);
    }

    return result;
}

TER
ApplyContext::checkInvariants(TER const result, XRPAmount const fee)
{
    XRPL_ASSERT(
        isTesSuccess(result) || isTecClaim(result),
        "xrpl::ApplyContext::checkInvariants : is tesSUCCESS or tecCLAIM");

    return checkInvariantsHelper(
        result, fee, std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});
}

}  // namespace xrpl
