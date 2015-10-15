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
#include <ripple/app/tx/apply.h>
#include <ripple/app/tx/applySteps.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/protocol/Feature.h>

namespace ripple {

// These are the same flags defined as SF_PRIVATE1-4 in HashRouter.h
#define SF_SIGBAD      SF_PRIVATE1    // Signature is bad
#define SF_SIGGOOD     SF_PRIVATE2    // Signature is good
#define SF_LOCALBAD    SF_PRIVATE3    // Local checks failed
#define SF_LOCALGOOD   SF_PRIVATE4    // Local checks passed

//------------------------------------------------------------------------------

std::pair<Validity, std::string>
checkValidity(HashRouter& router, STTx const& tx, bool allowMultiSign)
{
    auto const id = tx.getTransactionID();
    auto const flags = router.getFlags(id);
    if (flags & SF_SIGBAD)
        // Signature is known bad
        return std::make_pair(Validity::SigBad,
            "Transaction has bad signature.");
    if (!(flags & SF_SIGGOOD))
    {
        // Don't know signature state. Check it.
        if (!tx.checkSign(allowMultiSign))
        {
            router.setFlags(id, SF_SIGBAD);
            return std::make_pair(Validity::SigBad,
                "Transaction has bad signature.");
        }
        router.setFlags(id, SF_SIGGOOD);
    }

    // Signature is now known good
    if (flags & SF_LOCALBAD)
        // ...but the local checks
        // are known bad.
        return std::make_pair(Validity::SigGoodOnly,
            "Local checks failed.");
    if (flags & SF_LOCALGOOD)
        // ...and the local checks
        // are known good.
        return std::make_pair(Validity::Valid, "");

    // Do the local checks
    std::string reason;
    if (!passesLocalChecks(tx, reason))
    {
        router.setFlags(id, SF_LOCALBAD);
        return std::make_pair(Validity::SigGoodOnly, reason);
    }
    router.setFlags(id, SF_LOCALGOOD);
    return std::make_pair(Validity::Valid, "");
}

std::pair<Validity, std::string>
checkValidity(HashRouter& router,
    STTx const& tx, Rules const& rules,
        Config const& config,
            ApplyFlags const& flags)
{
    auto const allowMultiSign =
        (flags & tapENABLE_TESTING) ||
            (rules.enabled(featureMultiSign,
                config.features));

    return checkValidity(router, tx, allowMultiSign);
}

void
forceValidity(HashRouter& router, uint256 const& txid,
    Validity validity)
{
    int flags = 0;
    switch (validity)
    {
    case Validity::Valid:
        flags |= SF_LOCALGOOD;
        // fall through
    case Validity::SigGoodOnly:
        flags |= SF_SIGGOOD;
        // fall through
    case Validity::SigBad:
        // would be silly to call directly
        break;
    }
    if (flags)
        router.setFlags(txid, flags);
}

std::pair<TER, bool>
apply (Application& app, OpenView& view,
    STTx const& tx, ApplyFlags flags,
        beast::Journal j)
{
    auto pfresult = preflight(app, view.rules(),
        tx, flags, j);
    auto pcresult = preclaim(pfresult, app, view);
    return doApply(pcresult, app, view);
}

} // ripple
