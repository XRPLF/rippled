#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

Number
tokenScaledCeil(Number const& value, std::uint8_t scale)
{
    Number const shifted(value.mantissa(), value.exponent() + scale);
    Number const trunc = shifted.truncate();
    if (trunc < shifted)
        return trunc + Number(1);
    return trunc;
}

std::optional<std::int64_t>
tokenBaseUnits(Number const& value, std::uint8_t scale)
{
    if (value < Number{})
        return std::nullopt;
    Number const shifted(value.mantissa(), value.exponent() + scale);
    // 10^18 is exactly representable; anything above it is far beyond any
    // legal MaximumAmount.
    if (shifted > Number(1'000'000'000'000'000'000LL))
        return std::nullopt;
    NumberRoundModeGuard const guard(Number::RoundingMode::TowardsZero);
    return static_cast<std::int64_t>(shifted);
}

static bool
supplyWouldExceed(ReadView const& view, SLE::const_ref sleIssuance, Number const& issued)
{
    auto const maximum = sleIssuance->at(~sfMaximumAmount);
    if (!maximum)
        return false;

    std::uint64_t mptOutstanding = 0;
    if (auto const mptId = sleIssuance->at(~sfMPTokenIssuanceID))
    {
        if (auto const sleMpt = view.read(keylet::mptokenIssuance(*mptId)))
            mptOutstanding = sleMpt->at(sfOutstandingAmount);
    }
    if (mptOutstanding > *maximum)
        return true;

    Number const iouUnits = tokenScaledCeil(issued, sleIssuance->at(sfTokenScale));
    return iouUnits > Number(static_cast<std::int64_t>(*maximum - mptOutstanding));
}

bool
tokenSupplyExceeded(ReadView const& view, SLE::const_ref sleIssuance)
{
    return supplyWouldExceed(view, sleIssuance, sleIssuance->at(sfIssuedAmount));
}

TER
adjustTokenIssuance(
    ApplyView& view,
    AccountID const& sender,
    AccountID const& receiver,
    STAmount const& amount,
    EnforceSupplyCap enforceCap,
    beast::Journal j)
{
    if (!view.rules().enabled(featureTokenIssuance))
        return tesSUCCESS;

    if (!amount.holds<Issue>() || amount.native() || amount == beast::kZero)
        return tesSUCCESS;

    if (amount.negative())
        return adjustTokenIssuance(view, receiver, sender, -amount, enforceCap, j);

    // Only the amount's issuer has an obligation in this move. Currency
    // codes are not unique across issuers, so the counterparty's issuance
    // of the same code must never be touched.
    AccountID const& issuer = amount.getIssuer();
    bool const increasing = issuer == sender;
    if (!increasing && issuer != receiver)
        return tesSUCCESS;

    auto const sle = view.peek(keylet::tokenIssuance(issuer, amount.get<Issue>().currency));
    if (!sle)
        return tesSUCCESS;

    Number const delta = amount;
    Number const issued = sle->at(sfIssuedAmount);
    Number const next = increasing ? issued + delta : issued - delta;

    if (increasing && enforceCap == EnforceSupplyCap::Yes && supplyWouldExceed(view, sle, next))
    {
        JLOG(j.trace()) << "adjustTokenIssuance: supply cap exceeded for " << issuer;
        return tecSUPPLY_EXCEEDED;
    }

    sle->at(sfIssuedAmount) = next;
    associateAsset(*sle, amount.asset());
    view.update(sle);
    return tesSUCCESS;
}

std::optional<STAmount>
tokenIssuanceHeadroom(ReadView const& view, Issue const& issue)
{
    if (!view.rules().enabled(featureTokenIssuance))
        return std::nullopt;

    auto const sle = view.read(keylet::tokenIssuance(issue.account, issue.currency));
    if (!sle)
        return std::nullopt;

    auto const maximum = sle->at(~sfMaximumAmount);
    if (!maximum)
        return std::nullopt;

    std::uint8_t const scale = sle->at(sfTokenScale);

    std::uint64_t mptOutstanding = 0;
    if (auto const mptId = sle->at(~sfMPTokenIssuanceID))
    {
        if (auto const sleMpt = view.read(keylet::mptokenIssuance(*mptId)))
            mptOutstanding = sleMpt->at(sfOutstandingAmount);
    }
    if (mptOutstanding > *maximum)
        return STAmount{issue};

    Number const capUnits(static_cast<std::int64_t>(*maximum - mptOutstanding));
    Number const iouUnits = tokenScaledCeil(sle->at(sfIssuedAmount), scale);
    if (iouUnits >= capUnits)
        return STAmount{issue};

    Number const freeUnits = capUnits - iouUnits;
    return STAmount{issue, Number(freeUnits.mantissa(), freeUnits.exponent() - scale)};
}

bool
isTokenLocked(ReadView const& view, Issue const& issue)
{
    if (!view.rules().enabled(featureTokenIssuance))
        return false;

    auto const sle = view.read(keylet::tokenIssuance(issue.account, issue.currency));
    return sle && sle->isFlag(lsfTokenLocked);
}

std::optional<std::int64_t>
mptBoundHeadroom(ReadView const& view, SLE::const_ref sleMptIssuance)
{
    if (!view.rules().enabled(featureTokenIssuance))
        return std::nullopt;

    auto const issuanceKey = sleMptIssuance->at(~sfTokenIssuanceID);
    if (!issuanceKey)
        return std::nullopt;

    auto const sleTI = view.read(keylet::tokenIssuance(*issuanceKey));
    if (!sleTI)
        return std::nullopt;  // LCOV_EXCL_LINE

    auto const maximum = sleTI->at(~sfMaximumAmount);
    if (!maximum)
        return std::nullopt;

    std::uint64_t const outstanding = sleMptIssuance->at(sfOutstandingAmount);
    if (outstanding >= *maximum)
        return 0;

    Number const room(static_cast<std::int64_t>(*maximum - outstanding));
    Number const iouUnits = tokenScaledCeil(sleTI->at(sfIssuedAmount), sleTI->at(sfTokenScale));
    if (iouUnits >= room)
        return 0;

    return tokenBaseUnits(room - iouUnits, 0).value_or(0);
}

TER
validateTokenBinding(
    ReadView const& view,
    MPTID const& mptId,
    AccountID const& account,
    std::optional<std::uint64_t> const& maximumAmount,
    std::uint8_t tokenScale)
{
    auto const sleMpt = view.read(keylet::mptokenIssuance(mptId));
    if (!sleMpt)
        return tecOBJECT_NOT_FOUND;

    if (sleMpt->at(sfIssuer) != account)
        return tecNO_PERMISSION;

    // The binding is one-to-one: an MPT issuance already bound to a
    // TokenIssuance cannot be bound again.
    if (sleMpt->isFieldPresent(sfTokenIssuanceID))
        return tecDUPLICATE;

    // One cap, two views of it: equal maxima in the same base units.
    if (sleMpt->at(~sfMaximumAmount) != maximumAmount)
        return tecWRONG_ASSET;

    if (sleMpt->at(sfAssetScale) != tokenScale)
        return tecWRONG_ASSET;

    return tesSUCCESS;
}

}  // namespace xrpl
