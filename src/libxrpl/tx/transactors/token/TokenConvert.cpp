#include <xrpl/tx/transactors/token/TokenConvert.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <expected>

namespace xrpl {

namespace {

struct ConvertInfo
{
    bool iouToMpt = false;
    Issue issue;
    MPTIssue mptIssue;
    std::int64_t units = 0;  // MPT base units minted or burned
    STAmount iouAmount;      // exact IOU amount debited or credited
};

std::expected<ConvertInfo, TER>
checkConvert(ReadView const& view, STTx const& tx, beast::Journal j)
{
    AccountID const account = tx[sfAccount];
    STAmount const amount{tx[sfAmount]};

    SLE::const_pointer sleIssuance;
    ConvertInfo info;
    info.iouToMpt = amount.holds<Issue>();

    if (info.iouToMpt)
    {
        info.issue = amount.get<Issue>();
        sleIssuance = view.read(keylet::tokenIssuance(info.issue.account, info.issue.currency));
        if (!sleIssuance)
            return std::unexpected(tecOBJECT_NOT_FOUND);

        auto const mptId = sleIssuance->at(~sfMPTokenIssuanceID);
        if (!mptId)
            return std::unexpected(tecNO_PERMISSION);
        info.mptIssue = MPTIssue{*mptId};
    }
    else
    {
        info.mptIssue = amount.get<MPTIssue>();
        auto const sleMpt = view.read(keylet::mptokenIssuance(info.mptIssue.getMptID()));
        if (!sleMpt)
            return std::unexpected(tecOBJECT_NOT_FOUND);

        auto const issuanceKey = sleMpt->at(~sfTokenIssuanceID);
        if (!issuanceKey)
            return std::unexpected(tecNO_PERMISSION);

        sleIssuance = view.read(keylet::tokenIssuance(*issuanceKey));
        if (!sleIssuance)
            return std::unexpected(tecINTERNAL);  // LCOV_EXCL_LINE

        info.issue = Issue{sleIssuance->at(sfCurrency), sleIssuance->at(sfIssuer)};
    }

    if (sleIssuance->isFlag(lsfTokenLocked))
        return std::unexpected(tecLOCKED);

    std::uint8_t const scale = sleIssuance->at(sfTokenScale);

    // Both representations are touched in both directions, so neither side
    // may be frozen or locked.
    if (isFrozen(view, account, info.issue) || isDeepFrozen(view, account, info.issue))
        return std::unexpected(tecFROZEN);

    if (isFrozen(view, account, info.mptIssue))
        return std::unexpected(tecLOCKED);

    // The holder needs an existing, authorized MPToken; conversion never
    // creates one (that would silently take a reserve).
    if (!view.exists(keylet::mptoken(info.mptIssue.getMptID(), account)))
        return std::unexpected(tecNO_ENTRY);
    if (auto const ter = requireAuth(view, info.mptIssue, account, AuthType::StrongAuth);
        !isTesSuccess(ter))
        return std::unexpected(ter);

    if (info.iouToMpt)
    {
        // Over-range (an uncapped binding with a high scale) or sub-unit
        // amounts cannot be represented in base units.
        auto const units = tokenBaseUnits(amount, scale);
        if (!units || *units == 0)
            return std::unexpected(tecPRECISION_LOSS);
        info.units = *units;
        info.iouAmount = STAmount{info.issue, Number(info.units, -scale)};

        auto const spendable =
            accountHolds(view, account, info.issue, FreezeHandling::ZeroIfFrozen, j);
        if (spendable < info.iouAmount)
            return std::unexpected(tecINSUFFICIENT_FUNDS);
    }
    else
    {
        info.units = amount.mpt().value();
        info.iouAmount = STAmount{info.issue, Number(info.units, -scale)};

        auto const spendable = accountHolds(
            view,
            account,
            info.mptIssue,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::ZeroIfUnauthorized,
            j);
        if (spendable < amount)
            return std::unexpected(tecINSUFFICIENT_FUNDS);

        // The trust line must already exist with limit headroom; conversion
        // never creates one and never pushes a holder past their limit.
        auto const lineKey = keylet::trustLine(account, info.issue.account, info.issue.currency);
        auto const sleLine = view.read(lineKey);
        if (!sleLine)
            return std::unexpected(tecNO_LINE);

        bool const accountLow = account < info.issue.account;
        STAmount const limit = sleLine->getFieldAmount(accountLow ? sfLowLimit : sfHighLimit);
        STAmount balance = sleLine->getFieldAmount(sfBalance);
        if (!accountLow)
            balance.negate();
        balance += info.iouAmount;
        if (limit < balance)
            return std::unexpected(tecLIMIT_EXCEEDED);
    }

    return info;
}

}  // namespace

bool
TokenConvert::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureMPTokensV1);
}

NotTEC
TokenConvert::preflight(PreflightContext const& ctx)
{
    STAmount const amount{ctx.tx[sfAmount]};

    if (amount.native())
        return temBAD_AMOUNT;

    if (amount <= beast::kZero)
        return temBAD_AMOUNT;

    // The issuer cannot hold its own token in either representation.
    if (amount.getIssuer() == ctx.tx[sfAccount])
        return temMALFORMED;

    return tesSUCCESS;
}

TER
TokenConvert::preclaim(PreclaimContext const& ctx)
{
    auto const info = checkConvert(ctx.view, ctx.tx, ctx.j);
    if (!info)
        return info.error();
    return tesSUCCESS;
}

TER
TokenConvert::doApply()
{
    auto const infoExp = checkConvert(view(), ctx_.tx, j_);
    if (!infoExp)
        return infoExp.error();
    auto const& info = *infoExp;

    AccountID const& issuer = info.issue.account;
    STAmount const mptAmount{info.mptIssue, info.units};

    if (info.iouToMpt)
    {
        // Redeem the IOU (exactly units * 10^-scale; sub-unit dust stays on
        // the trust line), then mint the bound MPT. Sum-neutral.
        if (auto const ter = directSendNoFee(view(), accountID_, issuer, info.iouAmount, true, j_);
            !isTesSuccess(ter))
            return ter;

        // Defensive cap check; a conversion is sum-neutral so this can only
        // trip if the two sides have drifted.
        auto const sleMpt = view().read(keylet::mptokenIssuance(info.mptIssue.getMptID()));
        if (!sleMpt)
            return tecINTERNAL;  // LCOV_EXCL_LINE
        if (auto const maximum = sleMpt->at(~sfMaximumAmount))
        {
            auto const minted = static_cast<std::uint64_t>(info.units);
            if (minted > *maximum || sleMpt->at(sfOutstandingAmount) > *maximum - minted)
                return tecSUPPLY_EXCEEDED;  // LCOV_EXCL_LINE
        }

        return directSendNoFee(view(), issuer, accountID_, mptAmount, false, j_);
    }

    // Burn the MPT, then credit the IOU. This direction is exact.
    if (auto const ter = directSendNoFee(view(), accountID_, issuer, mptAmount, false, j_);
        !isTesSuccess(ter))
        return ter;

    return directSendNoFee(view(), issuer, accountID_, info.iouAmount, true, j_);
}

}  // namespace xrpl
