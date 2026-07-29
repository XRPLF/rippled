#include <xrpl/tx/invariants/InvariantRunner.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/invariants/InvariantCheck.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>

namespace xrpl {

namespace {

TER
failInvariantCheck(TER const result)
{
    return (result == tecINVARIANT_FAILED || result == tefINVARIANT_FAILED)
        ? TER{tefINVARIANT_FAILED}
        : TER{tecINVARIANT_FAILED};
}

template <std::size_t... Is>
TER
checkInvariantsHelper(
    ApplyContext& ctx,
    TER const result,
    XRPAmount const fee,
    std::optional<std::reference_wrapper<TxInvariantCheck>> txCheck,
    std::index_sequence<Is...>)
{
    bool allOk = true;

    try
    {
        auto checkers = getInvariantChecks();

        ctx.visit([&](uint256 const&, bool isDelete, SLE::const_ref before, SLE::const_ref after) {
            if (txCheck)
                txCheck->get().visitEntry(isDelete, before, after);
            (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
        });

        if (txCheck)
        {
            if (!txCheck->get().finalize(ctx.tx, result, fee, ctx.view(), ctx.journal))
            {
                JLOG(ctx.journal.fatal())
                    << "Transaction has failed one or more transaction invariants: "
                    << to_string(ctx.tx.getJson(JsonOptions::Values::None));
                allOk = false;
            }
        }

        // Note: do not replace this logic with a `...&&` fold expression.
        // The fold expression will only run until the first check fails (it
        // short-circuits). While the logic is still correct, the log
        // message won't be. Every failed invariant should write to the log,
        // not just the first one.
        std::array<bool, sizeof...(Is)> const finalizers{
            {std::get<Is>(checkers).finalize(ctx.tx, result, fee, ctx.view(), ctx.journal)...}};

        if (!std::all_of(finalizers.cbegin(), finalizers.cend(), [](auto const& b) { return b; }))
        {
            JLOG(ctx.journal.fatal()) << "Transaction has failed one or more global invariants: "
                                      << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            allOk = false;
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(ctx.journal.fatal()) << "Transaction caused an exception during invariant checks"
                                  << ", ex: " << ex.what() << ", tx: "
                                  << to_string(ctx.tx.getJson(JsonOptions::Values::None));
        return failInvariantCheck(result);
    }

    return allOk ? result : failInvariantCheck(result);
}

}  // namespace

TER
checkInvariants(
    ApplyContext& ctx,
    TER const result,
    XRPAmount const fee,
    std::optional<std::reference_wrapper<TxInvariantCheck>> txCheck)
{
    XRPL_ASSERT(
        isTesSuccess(result) || isTecClaim(result),
        "xrpl::checkInvariants : is tesSUCCESS or tecCLAIM");

    return checkInvariantsHelper(
        ctx, result, fee, txCheck, std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});
}

}  // namespace xrpl
