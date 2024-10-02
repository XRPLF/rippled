//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>

#include <limits>

namespace ripple {

struct STNumber_test : public beast::unit_test::suite
{
    void
    run() override
    {
        {
            STNumber const stnum{sfQuantity};
            BEAST_EXPECT(stnum.getSType() == STI_NUMBER);
            BEAST_EXPECT(stnum.getText() == "0");
            BEAST_EXPECT(stnum.isDefault() == true);
            BEAST_EXPECT(stnum.value() == Number{0});
        }

        std::initializer_list<std::int64_t> const values = {
            std::numeric_limits<std::int64_t>::min(),
            -1,
            0,
            1,
            std::numeric_limits<std::int64_t>::max()};
        for (std::int64_t value : values)
        {
            STNumber const before{sfQuantity, value};
            Serializer s;
            before.add(s);
            BEAST_EXPECT(s.size() == 12);
            SerialIter sit(s.slice());
            STNumber const after{sit, sfQuantity};
            BEAST_EXPECT(after.isEquivalent(before));
        }

        {
            STAmount const strikePrice{noIssue(), 100};
            STNumber const factor{sfQuantity, 100};
            auto const iouValue = strikePrice.iou();
            IOUAmount totalValue{iouValue * factor};
            STAmount const totalAmount{totalValue, strikePrice.issue()};
            BEAST_EXPECT(totalAmount == Number{10'000});
        }
    }
};

BEAST_DEFINE_TESTSUITE(STNumber, protocol, ripple);

}  // namespace ripple
