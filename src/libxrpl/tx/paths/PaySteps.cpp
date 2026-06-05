#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace xrpl {

// Check equal with tolerance
bool
checkNear(IOUAmount const& expected, IOUAmount const& actual)
{
    double const ratTol = 0.001;
    if (abs(expected.exponent() - actual.exponent()) > 1)
        return false;

    if (actual.exponent() < -20)
        return true;

    auto const a =
        (expected.exponent() < actual.exponent()) ? expected.mantissa() / 10 : expected.mantissa();
    auto const b =
        (actual.exponent() < expected.exponent()) ? actual.mantissa() / 10 : actual.mantissa();
    if (a == b)
        return true;

    double const diff = std::abs(a - b);
    auto const r = diff / std::max(std::abs(a), std::abs(b));
    return r <= ratTol;
};

static bool
isXRPAccount(STPathElement const& pe)
{
    if (pe.getNodeType() != STPathElement::TypeAccount)
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
        ((e1->getNodeType() & STPathElement::TypeCurrency) != 0u) && e1->getPathAsset().isXRP())
    {
        return makeXrpEndpointStep(ctx, e1->getAccountID());
    }

    if (ctx.isLast && isXRPAccount(*e1) && e2->isAccount())
        return makeXrpEndpointStep(ctx, e2->getAccountID());

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
        return curAsset.visit(
            [&](MPTIssue const& issue) {
                return makeMptEndpointStep(
                    ctx, e1->getAccountID(), e2->getAccountID(), issue.getMptID());
            },
            [&](Issue const& issue) {
                return makeDirectStepI(ctx, e1->getAccountID(), e2->getAccountID(), issue.currency);
            });
    }

    if (e1->isOffer() && e2->isAccount())
    {
        // LCOV_EXCL_START
        // should already be taken care of
        JLOG(j.error()) << "Found offer/account payment step. Aborting payment strand.";
        UNREACHABLE("xrpl::toStep : offer/account payment payment strand");
        return {temBAD_PATH, std::unique_ptr<Step>{}};
        // LCOV_EXCL_STOP
    }

    XRPL_ASSERT(
        (e2->getNodeType() & STPathElement::TypeAsset) ||
            (e2->getNodeType() & STPathElement::TypeIssuer),
        "xrpl::toStep : currency or issuer");
    PathAsset const outAsset =
        ((e2->getNodeType() & STPathElement::TypeAsset) != 0u) ? e2->getPathAsset() : curAsset;
    auto const outIssuer = ((e2->getNodeType() & STPathElement::TypeIssuer) != 0u)
        ? e2->getIssuerID()
        : curAsset.getIssuer();

    if (isXRP(curAsset) && outAsset.isXRP())
    {
        JLOG(j.info()) << "Found xrp/xrp offer payment step";
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    XRPL_ASSERT(e2->isOffer(), "xrpl::toStep : is offer");

    if (outAsset.isXRP())
    {
        return curAsset.visit(
            [&](MPTIssue const& issue) { return makeBookStepMx(ctx, issue); },
            [&](Issue const& issue) { return makeBookStepIx(ctx, issue); });
    }

    if (isXRP(curAsset))
    {
        return outAsset.visit(
            [&](MPTID const& mpt) { return makeBookStepXm(ctx, mpt); },
            [&](Currency const& currency) { return makeBookStepXi(ctx, {currency, outIssuer}); });
    }

    return curAsset.visit(
        [&](MPTIssue const& issue) {
            return outAsset.visit(
                [&](Currency const& currency) {
                    return makeBookStepMi(ctx, issue, {currency, outIssuer});
                },
                [&](MPTID const& mpt) { return makeBookStepMm(ctx, issue, mpt); });
        },
        [&](Issue const& issue) {
            return outAsset.visit(
                [&](MPTID const& mpt) { return makeBookStepIm(ctx, issue, mpt); },
                [&](Currency const& currency) {
                    return makeBookStepIi(ctx, issue, {currency, outIssuer});
                });
        });
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

    if ((sendMaxAsset && sendMaxAsset->getIssuer() == noAccount()) || (src == noAccount()) ||
        (dst == noAccount()) || (deliver.getIssuer() == noAccount()))
        return {temBAD_PATH, Strand{}};

    if ((deliver.holds<MPTIssue>() && deliver.getIssuer() == beast::kZero) ||
        (sendMaxAsset && sendMaxAsset->holds<MPTIssue>() &&
         sendMaxAsset->getIssuer() == beast::kZero))
        return {temBAD_PATH, Strand{}};

    for (std::size_t i = 0; i < path.size(); ++i)
    {
        auto const& pe = path[i];
        auto const t = pe.getNodeType();

        if (((t & ~STPathElement::TypeAll) != 0u) || (t == 0u))
            return {temBAD_PATH, Strand{}};

        bool const hasAccount = (t & STPathElement::TypeAccount) != 0u;
        bool const hasIssuer = (t & STPathElement::TypeIssuer) != 0u;
        bool const hasCurrency = (t & STPathElement::TypeCurrency) != 0u;
        bool const hasMPT = (t & STPathElement::TypeMpt) != 0u;
        bool const hasAsset = (t & STPathElement::TypeAsset) != 0u;

        if (hasAccount && (hasIssuer || hasCurrency))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && isXRP(pe.getAccountID()))
            return {temBAD_PATH, Strand{}};

        if (hasCurrency && hasIssuer && isXRP(pe.getCurrency()) != isXRP(pe.getIssuerID()))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer && (pe.getIssuerID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasAccount && (pe.getAccountID() == noAccount()))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && (hasCurrency || hasAccount))
            return {temBAD_PATH, Strand{}};

        if (hasMPT && hasIssuer && (pe.getIssuerID() != getMPTIssuer(pe.getMPTID())))
            return {temBAD_PATH, Strand{}};

        // No rippling if MPT
        if (i > 0 && path[i - 1].hasMPT() && (hasAccount || (hasIssuer && !hasAsset)))
            return {temBAD_PATH, Strand{}};
    }

    Asset curAsset = [&]() -> Asset {
        auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
        return asset.visit(
            [&](MPTIssue const& issue) -> Asset { return asset; },
            [&](Issue const& issue) -> Asset {
                if (isXRP(asset))
                    return xrpIssue();
                // First step ripples from the source to the issuer.
                return Issue{issue.currency, src};
            });
    }();

    // Currency or MPT
    auto hasAsset = [](STPathElement const pe) {
        return pe.getNodeType() & STPathElement::TypeAsset;
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
            auto const t = STPathElement::TypeAccount | STPathElement::TypeIssuer;
            return curAsset.visit(
                [&](MPTIssue const&) { return t | STPathElement::TypeMpt; },
                [&](Issue const&) { return t | STPathElement::TypeCurrency; });
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
            normPath.emplace_back(sendMaxAsset->getIssuer(), std::nullopt, std::nullopt);
        }

        for (auto const& i : path)
            normPath.push_back(i);

        {
            // Note that for offer crossing (only) we do use an offer book
            // even if all that is changing is the Issue.account. Note
            // that MPTIssue can't change the account.
            STPathElement const& lastAsset =
                *std::ranges::find_if(std::ranges::reverse_view(normPath), hasAsset);
            if (lastAsset.getPathAsset() != deliver ||
                (offerCrossing != OfferCrossing::No &&
                 lastAsset.getIssuerID() != deliver.getIssuer()))
            {
                normPath.emplace_back(std::nullopt, deliver, deliver.getIssuer());
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
            normPath.emplace_back(deliver.getIssuer(), std::nullopt, std::nullopt);
        }

        // Last step of a path is always implied to be the receiver of a
        // transaction, as defined by the transaction's Destination field.
        if (!normPath.back().isAccount() || normPath.back().getAccountID() != dst)
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
        {
            curAsset = Issue{};
        }

        // Can only update the account for Issue since MPTIssue's account
        // is immutable as it is part of MPTID.
        curAsset.visit(
            [&](Issue const&) {
                if (cur->isAccount())
                {
                    curAsset.get<Issue>().account = cur->getAccountID();
                }
                else if (cur->hasIssuer())
                {
                    curAsset.get<Issue>().account = cur->getIssuerID();
                }
            },
            [](MPTIssue const&) {});

        if (cur->hasCurrency())
        {
            curAsset = Issue{cur->getCurrency(), curAsset.getIssuer()};
            if (isXRP(curAsset))
                curAsset.get<Issue>().account = xrpAccount();
        }
        else if (cur->hasMPT())
        {
            curAsset = cur->getPathAsset().get<MPTID>();
        }

        using ImpliedStepRet = std::pair<TER, std::unique_ptr<Step>>;
        auto getImpliedStep =
            [&](AccountID const& src, AccountID const& dst, Asset const& asset) -> ImpliedStepRet {
            return asset.visit(
                [&](MPTIssue const&) -> ImpliedStepRet {
                    JLOG(j.error()) << "MPT is invalid with rippling";
                    return {temBAD_PATH, nullptr};
                },
                [&](Issue const& issue) -> ImpliedStepRet {
                    return makeDirectStepI(ctx(), src, dst, issue.currency);
                });
        };

        if (cur->isAccount() && next->isAccount())
        {
            // This block doesn't execute
            // since curAsset's account is set to cur's account above.
            // It should not execute for MPT either because MPT rippling
            // is invalid. Should this block be removed/amendment excluded?
            if (!isXRP(curAsset) && curAsset.getIssuer() != cur->getAccountID() &&
                curAsset.getIssuer() != next->getAccountID())
            {
                JLOG(j.trace()) << "Inserting implied account";
                auto msr = getImpliedStep(cur->getAccountID(), curAsset.getIssuer(), curAsset);
                if (!isTesSuccess(msr.first))
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::TypeAccount, curAsset.getIssuer(), xrpCurrency(), xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isAccount() && next->isOffer())
        {
            // Same as above, this block doesn't execute.
            if (curAsset.getIssuer() != cur->getAccountID())
            {
                JLOG(j.trace()) << "Inserting implied account before offer";
                auto msr = getImpliedStep(cur->getAccountID(), curAsset.getIssuer(), curAsset);
                if (!isTesSuccess(msr.first))
                    return {msr.first, Strand{}};
                result.push_back(std::move(msr.second));
                impliedPE.emplace(
                    STPathElement::TypeAccount, curAsset.getIssuer(), xrpCurrency(), xrpAccount());
                cur = &*impliedPE;
            }
        }
        else if (cur->isOffer() && next->isAccount())
        {
            // If the offer sells MPT, then next's account is always the issuer.
            // See how normPath step is added for second-to-last or last
            // step. Therefore, this block never executes if MPT.
            if (curAsset.getIssuer() != next->getAccountID() && !isXRP(next->getAccountID()))
            {
                if (isXRP(curAsset))
                {
                    if (i != normPath.size() - 2)
                        return {temBAD_PATH, Strand{}};

                    // Last step. insert xrp endpoint step
                    auto msr = makeXrpEndpointStep(ctx(), next->getAccountID());
                    if (!isTesSuccess(msr.first))
                        return {msr.first, Strand{}};
                    result.push_back(std::move(msr.second));
                }
                else
                {
                    JLOG(j.trace()) << "Inserting implied account after offer";
                    auto msr = getImpliedStep(curAsset.getIssuer(), next->getAccountID(), curAsset);
                    if (!isTesSuccess(msr.first))
                        return {msr.first, Strand{}};
                    result.push_back(std::move(msr.second));
                }
            }
            continue;
        }

        if (!next->isOffer() && next->hasAsset() && next->getPathAsset() != curAsset)
        {
            // Should never happen
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::toStrand : offer currency mismatch");
            return {temBAD_PATH, Strand{}};
            // LCOV_EXCL_STOP
        }

        auto s = toStep(ctx(/*isLast*/ i == normPath.size() - 2), cur, next, curAsset);
        if (isTesSuccess(s.first))
        {
            result.emplace_back(std::move(s.second));
        }
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
            Throw<FlowException>(tefEXCEPTION, "Step should be either a direct or book step");
            return std::make_pair(xrpAccount(), xrpAccount());
        };

        auto curAcc = src;
        auto curAsset = [&]() -> Asset {
            auto const& asset = sendMaxAsset ? *sendMaxAsset : deliver;
            return asset.visit(
                [&](MPTIssue const&) -> Asset { return asset; },
                [&](Issue const& issue) -> Asset {
                    if (isXRP(asset))
                        return xrpIssue();
                    return Issue{issue.currency, src};
                });
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
            (curAsset.holds<MPTIssue>() && curAsset.get<MPTIssue>() != deliver.get<MPTIssue>()))
            return false;
        if (curAsset.getIssuer() != deliver.getIssuer() && curAsset.getIssuer() != dst)
            return false;
        return true;
    };

    if (!checkStrand())
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Flow check strand failed";
        UNREACHABLE("xrpl::toStrand : invalid strand");
        return {temBAD_PATH, Strand{}};
        // LCOV_EXCL_STOP
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
        bool const hasStrand = std::ranges::find(result, s) != result.end();

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

        if (!isTesSuccess(ter))
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
            Throw<FlowException>(tefEXCEPTION, "toStrand returned tes & empty strand");
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

        if (!isTesSuccess(ter))
        {
            lastFailTer = ter;
            JLOG(j.trace()) << "failed to add path: ter: " << ter
                            << "path: " << p.getJson(JsonOptions::Values::None);
            if (isTemMalformed(ter))
                return {ter, std::vector<Strand>{}};
        }
        else if (strand.empty())
        {
            JLOG(j.trace()) << "toStrand failed";
            Throw<FlowException>(tefEXCEPTION, "toStrand returned tes & empty strand");
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
    ReadView const& view,
    std::vector<std::unique_ptr<Step>> const& strand,
    // A strand may not include an inner node that
    // replicates the source or destination.
    AccountID const& strandSrc,
    AccountID const& strandDst,
    Asset const& strandDeliver,
    std::optional<Quality> const& limitQuality,
    bool isLast,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    bool isDefaultPath,
    std::array<boost::container::flat_set<Asset>, 2>& seenDirectAssets,
    boost::container::flat_set<Asset>& seenBookOuts,
    AMMContext& ammContext,
    std::optional<uint256> const& domainId,
    beast::Journal j)
    : view(view)
    , strandSrc(strandSrc)
    , strandDst(strandDst)
    , strandDeliver(strandDeliver)
    , limitQuality(limitQuality)
    , isFirst(strand.empty())
    , isLast(isLast)
    , ownerPaysTransferFee(ownerPaysTransferFee)
    , offerCrossing(offerCrossing)
    , isDefaultPath(isDefaultPath)
    , strandSize(strand.size())
    , prevStep(!strand.empty() ? strand.back().get() : nullptr)
    , seenDirectAssets(seenDirectAssets)
    , seenBookOuts(seenBookOuts)
    , ammContext(ammContext)
    , domainID(domainId)
    , j(j)
{
}

}  // namespace xrpl
