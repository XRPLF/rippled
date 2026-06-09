#include <xrpl/tx/invariants/CheckInvariants.h>

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
#include <string>
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
    std::optional<std::reference_wrapper<InvariantCheck>> txCheck,
    std::index_sequence<Is...>)
{
    auto checkers = getInvariantChecks();

    // Phase 1 — state collection.
    // One walk feeds both layers. Per-layer try-catch isolates faults: a throw
    // in txCheck stops only txCheck from visiting further entries; the protocol
    // fold keeps going (and vice-versa). A layer that threw skips finalize.
    bool txCollectionOk = true;
    bool protoCollectionOk = true;
    std::string txCollectionEx;
    std::string protoCollectionEx;

    ctx.visit([&](uint256 const&, bool isDelete, SLE::const_ref before, SLE::const_ref after) {
        if (txCheck && txCollectionOk)
        {
            try
            {
                txCheck->get().visitEntry(isDelete, before, after);
            }
            catch (std::exception const& ex)
            {
                txCollectionOk = false;
                txCollectionEx = ex.what();
            }
        }

        if (protoCollectionOk)
        {
            try
            {
                (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
            }
            catch (std::exception const& ex)
            {
                protoCollectionOk = false;
                protoCollectionEx = ex.what();
            }
        }
    });

    // Phase 2 — evaluate invariant conditions.
    auto const txResult = [&]() -> TER {
        if (!txCheck)
            return result;

        if (!txCollectionOk)
        {
            JLOG(ctx.journal.fatal())
                << "Transaction caused an exception while collecting transaction invariant state"
                << ", ex: " << txCollectionEx
                << ", tx: " << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            return tecINVARIANT_FAILED;
        }

        try
        {
            if (!txCheck->get().finalize(ctx.tx, result, fee, ctx.view(), ctx.journal))
            {
                JLOG(ctx.journal.fatal())
                    << "Transaction has failed one or more transaction invariants: "
                    << to_string(ctx.tx.getJson(JsonOptions::Values::None));
                return tecINVARIANT_FAILED;
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(ctx.journal.fatal())
                << "Transaction caused an exception in a transaction invariant"
                << ", ex: " << ex.what()
                << ", tx: " << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            return tecINVARIANT_FAILED;
        }

        return result;
    }();

    auto const protoResult = [&]() -> TER {
        if (!protoCollectionOk)
        {
            JLOG(ctx.journal.fatal())
                << "Transaction caused an exception while collecting global invariant state"
                << ", ex: " << protoCollectionEx
                << ", tx: " << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            return failInvariantCheck(result);
        }

        bool protoOk = true;
        try
        {
            // Note: do not replace this logic with a `...&&` fold expression.
            // The fold expression will only run until the first check fails (it
            // short-circuits). While the logic is still correct, the log
            // message won't be. Every failed invariant should write to the log,
            // not just the first one.
            std::array<bool, sizeof...(Is)> const finalizers{{std::get<Is>(checkers).finalize(
                ctx.tx,
                result,
                fee,
                ctx.view(),
                ctx.journal)...}};  // NOLINT(bugprone-unchecked-optional-access)

            protoOk = std::all_of(
                finalizers.cbegin(), finalizers.cend(), [](auto const& b) { return b; });
            if (!protoOk)
            {
                JLOG(ctx.journal.fatal())
                    << "Transaction has failed one or more global invariants: "
                    << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            }
        }
        catch (std::exception const& ex)
        {
            JLOG(ctx.journal.fatal())
                << "Transaction caused an exception in a global invariant"
                << ", ex: " << ex.what()
                << ", tx: " << to_string(ctx.tx.getJson(JsonOptions::Values::None));
            protoOk = false;
        }

        return protoOk ? result : failInvariantCheck(result);
    }();

    // Fail if either check failed. tef (fatal) takes priority over tec.
    if (protoResult == tefINVARIANT_FAILED)
        return tefINVARIANT_FAILED;
    if (txResult == tecINVARIANT_FAILED || protoResult == tecINVARIANT_FAILED)
        return tecINVARIANT_FAILED;

    return result;
}

}  // namespace

TER
checkInvariants(
    ApplyContext& ctx,
    TER const result,
    XRPAmount const fee,
    std::optional<std::reference_wrapper<InvariantCheck>> txCheck)
{
    XRPL_ASSERT(
        isTesSuccess(result) || isTecClaim(result),
        "xrpl::checkInvariants : is tesSUCCESS or tecCLAIM");

    return checkInvariantsHelper(
        ctx, result, fee, txCheck, std::make_index_sequence<std::tuple_size_v<InvariantChecks>>{});
}

}  // namespace xrpl
