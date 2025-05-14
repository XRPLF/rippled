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

#include <test/jtx/sig.h>
#include <test/jtx/utility.h>

namespace ripple {
namespace test {
namespace jtx {

void
sig::operator()(Env&, JTx& jt) const
{
    if (!manual_)
        return;
    if (!subField)
        jt.fill_sig = false;
    if (account_)
    {
        // VFALCO Inefficient pre-C++14
        auto const account = *account_;
        auto callback = [subField = subField, account](Env&, JTx& jtx) {
            // Where to put the signature. Supports sfCounterPartySignature.
            auto& sigObject = subField ? jtx[*subField] : jtx.jv;

            jtx::sign(jtx.jv, account, sigObject);
        };
        if (!subField)
            jt.mainSigners.emplace_back(callback);
        else
            jt.postSigners.emplace_back(callback);
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
