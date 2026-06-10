#include <xrpl/tx/ApplyContext.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/invariants/AMMInvariant.h>
#include <xrpl/tx/invariants/DirectoryInvariant.h>
#include <xrpl/tx/invariants/FreezeInvariant.h>
#include <xrpl/tx/invariants/InvariantCheck.h>
#include <xrpl/tx/invariants/LoanBrokerInvariant.h>
#include <xrpl/tx/invariants/LoanInvariant.h>
#include <xrpl/tx/invariants/MPTInvariant.h>
#include <xrpl/tx/invariants/NFTInvariant.h>
#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>
#include <xrpl/tx/invariants/PermissionedDomainInvariant.h>
#include <xrpl/tx/invariants/VaultInvariant.h>

#include <algorithm>
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

template <class Checker>
using RelevantLedgerEntryTypes = std::remove_cvref_t<decltype(Checker::kRelevantLedgerEntryTypes)>;

template <class Checker>
concept HasRelevantLedgerEntryTypes = requires { Checker::kRelevantLedgerEntryTypes; };

template <class Checker>
consteval bool
hasInvariantVisitRoute()
{
    if constexpr (HasRelevantLedgerEntryTypes<Checker>)
    {
        using Types = RelevantLedgerEntryTypes<Checker>;
        return Types::visitsAll || Types::visitsNone || !Types::empty;
    }
    return false;
}

template <std::size_t... Is>
consteval bool
allInvariantChecksHaveVisitRoutes(std::index_sequence<Is...>)
{
    return (hasInvariantVisitRoute<std::tuple_element_t<Is, InvariantChecks>>() && ...);
}

static_assert(
    allInvariantChecksHaveVisitRoutes(
        std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{}),
    "Every invariant check must declare ledger-entry visit routing.");

template <class Checker>
void
visitAllEntryInvariantCheck(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    if constexpr (RelevantLedgerEntryTypes<Checker>::visitsAll)
        std::get<Checker>(checkers).visitEntry(isDelete, before, after);
}

template <std::size_t... Is>
void
visitAllEntryInvariantChecks(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after,
    std::index_sequence<Is...>)
{
    (...,
     visitAllEntryInvariantCheck<std::tuple_element_t<Is, InvariantChecks>>(
         checkers, isDelete, before, after));
}

[[nodiscard]] std::optional<LedgerEntryType>
entryTypeForMappedInvariants(SLE::const_ref before, SLE::const_ref after)
{
    if (before && after && before->getType() != after->getType())
    {
        // LedgerEntryTypesMatch runs as an all-entry invariant and reports this.
        return std::nullopt;
    }

    if (after)
        return after->getType();
    if (before)
        return before->getType();
    return std::nullopt;
}

template <LedgerEntryType Type, class Checker>
void
visitLedgerTypeInvariantCheck(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    using Types = RelevantLedgerEntryTypes<Checker>;
    if constexpr (!Types::visitsAll && !Types::visitsNone && Types::template contains<Type>())
    {
        std::get<Checker>(checkers).visitEntry(isDelete, before, after);
    }
}

template <LedgerEntryType Type, std::size_t... Is>
void
visitLedgerTypeInvariantChecks(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after,
    std::index_sequence<Is...>)
{
    (...,
     visitLedgerTypeInvariantCheck<Type, std::tuple_element_t<Is, InvariantChecks>>(
         checkers, isDelete, before, after));
}

using LedgerTypeInvariantVisitor = void (*)(InvariantChecks&, bool, SLE::const_ref, SLE::const_ref);

template <LedgerEntryType Type>
void
visitMappedLedgerTypeInvariantChecks(
    InvariantChecks& checkers,
    bool isDelete,
    SLE::const_ref before,
    SLE::const_ref after)
{
    visitLedgerTypeInvariantChecks<Type>(
        checkers,
        isDelete,
        before,
        after,
        std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});
}

#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define LEDGER_ENTRY(tag, ...)                              \
    std::pair<LedgerEntryType, LedgerTypeInvariantVisitor>{ \
        tag, &visitMappedLedgerTypeInvariantChecks<tag>},

static constexpr auto kLedgerTypeInvariantVisitors =
    std::to_array<std::pair<LedgerEntryType, LedgerTypeInvariantVisitor>>({
#include <xrpl/protocol/detail/ledger_entries.macro>
    });

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")

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
            visitAllEntryInvariantChecks(
                checkers,
                isDelete,
                before,
                after,
                std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});

            if (auto const type = entryTypeForMappedInvariants(before, after))
            {
                auto const iter = std::find_if(
                    kLedgerTypeInvariantVisitors.cbegin(),
                    kLedgerTypeInvariantVisitors.cend(),
                    [type](auto const& visitor) { return visitor.first == *type; });
                if (iter != kLedgerTypeInvariantVisitors.cend())
                    iter->second(checkers, isDelete, before, after);
            }
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
