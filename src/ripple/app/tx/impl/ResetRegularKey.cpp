//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019, Ripple Labs Inc.

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

#include <ripple/app/tx/impl/ResetRegularKey.h>
#include <ripple/basics/Log.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple
{

NotTEC
ResetRegularKey::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;
    return preflight2(ctx);
}

TER
ResetRegularKey::doApply ()
{
    auto const target = ctx_.tx.getAccountID(sfTarget);
    auto const root = view().peek(keylet::account(target));

    if (!root)
        return tecNO_TARGET;

    // Reset can't lock a account out
    if (root->isFlag(lsfDisableMaster) && !view().peek(keylet::signers(target)))
        return tecNO_ALTERNATIVE_KEY;

    auto const cb = (*root)[~sfRegularKeyReset];

    if (!cb)
        return tecNO_PERMISSION;

    using namespace cryptoconditions;

    std::error_code ec;

    auto rc = Condition::deserialize(*cb, ec);
    if (!rc)
        return tecCRYPTOCONDITION_ERROR;

    auto rf = Fulfillment::deserialize(ctx_.tx[sfRegularKeyReset], ec);
    if (!rf)
        return tecCRYPTOCONDITION_ERROR;

    if (!validate(*rf, *rc))
        return tecNO_AUTH;

    root->makeFieldAbsent(sfRegularKeyReset);
    root->makeFieldAbsent(sfRegularKey);

    return tesSUCCESS;
}

}
