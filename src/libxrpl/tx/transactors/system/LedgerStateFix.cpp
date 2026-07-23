#include <xrpl/tx/transactors/system/LedgerStateFix.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <array>
#include <utility>

namespace xrpl {

namespace {

using FixType = LedgerStateFix::FixType;

std::array<std::pair<FixType, SField const*>, 2> const kLedgerFixFields = {{
    {FixType::NfTokenPageLink, &sfOwner},
    {FixType::BookExchangeRate, &sfBookDirectory},
}};

[[nodiscard]] SField const*
fixField(FixType const fixType)
{
    auto const iter = std::ranges::find_if(
        kLedgerFixFields, [fixType](auto const& entry) { return entry.first == fixType; });

    if (iter == kLedgerFixFields.end())
        return nullptr;  // LCOV_EXCL_LINE

    return iter->second;
}

[[nodiscard]] bool
hasUnexpectedFixField(STTx const& tx, SField const& expected)
{
    return std::ranges::any_of(kLedgerFixFields, [&tx, &expected](auto const& entry) {
        auto const field = entry.second;
        return field != &expected && tx.isFieldPresent(*field);
    });
}

}  // namespace

NotTEC
LedgerStateFix::preflight(PreflightContext const& ctx)
{
    auto const fixType = static_cast<FixType>(ctx.tx[sfLedgerFixType]);

    switch (fixType)
    {
        case FixType::NfTokenPageLink:
            break;

        case FixType::BookExchangeRate:
            if (!ctx.rules.enabled(fixCleanup3_2_0))
                return temDISABLED;
            break;

        default:
            return tefINVALID_LEDGER_FIX_TYPE;
    }

    auto const expectedField = fixField(fixType);
    if (expectedField == nullptr)
        return tefINVALID_LEDGER_FIX_TYPE;  // LCOV_EXCL_LINE

    // Each fix type allows exactly one fix-specific field.
    if (!ctx.tx.isFieldPresent(*expectedField) || hasUnexpectedFixField(ctx.tx, *expectedField))
        return temINVALID;

    return tesSUCCESS;
}

XRPAmount
LedgerStateFix::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for LedgerStateFix is one owner reserve, just like
    // the fee for AccountDelete.
    return calculateOwnerReserveFee(view, tx);
}

TER
LedgerStateFix::preclaim(PreclaimContext const& ctx)
{
    if (static_cast<FixType>(ctx.tx[sfLedgerFixType]) == FixType::NfTokenPageLink)
    {
        AccountID const owner{ctx.tx[sfOwner]};
        if (!ctx.view.read(keylet::account(owner)))
            return tecOBJECT_NOT_FOUND;

        return tesSUCCESS;
    }

    if (static_cast<FixType>(ctx.tx[sfLedgerFixType]) == FixType::BookExchangeRate)
    {
        auto const dirKey = ctx.tx.getFieldH256(sfBookDirectory);
        auto const sle = ctx.view.read(Keylet(ltDIR_NODE, dirKey));
        if (!sle)
            return tecOBJECT_NOT_FOUND;

        // Must be the first page of a book directory (has sfExchangeRate).
        if (!sle->isFieldPresent(sfExchangeRate))
            return tecNO_PERMISSION;

        // ExchangeRate is already correct, nothing to fix.
        if (getQuality(sle->key()) == sle->getFieldU64(sfExchangeRate))
            return tecNO_PERMISSION;

        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to preclaim.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

TER
LedgerStateFix::doApply()
{
    if (static_cast<FixType>(ctx_.tx[sfLedgerFixType]) == FixType::NfTokenPageLink)
    {
        if (!nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner]))
            return tecFAILED_PROCESSING;

        return tesSUCCESS;
    }

    if (static_cast<FixType>(ctx_.tx[sfLedgerFixType]) == FixType::BookExchangeRate)
    {
        auto const dirKey = ctx_.tx.getFieldH256(sfBookDirectory);
        auto sle = view().peek(Keylet(ltDIR_NODE, dirKey));
        if (!sle)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        sle->setFieldU64(sfExchangeRate, getQuality(sle->key()));
        view().update(sle);
        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to doApply.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

void
LedgerStateFix::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LedgerStateFix::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
