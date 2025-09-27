//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/misc/FirewallHelpers.h>

#include <xrpl/protocol/TER.h>

namespace ripple {
namespace firewall {

NotTEC
checkFirewallSigners(PreflightContext const& ctx)
{
    if (!ctx.tx.isFieldPresent(sfFirewallSigners))
    {
        JLOG(ctx.j.trace())
            << "checkFirewallSigners: sfFirewallSigners required";
        return temMALFORMED;
    }
    // Validate signers structure - similar to Batch validation
    auto const& signers = ctx.tx.getFieldArray(sfFirewallSigners);
    if (signers.empty())
    {
        JLOG(ctx.j.trace())
            << "checkFirewallSigners: sfFirewallSigners cannot be empty";
        return temMALFORMED;
    }

    // None of the signers can be the outer account
    for (auto const& signer : signers)
    {
        if (signer.getAccountID(sfAccount) == ctx.tx.getAccountID(sfAccount))
        {
            JLOG(ctx.j.trace())
                << "checkFirewallSigners: sfFirewallSigners cannot include the "
                   "outer account";
            return temMALFORMED;
        }
    }

    auto const sigResult = ctx.tx.checkFirewallSign(
        STTx::RequireFullyCanonicalSig::yes, ctx.rules);
    if (!sigResult)
    {
        JLOG(ctx.j.trace())
            << "checkFirewallSigners: invalid firewall signature: "
            << sigResult.error();
        return temBAD_SIGNATURE;
    }

    return tesSUCCESS;
}

}  // namespace firewall
}  // namespace ripple
