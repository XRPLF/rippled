//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/ledger/ReadView.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace ripple {

// Check equal with tolerance
bool
checkNear(IOUAmount const& expected, IOUAmount const& actual)
{
    double const ratTol = 0.001;
    if (abs(expected.exponent() - actual.exponent()) > 1)
        return false;

    if (actual.exponent() < -20)
        return true;

    auto const a = (expected.exponent() < actual.exponent())
        ? expected.mantissa() / 10
        : expected.mantissa();
    auto const b = (actual.exponent() < expected.exponent())
        ? actual.mantissa() / 10
        : actual.mantissa();
    if (a == b)
        return true;

    double const diff = std::abs(a - b);
    auto const r = diff / std::max(std::abs(a), std::abs(b));
    return r <= ratTol;
};
bool
checkNear(MPTAmount const& expected, MPTAmount const& actual)
{
    if (expected == actual)
        return true;
    Number ratTol = Number(1, -3);
    Number const diff = abs(expected.value() - actual.value());
    auto const r = diff / std::max(expected, actual);
    return r <= ratTol;
}

static bool
isXRPAccount(STPathElement const& pe)
{
    if (pe.getNodeType() != STPathElement::typeAccount)
        return false;
    return isXRP(pe.getAccountID());
};

static std::pair<TER, std::unique_ptr<Step>>
toStep(
    StrandContext const& ctx,
    STPathElement const* e1,
    STPathElement const* e2,
    Asset const& curAsset)
{
    auto& j = ctx.j;

    if (ctx.isFirst && e1->isAccount() &&
        (e1->getNodeType() & STPathElement::typeCurrency) &&
        e1->getPathAsset().isXRP())
    {
        return make_XRPEndpointStep(ctx, e1->getAccountID());
    }

    if (ctx.isLast && isXRPAccount(*e1) && e2->isAccount())
        return make_XRPEndpointStep(ctx, e2->getAccountID());

    // MPTEndpointStep is created in following cases:
    // 1 Direct payment between an issuer and a holder
    //   e1 is issuer and e2 is holder or vise versa
    //   There is only one step in this case: holder->issuer or
    //   issuer->holder
    // 2 Direct payment between the holders
    //   e1 is issuer and e2 is holder or vise versa
    //   There are two steps in this case: holder->issuer->holder1
    // 3 Cross-token payment with Amount or SendMax or both MPT
    //   If destination is an issuer then the last step is BookStep,
    //   otherwise the last step is MPTEndpointStep where e1 is
    //   the issuer and e2 is the holder.
    // In all cases MPTEndpointStep is always first or last step,
    // e1/e2 are always account types, and curAsset is always MPT.

    if (e1->isAccount() && e2->isAccount())
    {
        if (curAsset.holds<MPTIssue>())
            return make_MPTEndpointStep(
                ctx,
                e1->getAccountID(),
                e2->getAccountID(),
                curAsset.get<MPTIssue>().getMptID());
        return make_DirectStepI(
            ctx,
            e1->getAccountID(),
            e2->getAccountID(),
            curAsset.get<Issue>().currency);
    }

    if (e1->isOffer() && e2->isAccount())
    {
        // should already be taken care of
        JLOG(j.error())
            << "Found offer/account payment step. Aborting payment strand.";
        UNREACHABLE("ripple::toStep : offer/account payment payment strand");
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    XRPL_ASSERT(
        (e2->getNodeType() & STPathElement::typeAsset) ||
            (e2->getNodeType() & STPathElement::typeIssuer),
        "ripple::toStep : currency or issuer");
    PathAsset const outAsset = e2->getNodeType() & STPathElement::typeAsset
        ? e2->getPathAsset()
        : curAsset;
    auto const outIssuer = e2->getNodeType() & STPathElement::typeIssuer
        ? e2->getIssuerID()
        : curAsset.getIssuer();

    if (isXRP(curAsset) && outAsset.isXRP())
    {
        JLOG(j.info()) << "Found xrp/xrp offer payment step";
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    XRPL_ASSERT(e2->isOffer(), "ripple::toStep : is offer");

    if (outAsset.isXRP())
    {
        if (curAsset.holds<MPTIssue>())
            return make_BookStepMX(ctx, curAsset.get<MPTIssue>());
        return make_BookStepIX(ctx, curAsset.get<Issue>());
    }

    if (isXRP(curAsset))
    {
        if (outAsset.holds<MPTID>())
            return make_BookStepXM(ctx, outAsset.get<MPTID>());
        return make_BookStepXI(ctx, {outAsset.get<Currency>(), outIssuer});
    }

    if (curAsset.holds<MPTIssue>() && outAsset.holds<Currency>())
        return make_BookStepMI(
            ctx,
            curAsset.get<MPTIssue>(),
            {outAsset.get<Currency>(), outIssuer});
    if (curAsset.holds<Issue>() && outAsset.holds<MPTID>())
        return make_BookStepIM(
            ctx, curAsset.get<Issue>(), outAsset.get<MPTID>());

    if (curAsset.holds<MPTIssue>())
        return make_BookStepMM(
            ctx, curAsset.get<MPTIssue>(), outAsset.get<MPTID>());
    return make_BookStepII(
        ctx, curAsset.get<Issue>(), {outAsset.get<Currency>(), outIssuer});
}

std::pair<TER, Strand>
toStrand(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMaxAsset,
    STPath const& path,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    std::optional<uint256> const& domainID,
    beast::Journal j)
{
    if (isXRP(src) || isXRP(dst) || !isConsistent(deliver) ||
        (sendMaxAsset && !isConsistent(*sendMaxAsset)))
        return {temBAD_PATH, Strand{}};

    if ((sendMaxAsset && sendMaxAsset->getIssuer() == noAccount()) ||
        (src == noAccount()) || (dst == noAccount()) ||
        (deliver.getIssuer() == noAccount()))
        return {temBAD_PATH, Strand{}};

    if ((deliver.holds<MPTIssue>() && deliver.getIssuer() == beast::zero) ||
        (sendMaxAsset && sendMaxAsset->holds<MPTIssue>() &&
         sendMaxAsset->getIssuer() == beast::zero))
        return {temBAD_PATH, Strand{}};

    for (std::size_t i = 0; i < path.size(); ++i)
    {
        auto const& pe = path[i];
        auto const t = pe.getNodeType();

        if ((t & ~STPathElement::typeAll) || !t)
            return {temBAD_PATH, Strand{}};

        bool const hasAccount = t & STPathElement::typeAccount;
        bool const hasIssuer = t & STPathElement::typeIssuer;
        bool const hasCurrency = t & STPathElement::typeCurrency;
        bool const hasMPT = t & STPathElement::typeMPT;
        bool const hasAsset = t & STPathElement::typeAsset;

        if (hasAccount && (hasIssuer || hasCurrency))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && isXRP(pe.getAccountID()))
            return {temBAD_PATH, Strand{}};

        if (hasCurrency && hasIssuer &&
            isXRP(pe.getCurrency()) != isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && (pe.getIssuerID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && (pe.getAccountID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && (hasCurrency || hasAccount))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && hasIssuer &&
            (pe.getIssuerID() != getMPTIssuer(pe.getMPTID())))
            return {temBAD_PATH, Strand{}};

        // No rippling if MPT
        if (i > 0 && path[i - 1].hasMPT() &&
            (hasAccount || (hasIssuer && !hasAsset)))
            return {temBAD_PATH, Strand{}};
    }

    Asset curAsset = [&]() -> Asset {
        auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
        if (isXRP(asset))
            return xrpIssue();
        if (asset.holds<MPTIssue>())
            return asset;
        // First step ripples from the source to the issuer.
        return Issue{asset.get<Issue>().currency, src};
    }();

    // Currency or MPT
    auto hasAsset = [](STPathElement const pe) {
        return pe.getNodeType() & STPathElement::typeAsset;
    };

    std::vector<STPathElement> normPath;
    // reserve enough for the path, the implied source, destination,
    // sendmax and deliver.
    normPath.reserve(4 + path.size());
    {
        // The first step of a path is always implied to be the sender of the
        // transaction, as defined by the transaction's Account field. The Asset
        // is either SendMax or Deliver.
        auto const t = [&]() {
            auto const t =
                STPathElement::typeAccount | STPathElement::typeIssuer;
            if (curAsset.holds<MPTIssue>())
                return t | STPathElement::typeMPT;
            return t | STPathElement::typeCurrency;
        }();
        // If MPT then the issuer is the actual issuer, it is never the source
        // account.
        normPath.emplace_back(t, src, curAsset, curAsset.getIssuer());

        // If transaction includes SendMax with the issuer, which is not
        // the sender of the transaction, that issuer is implied to be
        // the second step of the path. Unless the path starts at an address,
        // which is the issuer of SendMax.
        if (sendMaxAsset && sendMaxAsset->getIssuer() != src &&
            (path.empty() || !path[0].isAccount() ||
             path[0].getAccountID() != sendMaxAsset->getIssuer()))
        {
            normPath.emplace_back(
                sendMaxAsset->getIssuer(), std::nullopt, std::nullopt);
        }

        for (auto const& i : path)
            normPath.push_back(i);

        {
            // Note that for offer crossing (only) we do use an offer book
            // even if all that is changing is the Issue.account. Note
            // that MPTIssue can't change the account.
            STPathElement const& lastAsset =
                *std::find_if(normPath.rbegin(), normPath.rend(), hasAsset);
            if (lastAsset.getPathAsset() != deliver ||
                (offerCrossing &&
                 lastAsset.getIssuerID() != deliver.getIssuer()))
            {
                normPath.emplace_back(
                    std::nullopt, deliver, deliver.getIssuer());
            }
        }

        // If the Amount field of the transaction includes an issuer that is not
        // the same as the Destination of the transaction, that issuer is
        // implied to be the second-to-last step of the path. If normPath.back
        // is an offer, which sells MPT then the added path element account is
        // the MPT's issuer.
        if (!((normPath.back().isAccount() &&
               normPath.back().getAccountID() == deliver.getIssuer()) ||
              (dst == deliver.getIssuer())))
        {
            normPath.emplace_back(
                deliver.getIssuer(), std::nullopt, std::nullopt);
        }

        // Last step of a path is always implied to be the receiver of a
        // transaction, as defined by the transaction's Destination field.
        if (!normPath.back().isAccount() ||
            normPath.back().getAccountID() != dst)
        {
            normPath.emplace_back(dst, std::nullopt, std::nullopt);
        }
    }

    if (normPath.size() < 2)
        return {temBAD_PATH, Strand{}};

    auto const strandSrc = normPath.front().getAccountID();
    auto const strandDst = normPath.back().getAccountID();
    bool const isDefaultPath = path.empty();

    Strand result;
    result.reserve(2 * normPath.size());

    /* A strand may not include the same account node more than once
       in the same currency. In a direct step, an account will show up
       at most twice: once as a src and once as a dst (hence the two element
       array). The strandSrc and strandDst will only show up once each.
    */
    std::array<boost::container::flat_set<Asset>, 2> seenDirectAssets;
    // A strand may not include the same offer book more than once
    boost::container::flat_set<Asset> seenBookOuts;
    seenDirectAssets[0].reserve(normPath.size());
    seenDirectAssets[1].reserve(normPath.size());
    seenBookOuts.reserve(normPath.size());
    auto ctx = [&](bool isLast = false) {
        return StrandContext{
            view,
            result,
            strandSrc,
            strandDst,
            deliver,
            limitQuality,
            isLast,
            ownerPaysTransferFee,
            offerCrossing,
            isDefaultPath,
            seenDirectAssets,
            seenBookOuts,
            ammContext,
            domainID,
            j};
    };

    for (std::size_t i = 0; i < normPath.size() - 1; ++i)
    {
        /* Iterate through the path elements considering them in pairs.
           The first element of the pair is `cur` and the second element is
           `next`. When an offer is one of the pairs, the step created will be
           for `next`. This means when `cur` is an offer and `next` is an
           account then no step is created, as a step has already been created
           for that offer.
        */
        std::optional<STPathElement> impliedPE;
        auto cur = &normPath[i];
        auto const next = &normPath[i + 1];

        // Switch over from MPT to Currency. In this case curAsset account
        // can be different from the issuer. If cur is MPT then curAsset
        // is just set to MPTID.
        if (curAsset.holds<MPTIssue>() && cur->hasCurrency())
            curAsset = Issue{};

        // Can only update the account for Issue since MPTIssue's account
        // is immutable as it is part of MPTID.
        if (curAsset.holds<Issue>())
        {
            if (cur->isAccount())
                curAsset.get<Issue>().account = cur->getAccountID();
            else if (cur->hasIssuer())
                curAsset.get<Issue>().account = cur->getIssuerID();
        }

        if (cur->hasCurrency())
        {
            curAsset = Issue{cur->getCurrency(), curAsset.getIssuer()};
            if (isXRP(curAsset))
                curAsset.get<Issue>().account = xrpAccount();
        }
        else if (cur->hasMPT())
            curAsset = cur->getPathAsset().get<MPTID>();

        auto getImpliedStep =
            [&](AccountID const& src_,
                AccountID const& dst_,
                Asset const& asset_) -> std::pair<TER, std::unique_ptr<Step>> {
            if (asset_.holds<MPTIssue>())
            {
                JLOG(j.error()) << "MPT is invalid with rippling";
                return {temBAD_PATH, nullptr};
            }
            return make_DirectStepI(
                ctx(), src_, dst_, asset_.get<Issue>().currency);
        };

        if (cur->isAccount() && next->isAccount())
        {
            // This block doesn't execute
            // since curAsset's account is set to cur's account above.
            // It should not execute for MPT either because MPT rippling
            // is invalid. Should this block be removed/amendment excluded?
            if (!isXRP(curAsset) &&
                curAsset.getIssuer() != cur->getAccountID() &&
                curAsset.getIssuer() != next->getAccountID())
            {
                JLOG(j.trace()) << "Inserting implied account";
                auto msr = getImpliedStep(
                    cur->getAccountID(), curAsset.getIssuer(), curAsset);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::typeAccount,
                    curAsset.getIssuer(),
                    xrpCurrency(),
                    xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isAccount() && next->isOffer())
        {
            // Same as above, this block doesn't execute.
            if (curAsset.getIssuer() != cur->getAccountID())
            {
                JLOG(j.trace()) << "Inserting implied account before offer";
                auto msr = getImpliedStep(
                    cur->getAccountID(), curAsset.getIssuer(), curAsset);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::typeAccount,
                    curAsset.getIssuer(),
                    xrpCurrency(),
                    xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isOffer() && next->isAccount())
        {
            // If the offer sells MPT, then next's account is always the issuer.
            // See how normPath step is added for second-to-last or last
            // step. Therefore, this block never executes if MPT.
            if (curAsset.getIssuer() != next->getAccountID() &&
                !isXRP(next->getAccountID()))
            {
                if (isXRP(curAsset))
                {
                    if (i != normPath.size() - 2)
                        return {temBAD_PATH, Strand{}};
                    else
                    {
                        // Last step. insert xrp endpoint step
                        auto msr =
                            make_XRPEndpointStep(ctx(), next->getAccountID());
                        if (msr.first != tesSUCCESS)
                            return {msr.first, Strand{}};
                        result.push_back(std::move(msr.second));
                    }
                }
                else
                {
                    JLOG(j.trace()) << "Inserting implied account after offer";
                    auto msr = getImpliedStep(
                        curAsset.getIssuer(), next->getAccountID(), curAsset);
                    if (msr.first != tesSUCCESS)
                        return {msr.first, Strand{}};
                    result.push_back(std::move(msr.second));
                }
            }
            continue;
        }

        if (!next->isOffer() && next->hasAsset() &&
            next->getPathAsset() != curAsset)
        {
            // Should never happen
            UNREACHABLE("ripple::toStrand : offer currency mismatch");
            return {temBAD_PATH, Strand{}};
        }

        auto s = toStep(
            ctx(/*isLast*/ i == normPath.size() - 2), cur, next, curAsset);
        if (s.first == tesSUCCESS)
            result.emplace_back(std::move(s.second));
        else
        {
            JLOG(j.debug()) << "toStep failed: " << s.first;
            return {s.first, Strand{}};
        }
    }

    auto checkStrand = [&]() -> bool {
        auto stepAccts = [](Step const& s) -> std::pair<AccountID, AccountID> {
            if (auto r = s.directStepAccts())
                return *r;
            if (auto const r = s.bookStepBook())
                return std::make_pair(r->in.getIssuer(), r->out.getIssuer());
            Throw<FlowException>(
                tefEXCEPTION, "Step should be either a direct or book step");
            return std::make_pair(xrpAccount(), xrpAccount());
        };

        auto curAcc = src;
        auto curAsset = [&]() -> Asset {
            auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
            if (isXRP(asset))
                return xrpIssue();
            if (asset.holds<MPTIssue>())
                return asset;
            return Issue{asset.get<Issue>().currency, src};
        }();

        for (auto const& s : result)
        {
            auto const accts = stepAccts(*s);
            if (accts.first != curAcc)
                return false;

            if (auto const b = s->bookStepBook())
            {
                if (curAsset != b->in)
                    return false;
                curAsset = b->out;
            }
            else if (curAsset.holds<Issue>())
            {
                curAsset.get<Issue>().account = accts.second;
            }

            curAcc = accts.second;
        }
        if (curAcc != dst)
            return false;
        if (curAsset.holds<Issue>() != deliver.holds<Issue>() ||
            (curAsset.holds<Issue>() &&
             curAsset.get<Issue>().currency != deliver.get<Issue>().currency) ||
            (curAsset.holds<MPTIssue>() &&
             curAsset.get<MPTIssue>() != deliver.get<MPTIssue>()))
            return false;
        if (curAsset.getIssuer() != deliver.getIssuer() &&
            curAsset.getIssuer() != dst)
            return false;
        return true;
    };

    if (!checkStrand())
    {
        JLOG(j.warn()) << "Flow check strand failed";
        UNREACHABLE("ripple::toStrand : invalid strand");
        return {temBAD_PATH, Strand{}};
    }

    return {tesSUCCESS, std::move(result)};
}

std::pair<TER, std::vector<Strand>>
toStrands(
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Asset const& deliver,
    std::optional<Quality> const& limitQuality,
    std::optional<Asset> const& sendMax,
    STPathSet const& paths,
    bool addDefaultPath,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    AMMContext& ammContext,
    std::optional<uint256> const& domainID,
    beast::Journal j)
{
    std::vector<Strand> result;
    result.reserve(1 + paths.size());
    // Insert the strand into result if it is not already part of the vector
    auto insert = [&](Strand s) {
        bool const hasStrand =
            std::find(result.begin(), result.end(), s) != result.end();

        if (!hasStrand)
            result.emplace_back(std::move(s));
    };

    if (addDefaultPath)
    {
        auto sp = toStrand(
            view,
            src,
            dst,
            deliver,
            limitQuality,
            sendMax,
            STPath(),
            ownerPaysTransferFee,
            offerCrossing,
            ammContext,
            domainID,
            j);
        auto const ter = sp.first;
        auto& strand = sp.second;

        if (ter != tesSUCCESS)
        {
            JLOG(j.trace()) << "failed to add default path";
            if (isTemMalformed(ter) || paths.empty())
            {
                return {ter, std::vector<Strand>{}};
            }
        }
        else if (strand.empty())
        {
            JLOG(j.trace()) << "toStrand failed";
            Throw<FlowException>(
                tefEXCEPTION, "toStrand returned tes & empty strand");
        }
        else
        {
            insert(std::move(strand));
        }
    }
    else if (paths.empty())
    {
        JLOG(j.debug()) << "Flow: Invalid transaction: No paths and direct "
                           "ripple not allowed.";
        return {temRIPPLE_EMPTY, std::vector<Strand>{}};
    }

    TER lastFailTer = tesSUCCESS;
    for (auto const& p : paths)
    {
        auto sp = toStrand(
            view,
            src,
            dst,
            deliver,
            limitQuality,
            sendMax,
            p,
            ownerPaysTransferFee,
            offerCrossing,
            ammContext,
            domainID,
            j);
        auto ter = sp.first;
        auto& strand = sp.second;

        if (ter != tesSUCCESS)
        {
            lastFailTer = ter;
            JLOG(j.trace()) << "failed to add path: ter: " << ter
                            << "path: " << p.getJson(JsonOptions::none);
            if (isTemMalformed(ter))
                return {ter, std::vector<Strand>{}};
        }
        else if (strand.empty())
        {
            JLOG(j.trace()) << "toStrand failed";
            Throw<FlowException>(
                tefEXCEPTION, "toStrand returned tes & empty strand");
        }
        else
        {
            insert(std::move(strand));
        }
    }

    if (result.empty())
        return {lastFailTer, std::move(result)};

    return {tesSUCCESS, std::move(result)};
}

StrandContext::StrandContext(
    ReadView const& view_,
    std::vector<std::unique_ptr<Step>> const& strand_,
    // A strand may not include an inner node that
    // replicates the source or destination.
    AccountID const& strandSrc_,
    AccountID const& strandDst_,
    Asset const& strandDeliver_,
    std::optional<Quality> const& limitQuality_,
    bool isLast_,
    bool ownerPaysTransferFee_,
    OfferCrossing offerCrossing_,
    bool isDefaultPath_,
    std::array<boost::container::flat_set<Asset>, 2>& seenDirectAssets_,
    boost::container::flat_set<Asset>& seenBookOuts_,
    AMMContext& ammContext_,
    std::optional<uint256> const& domainID_,
    beast::Journal j_)
    : view(view_)
    , strandSrc(strandSrc_)
    , strandDst(strandDst_)
    , strandDeliver(strandDeliver_)
    , limitQuality(limitQuality_)
    , isFirst(strand_.empty())
    , isLast(isLast_)
    , ownerPaysTransferFee(ownerPaysTransferFee_)
    , offerCrossing(offerCrossing_)
    , isDefaultPath(isDefaultPath_)
    , strandSize(strand_.size())
    , prevStep(!strand_.empty() ? strand_.back().get() : nullptr)
    , seenDirectAssets(seenDirectAssets_)
    , seenBookOuts(seenBookOuts_)
    , ammContext(ammContext_)
    , domainID(domainID_)
    , j(j_)
{
}

template <class InAmt, class OutAmt>
bool
isDirectXrpToXrp(Strand const& strand)
{
    return false;
}

template <>
bool
isDirectXrpToXrp<XRPAmount, XRPAmount>(Strand const& strand)
{
    return (strand.size() == 2);
}

template bool
isDirectXrpToXrp<XRPAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, XRPAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<IOUAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, IOUAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<XRPAmount, MPTAmount>(Strand const& strand);
template bool
isDirectXrpToXrp<MPTAmount, XRPAmount>(Strand const& strand);

}  // namespace ripple
