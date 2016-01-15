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

#include <BeastConfig.h>
#include <ripple/app/paths/impl/DirectStep.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/XRPEndpointStep.h>
#include <ripple/basics/contract.h>
#include <ripple/json/json_writer.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/XRPAmount.h>

#include <numeric>
#include <sstream>

namespace ripple {

// Check equal with tolerance
bool checkEqual (IOUAmount const& expected, IOUAmount const& actual)
{
    double const ratTol = 0.001;
    if (expected.exponent () != actual.exponent ())
        return false;

    if (actual.exponent () < -20)
        return true;

    auto const a = expected.mantissa ();
    auto const b = actual.mantissa ();
    if (a == b)
        return true;

    auto const diff = std::abs (a - b);
    auto const r = double(diff) / double(std::max (std::abs (a), std::abs (b)));
    return r <= ratTol;
};

bool checkEqual (XRPAmount const& expected, XRPAmount const& actual)
{
    return expected == actual;
};

static
bool isXRPAccount (STPathElement const& pe)
{
    if (pe.getNodeType () != STPathElement::typeAccount)
        return false;
    return isXRP (pe.getAccountID ());
};


static
std::pair<TER, std::unique_ptr<Step>>
toStep (
    StrandContext const& ctx,
    STPathElement const* e1,
    STPathElement const* e2,
    Issue const& curIssue)
{
    auto j = ctx.logs.journal ("Flow");

    if (ctx.isFirst && e1->isAccount () &&
        (e1->getNodeType () & STPathElement::typeCurrency) &&
        isXRP (e1->getCurrency ()))
    {
        return make_XRPEndpointStep (ctx, e1->getAccountID ());
    }

    if (ctx.isLast && isXRPAccount (*e1) && e2->isAccount())
        return make_XRPEndpointStep (ctx, e2->getAccountID());

    if (e1->isAccount() && e2->isAccount())
    {
        return make_DirectStepI (ctx, e1->getAccountID (),
            e2->getAccountID (), curIssue.currency);
    }

    if (e1->isOffer() && e2->isAccount())
    {
        // should already be taken care of
        JLOG (j.warning)
            << "Found offer/account payment step. Aborting payment strand.";
        Throw<StepError> (tefEXCEPTION, "Found offer/account payment step.");
    }

    assert ((e2->getNodeType () & STPathElement::typeCurrency) ||
        (e2->getNodeType () & STPathElement::typeIssuer));
    auto const outCurrency = e2->getNodeType () & STPathElement::typeCurrency
        ? e2->getCurrency ()
        : curIssue.currency;
    auto const outIssuer = e2->getNodeType () & STPathElement::typeIssuer
        ? e2->getIssuerID ()
        : curIssue.account;

    if (isXRP (curIssue.currency) && isXRP (outCurrency))
    {
        JLOG (j.warning) << "Found xrp/xrp offer payment step";
        return {temBAD_PATH, std::unique_ptr<Step>{}};
    }

    assert (e2->isOffer ());

    if (isXRP (outCurrency))
        return make_BookStepIX (ctx, curIssue);

    if (isXRP (curIssue.currency))
        return make_BookStepXI (ctx, {outCurrency, outIssuer});

    return make_BookStepII (ctx, curIssue, {outCurrency, outIssuer});
}

std::pair<TER, Strand>
toStrand (
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Issue const& deliver,
    boost::optional<Issue> const& sendMaxIssue,
    STPath const& path,
    Logs& l)
{
    auto j = l.journal ("Flow");

    if (isXRP (src))
    {
        JLOG (j.debug) << "toStrand with xrpAccount as src";
        return {temBAD_PATH, Strand{}};
    }
    if (isXRP (dst))
    {
        JLOG (j.debug) << "toStrand with xrpAccount as dst";
        return {temBAD_PATH, Strand{}};
    }
    if (!isConsistent (deliver))
    {
        JLOG (j.debug) << "toStrand inconsistent deliver issue";
        return {temBAD_PATH, Strand{}};
    }
    if (sendMaxIssue && !isConsistent (*sendMaxIssue))
    {
        JLOG (j.debug) << "toStrand inconsistent sendMax issue";
        return {temBAD_PATH, Strand{}};
    }

    Issue curIssue = [&]
    {
        auto& currency =
            sendMaxIssue ? sendMaxIssue->currency : deliver.currency;
        if (isXRP (currency))
            return xrpIssue ();
        return Issue{currency, src};
    }();

    STPathElement const firstNode (
        STPathElement::typeAll, src, curIssue.currency, curIssue.account);

    boost::optional<STPathElement> sendMaxPE;
    if (sendMaxIssue && sendMaxIssue->account != src)
        sendMaxPE.emplace (sendMaxIssue->account, boost::none, boost::none);

    STPathElement const lastNode (dst, boost::none, boost::none);

    auto isAccount = [](STPathElement const* pe)
    {
        return pe->isAccount ();
    };
    auto isOffer = [](STPathElement const* pe)
    {
        return pe->isOffer ();
    };
    auto hasIssuer = [](STPathElement const* pe)
    {
        return pe->getNodeType () & STPathElement::typeIssuer;
    };
    auto hasCurrency = [](STPathElement const* pe)
    {
        return pe->getNodeType () & STPathElement::typeCurrency;
    };
    auto isNone = [](STPathElement const* pe)
    {
        return pe->getNodeType () == STPathElement::typeNone;
    };

    boost::optional<STPathElement> deliverOfferNode;
    boost::optional<STPathElement> deliverAccountNode;

    std::vector<STPathElement const*> pes;
    pes.reserve (4 + path.size ());
    pes.push_back (&firstNode);
    if (sendMaxPE)
        pes.push_back (&*sendMaxPE);
    for (auto& i : path)
        pes.push_back (&i);
    auto const lastCurrency=[&]{
        for(auto i=pes.crbegin(), e=pes.crend();i!=e;++i)
        {
            if (hasCurrency (*i))
                return (*i)->getCurrency();
        }
        assert(0);
        return deliver.currency;
    }();
    if (lastCurrency != deliver.currency)
    {
        deliverOfferNode.emplace (boost::none, deliver.currency, deliver.account);
        pes.push_back (&*deliverOfferNode);
    }
    if (!((isAccount (pes.back ()) && pes.back ()->getAccountID () == deliver.account) ||
          (isAccount (&lastNode) && lastNode.getAccountID () == deliver.account)))
    {
        deliverAccountNode.emplace (deliver.account, boost::none, boost::none);
        pes.push_back (&*deliverAccountNode);
    }
    if (*pes.back() != lastNode)
        pes.push_back (&lastNode);

    auto const strandSrc = firstNode.getAccountID ();
    auto const strandDst = lastNode.getAccountID ();

    Strand result;
    result.reserve (2 * pes.size ());

    // A strand may not include the same account node more than once
    // in the same currency. In a direct step, an account will show up
    // at most twice: once as a src and once as a dst (hence the two element array).
    // The strandSrc and strandDst will only show up once each.
    std::array<boost::container::flat_set<Issue>, 2> seenDirectIssues;
    // A strand may not include the same offer book more than once
    boost::container::flat_set<Book> seenBooks;
    seenDirectIssues[0].reserve (pes.size());
    seenDirectIssues[1].reserve (pes.size());
    seenBooks.reserve (pes.size());
    auto ctx = [&](bool isLast = false)
    {
        return StrandContext{view, result, strandSrc, strandDst, isLast,
            seenDirectIssues, seenBooks, l};
    };

    for (int i = 0; i < pes.size () - 1; ++i)
    {
        // Iterate through the path elements considering them in pairs, cur and
        // next. When an offer is one of the pairs, the step created will be for
        // `next`, so when the first element is an offer and the second is an
        // account, no step is created, as there is already a step for that
        // offer.
        boost::optional<STPathElement> impliedPE;
        auto cur = pes[i];
        auto next = pes[i + 1];

        if (isNone(cur) || isNone(next))
            return {temBAD_PATH, Strand{}};

        if (hasIssuer(cur))
            curIssue.account = cur->getIssuerID ();
        else if (isAccount (cur))
            curIssue.account = cur->getAccountID ();

        if (hasCurrency(cur))
            curIssue.currency = cur->getCurrency ();

        if (isAccount (cur) && isAccount(next))
        {
            if (!isXRP (curIssue.currency) &&
                curIssue.account != cur->getAccountID () &&
                curIssue.account != next->getAccountID ())
            {
                JLOG (j.trace) << "Inserting implied account";
                auto msr = make_DirectStepI (ctx(), cur->getAccountID (),
                    curIssue.account, curIssue.currency);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
                Currency dummy;
                impliedPE.emplace (STPathElement::typeAccount,
                    curIssue.account, dummy, curIssue.account);
                cur = &*impliedPE;
            }
        }
        else if (isAccount (cur) && isOffer(next))
        {
            if (curIssue.account != cur->getAccountID ())
            {
                JLOG (j.trace) << "Inserting implied account before offer";
                auto msr = make_DirectStepI (ctx(), cur->getAccountID (),
                    curIssue.account, curIssue.currency);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
                Currency dummy;
                impliedPE.emplace (STPathElement::typeAccount,
                    curIssue.account, dummy, curIssue.account);
                cur = &*impliedPE;
            }
        }
        else if (isOffer (cur) && isAccount (next))
        {
            if (curIssue.account != next->getAccountID () &&
                !isXRP (next->getAccountID ()))
            {
                JLOG (j.trace) << "Inserting implied account after offer";
                auto msr = make_DirectStepI (ctx(), curIssue.account,
                    next->getAccountID (), curIssue.currency);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
            }
            continue;
        }

        if (!isOffer (next) &&
            hasCurrency(next) && next->getCurrency () != curIssue.currency)
        {
            auto const& nextCurrency = next->getCurrency ();
            auto const& nextIssuer = hasIssuer(next) ? next->getIssuerID () : curIssue.account;

            if (isXRP (curIssue.currency))
            {
                JLOG (j.trace) << "Inserting implied XI offer";
                auto msr = make_BookStepXI (
                    ctx(), {nextCurrency, nextIssuer});
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
            }
            else if (isXRP (nextCurrency))
            {
                JLOG (j.trace) << "Inserting implied IX offer";
                auto msr = make_BookStepIX (ctx(), curIssue);
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
            }
            else
            {
                JLOG (j.trace) << "Inserting implied II offer";
                auto msr = make_BookStepII (
                    ctx(), curIssue, {nextCurrency, nextIssuer});
                if (msr.first != tesSUCCESS)
                    return {msr.first, Strand{}};
                result.push_back (std::move (msr.second));
            }
            impliedPE.emplace (
                boost::none, nextCurrency, nextIssuer);
            cur = &*impliedPE;
            curIssue.currency = nextCurrency;
            curIssue.account = nextIssuer;
        }

        auto s =
            toStep (ctx (/*isLast*/ i == pes.size () - 2), cur, next, curIssue);
        if (s.first == tesSUCCESS)
            result.emplace_back (std::move (s.second));
        else
        {
            JLOG (j.warning) << "toStep failed";
            return {s.first, Strand{}};
        }
    }

    // Any XRPEndpointStep steps must be either the first or last step
    for (int i=1; i<result.size()-1;++i)
    {
        if (dynamic_cast<XRPEndpointStep*>(result[i].get()))
            return {temBAD_PATH, Strand{}};
    }

    return {tesSUCCESS, std::move (result)};
}

std::pair<TER, std::vector<Strand>>
toStrands (
    ReadView const& view,
    AccountID const& src,
    AccountID const& dst,
    Issue const& deliver,
    boost::optional<Issue> const& sendMax,
    STPathSet const& paths,
    bool addDefaultPath,
    Logs& l)
{
    auto j = l.journal ("View");
    std::vector<Strand> result;
    result.reserve (1 + paths.size ());
    auto const add = [&](STPath const& p) -> TER
    {
        auto sp = toStrand (view, src, dst, deliver, sendMax, p, l);
        auto ter = sp.first;
        auto& strand = sp.second;
        if (ter != tesSUCCESS)
            return ter;

        if (strand.empty ())
        {
            JLOG (j.trace) << "toStrand failed";
            Throw<StepError> (tefEXCEPTION, "toStrand returned tes & empty strand");
        }

        bool const hasStrand = [&]
        {
            auto const curStrandSize = strand.size ();
            for (auto const& rs : result)
            {
                bool const strandEqual = [&]
                {
                    if (rs.size () != curStrandSize)
                        return false;
                    for (size_t i = 0; i < curStrandSize; ++i)
                        if (*rs[i] != *strand[i])
                            return false;
                    return true;
                }();
                if (strandEqual)
                    return true;
            };
            return false;
        }();

        if (!hasStrand)
            result.emplace_back (std::move (strand));
        return ter;
    };

    if (addDefaultPath)
    {
        auto ter = add (STPath ());
        if (ter != tesSUCCESS)
        {
            JLOG (j.trace) << "failed to add default path";
            if (isTemMalformed (ter) || paths.empty ()) {
                return {ter, std::vector<Strand>{}};
            }
        }
    }
    else if (paths.empty ())
    {
        JLOG (j.debug)
            << "Flow: Invalid transaction: No paths and direct ripple not allowed.";
        return {temRIPPLE_EMPTY, std::vector<Strand>{}};
    }

    TER lastFailTer = tesSUCCESS;
    for (auto const& p : paths)
    {
        auto ter = add (p);
        if (ter != tesSUCCESS)
        {
            lastFailTer = ter;
            JLOG (j.trace)
                << "failed to add path: ter: " << ter << "path: " << p.getJson(0);
            if (isTemMalformed (ter))
                return {ter, std::vector<Strand>{}};
        }
    }

    if (result.empty ())
        return {lastFailTer, std::move (result)};

    return {tesSUCCESS, std::move (result)};
}

static
boost::optional<AccountID>
getDSSrcAccount (Step const& s)
{
    if (auto ds = dynamic_cast<DirectStepI const*>(&s))
        return ds->src();
    return boost::none;
}

StrandContext::StrandContext (
    ReadView const& view_,
    std::vector<std::unique_ptr<Step>> const& strand_,
    // A strand may not include an inner node that
    // replicates the source or destination.
    AccountID strandSrc_,
    AccountID strandDst_,
    bool isLast_,
    std::array<boost::container::flat_set<Issue>, 2>& seenDirectIssues_,
    boost::container::flat_set<Book>& seenBooks_,
    Logs& logs_)
        : view (view_)
        , strandSrc (strandSrc_)
        , strandDst (strandDst_)
        , isFirst (strand_.empty ())
        , isLast (isLast_)
        , strandSize (strand_.size ())
        , prevDSSrc (!strand_.empty () ? getDSSrcAccount (*strand_.back ())
                     : boost::none)
        , seenDirectIssues(seenDirectIssues_)
        , seenBooks(seenBooks_)
        , logs (logs_)
{
}

} // ripple
