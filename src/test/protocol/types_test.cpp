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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

struct types_test : public beast::unit_test::suite
{
    void
    testAccountID()
    {
        auto const s = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        if (auto const parsed = parseBase58<AccountID>(s); BEAST_EXPECT(parsed))
        {
            BEAST_EXPECT(toBase58(*parsed) == s);
        }

        {
            auto const s =
                "âabcd1rNxp4h8apvRis6mJf9Sh8C6iRxfrDWNâabcdAVâ\xc2\x80\xc2\x8f";
            BEAST_EXPECT(!parseBase58<AccountID>(s));
        }
    }

    void
    run() override
    {
        testAccountID();
    }
};

BEAST_DEFINE_TESTSUITE(types, protocol, ripple);

}  // namespace ripple
