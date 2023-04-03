//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/basics/strHex.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class Memo_test : public beast::unit_test::suite
{
public:
    void
    testMemos()
    {
        testcase("Test memos");

        using namespace test::jtx;
        Account alice{"alice"};

        Env env(*this);
        env.fund(XRP(10000), alice);
        env.close();

        // Lambda that returns a valid JTx with a memo that we can hack up.
        // This is the basis for building tests of invalid states.
        auto makeJtxWithMemo = [&env, &alice]() {
            JTx example = noop(alice);
            memo const exampleMemo{"tic", "tac", "toe"};
            exampleMemo(env, example);
            return example;
        };

        // A valid memo.
        env(makeJtxWithMemo());
        env.close();

        {
            // Make sure that too big a memo is flagged as invalid.
            JTx memoSize = makeJtxWithMemo();
            memoSize.jv[sfMemos.jsonName][0u][sfMemo.jsonName]
                       [sfMemoData.jsonName] = std::string(2020, '0');
            env(memoSize, ter(temINVALID));

            // This memo is just barely small enough.
            memoSize.jv[sfMemos.jsonName][0u][sfMemo.jsonName]
                       [sfMemoData.jsonName] = std::string(2018, '1');
            env(memoSize);
        }
        {
            // Put a non-Memo in the Memos array.
            JTx memoNonMemo = noop(alice);
            auto& jv = memoNonMemo.jv;
            auto& ma = jv[sfMemos.jsonName];
            auto& mi = ma[ma.size()];
            auto& m = mi[sfCreatedNode.jsonName];  // CreatedNode in Memos
            m[sfMemoData.jsonName] = "3030303030";

            env(memoNonMemo, ter(temINVALID));
        }
        {
            // Put an invalid field in a Memo object.
            JTx memoExtra = makeJtxWithMemo();
            memoExtra
                .jv[sfMemos.jsonName][0u][sfMemo.jsonName][sfFlags.jsonName] =
                13;
            env(memoExtra, ter(temINVALID));
        }
        {
            // Put a character that is not allowed in a URL in a MemoType field.
            JTx memoBadChar = makeJtxWithMemo();
            memoBadChar.jv[sfMemos.jsonName][0u][sfMemo.jsonName]
                          [sfMemoType.jsonName] =
                strHex(std::string_view("ONE<INFINITY"));
            env(memoBadChar, ter(temINVALID));
        }
        {
            // Put a character that is not allowed in a URL in a MemoData field.
            // That's okay.
            JTx memoLegitChar = makeJtxWithMemo();
            memoLegitChar.jv[sfMemos.jsonName][0u][sfMemo.jsonName]
                            [sfMemoData.jsonName] =
                strHex(std::string_view("ONE<INFINITY"));
            env(memoLegitChar);
        }
        {
            // Put a character that is not allowed in a URL in a MemoFormat.
            JTx memoBadChar = makeJtxWithMemo();
            memoBadChar.jv[sfMemos.jsonName][0u][sfMemo.jsonName]
                          [sfMemoFormat.jsonName] =
                strHex(std::string_view("NoBraces{}InURL"));
            env(memoBadChar, ter(temINVALID));
        }
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        testMemos();
    }
};

BEAST_DEFINE_TESTSUITE(Memo, ripple_data, ripple);

}  // namespace ripple
