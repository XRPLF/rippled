//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2017 Ripple Labs Inc.

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

#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/unit_test.h>
#include <ripple/ledger/View.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class RCLValidations_test : public beast::unit_test::suite
{
    void
    testChangeTrusted()
    {
        testcase("Change validation trusted status");
        auto keys = randomKeyPair(KeyType::secp256k1);
        auto v = std::make_shared<STValidation>(
            ripple::NetClock::time_point{},
            keys.first,
            keys.second,
            calcNodeID(keys.first),
            [&](STValidation& v) { v.setFieldU32(sfLedgerSequence, 123456); });

        BEAST_EXPECT(v->isTrusted());
        v->setUntrusted();
        BEAST_EXPECT(!v->isTrusted());

        RCLValidation rcv{v};
        BEAST_EXPECT(!rcv.trusted());
        rcv.setTrusted();
        BEAST_EXPECT(rcv.trusted());
        rcv.setUntrusted();
        BEAST_EXPECT(!rcv.trusted());
    }

    void
    testRCLValidatedLedger()
    {
        testcase("RCLValidatedLedger ancestry");

        using Seq = RCLValidatedLedger::Seq;
        using ID = RCLValidatedLedger::ID;

        // This tests RCLValidatedLedger properly implements the type
        // requirements of a LedgerTrie ledger, with its added behavior that
        // only the 256 prior ledger hashes are available to determine ancestry.
        Seq const maxAncestors = 256;

        //----------------------------------------------------------------------
        // Generate two ledger histories that agree on the first maxAncestors
        // ledgers, then diverge.

        std::vector<std::shared_ptr<Ledger const>> history;

        jtx::Env env(*this);
        Config config;
        auto prev = std::make_shared<Ledger const>(
            create_genesis, config, std::vector<uint256>{}, env.app().family());
        history.push_back(prev);
        for (auto i = 0; i < (2 * maxAncestors + 1); ++i)
        {
            auto next = std::make_shared<Ledger>(
                *prev, env.app().timeKeeper().closeTime());
            next->updateSkipList();
            history.push_back(next);
            prev = next;
        }

        // altHistory agrees with first half of regular history
        Seq const diverge = history.size() / 2;
        std::vector<std::shared_ptr<Ledger const>> altHistory(
            history.begin(), history.begin() + diverge);
        // advance clock to get new ledgers
        using namespace std::chrono_literals;
        env.timeKeeper().set(env.timeKeeper().now() + 1200s);
        prev = altHistory.back();
        bool forceHash = true;
        while (altHistory.size() < history.size())
        {
            auto next = std::make_shared<Ledger>(
                *prev, env.app().timeKeeper().closeTime());
            // Force a different hash on the first iteration
            next->updateSkipList();
            if (forceHash)
            {
                next->setImmutable(config);
                forceHash = false;
            }

            altHistory.push_back(next);
            prev = next;
        }

        //----------------------------------------------------------------------

        // Empty ledger
        {
            RCLValidatedLedger a{RCLValidatedLedger::MakeGenesis{}};
            BEAST_EXPECT(a.seq() == Seq{0});
            BEAST_EXPECT(a[Seq{0}] == ID{0});
            BEAST_EXPECT(a.minSeq() == Seq{0});
        }

        // Full history ledgers
        {
            std::shared_ptr<Ledger const> ledger = history.back();
            RCLValidatedLedger a{ledger, env.journal};
            BEAST_EXPECT(a.seq() == ledger->info().seq);
            BEAST_EXPECT(a.minSeq() == a.seq() - maxAncestors);
            // Ensure the ancestral 256 ledgers have proper ID
            for (Seq s = a.seq(); s > 0; s--)
            {
                if (s >= a.minSeq())
                    BEAST_EXPECT(a[s] == history[s - 1]->info().hash);
                else
                    BEAST_EXPECT(a[s] == ID{0});
            }
        }

        // Mismatch tests

        // Empty with non-empty
        {
            RCLValidatedLedger a{RCLValidatedLedger::MakeGenesis{}};

            for (auto ledger : {history.back(), history[maxAncestors - 1]})
            {
                RCLValidatedLedger b{ledger, env.journal};
                BEAST_EXPECT(mismatch(a, b) == 1);
                BEAST_EXPECT(mismatch(b, a) == 1);
            }
        }
        // Same chains, different seqs
        {
            RCLValidatedLedger a{history.back(), env.journal};
            for (Seq s = a.seq(); s > 0; s--)
            {
                RCLValidatedLedger b{history[s - 1], env.journal};
                if (s >= a.minSeq())
                {
                    BEAST_EXPECT(mismatch(a, b) == b.seq() + 1);
                    BEAST_EXPECT(mismatch(b, a) == b.seq() + 1);
                }
                else
                {
                    BEAST_EXPECT(mismatch(a, b) == Seq{1});
                    BEAST_EXPECT(mismatch(b, a) == Seq{1});
                }
            }
        }
        // Different chains, same seqs
        {
            // Alt history diverged at history.size()/2
            for (Seq s = 1; s < history.size(); ++s)
            {
                RCLValidatedLedger a{history[s - 1], env.journal};
                RCLValidatedLedger b{altHistory[s - 1], env.journal};

                BEAST_EXPECT(a.seq() == b.seq());
                if (s <= diverge)
                {
                    BEAST_EXPECT(a[a.seq()] == b[b.seq()]);
                    BEAST_EXPECT(mismatch(a, b) == a.seq() + 1);
                    BEAST_EXPECT(mismatch(b, a) == a.seq() + 1);
                }
                else
                {
                    BEAST_EXPECT(a[a.seq()] != b[b.seq()]);
                    BEAST_EXPECT(mismatch(a, b) == diverge + 1);
                    BEAST_EXPECT(mismatch(b, a) == diverge + 1);
                }
            }
        }
        // Different chains, different seqs
        {
            // Compare around the divergence point
            RCLValidatedLedger a{history[diverge], env.journal};
            for (Seq offset = diverge / 2; offset < 3 * diverge / 2; ++offset)
            {
                RCLValidatedLedger b{altHistory[offset - 1], env.journal};
                if (offset <= diverge)
                {
                    BEAST_EXPECT(mismatch(a, b) == b.seq() + 1);
                }
                else
                {
                    BEAST_EXPECT(mismatch(a, b) == diverge + 1);
                }
            }
        }
    }

    void
    testLedgerTrieRCLValidatedLedger()
    {
        testcase("RCLValidatedLedger LedgerTrie");

        // This test exposes an issue with the limited 256
        // ancestor hash design of RCLValidatedLedger.
        // There is only a single chain of validated ledgers
        // but the 256 gap causes a "split" in the LedgerTrie
        // due to the lack of ancestry information for a later ledger.
        // This exposes a bug in which we are unable to remove
        // support for a ledger hash which is already in the trie.

        using Seq = RCLValidatedLedger::Seq;
        using ID = RCLValidatedLedger::ID;

        // Max known ancestors for each ledger
        Seq const maxAncestors = 256;
        std::vector<std::shared_ptr<Ledger const>> history;

        // Generate a chain of 256 + 10 ledgers
        jtx::Env env(*this);
        auto& j = env.journal;
        Config config;
        auto prev = std::make_shared<Ledger const>(
            create_genesis, config, std::vector<uint256>{}, env.app().family());
        history.push_back(prev);
        for (auto i = 0; i < (maxAncestors + 10); ++i)
        {
            auto next = std::make_shared<Ledger>(
                *prev, env.app().timeKeeper().closeTime());
            next->updateSkipList();
            history.push_back(next);
            prev = next;
        }

        LedgerTrie<RCLValidatedLedger> trie;

        // First, create the single branch trie, with ledgers
        // separated by exactly 256 ledgers
        auto ledg_002 = RCLValidatedLedger{history[1], j};
        auto ledg_258 = RCLValidatedLedger{history[257], j};
        auto ledg_259 = RCLValidatedLedger{history[258], j};

        trie.insert(ledg_002);
        trie.insert(ledg_258, 4);
        // trie.dump(std::cout);
        // 000000[0,1)(T:0,B:5)
        //                     |-AB868A..36C8[1,3)(T:1,B:5)
        //                                                 |-AB868A..37C8[3,259)(T:4,B:4)
        BEAST_EXPECT(trie.tipSupport(ledg_002) == 1);
        BEAST_EXPECT(trie.branchSupport(ledg_002) == 5);
        BEAST_EXPECT(trie.tipSupport(ledg_258) == 4);
        BEAST_EXPECT(trie.branchSupport(ledg_258) == 4);

        // Move three of the s258 ledgers to s259, which splits the trie
        // due to the 256 ancestory limit
        BEAST_EXPECT(trie.remove(ledg_258, 3));
        trie.insert(ledg_259, 3);
        trie.getPreferred(1);
        // trie.dump(std::cout);
        // 000000[0,1)(T:0,B:5)
        //                     |-AB868A..37C9[1,260)(T:3,B:3)
        //                     |-AB868A..36C8[1,3)(T:1,B:2)
        //                                                 |-AB868A..37C8[3,259)(T:1,B:1)
        BEAST_EXPECT(trie.tipSupport(ledg_002) == 1);
        BEAST_EXPECT(trie.branchSupport(ledg_002) == 2);
        BEAST_EXPECT(trie.tipSupport(ledg_258) == 1);
        BEAST_EXPECT(trie.branchSupport(ledg_258) == 1);
        BEAST_EXPECT(trie.tipSupport(ledg_259) == 3);
        BEAST_EXPECT(trie.branchSupport(ledg_259) == 3);

        // The last call to trie.getPreferred cycled the children of the root
        // node to make the new branch the first child (since it has support 3)
        // then verify the remove call works
        // past bug: remove had assumed the first child of a node in the trie
        //      which matches is the *only* child in the trie which matches.
        //      This is **NOT** true with the limited 256 ledger ancestory
        //      quirk of RCLValidation and prevents deleting the old support
        //      for ledger 257

        BEAST_EXPECT(
            trie.remove(RCLValidatedLedger{history[257], env.journal}, 1));
        trie.insert(RCLValidatedLedger{history[258], env.journal}, 1);
        trie.getPreferred(1);
        // trie.dump(std::cout);
        // 000000[0,1)(T:0,B:5)
        //                      |-AB868A..37C9[1,260)(T:4,B:4)
        //                      |-AB868A..36C8[1,3)(T:1,B:1)
        BEAST_EXPECT(trie.tipSupport(ledg_002) == 1);
        BEAST_EXPECT(trie.branchSupport(ledg_002) == 1);
        BEAST_EXPECT(trie.tipSupport(ledg_258) == 0);
        // 258 no longer lives on a tip in the tree, BUT it is an ancestor
        // of 259 which is a tip and therefore gets it's branchSupport value
        // implicitly
        BEAST_EXPECT(trie.branchSupport(ledg_258) == 4);
        BEAST_EXPECT(trie.tipSupport(ledg_259) == 4);
        BEAST_EXPECT(trie.branchSupport(ledg_259) == 4);
    }

    void
    testLedgerSequence()
    {
        testcase("Validations with and without the LedgerSequence field");

        auto const nodeID =
            from_hex_text<NodeID>("38ECC15DBD999DE4CE70A6DC69A4166AB18031A7");

        try
        {
            std::string const withLedgerSequence =
                "228000000126034B9FFF2926460DC55185937F7F41DD7977F21B9DF95FCB61"
                "9E5132ABB0D7ADEA0F7CE8A9347871A34250179D85BDE824F57FFE0AC8F89B"
                "55FCB89277272A1D83D08ADEC98096A88EF723137321029D19FB0940E5C0D8"
                "5873FA711999944A687D129DA5C33E928C2751FC1B31EB3276463044022022"
                "6229CF66A678EE021F62CA229BA006B41939845004D3FAF8347C6FFBB7C613"
                "02200BE9CD3629FD67C6C672BD433A2769FCDB36B1ECA2292919C58A86224E"
                "2BF5970313C13F00C1FC4A53E60AB02C864641002B3172F38677E29C26C540"
                "6685179B37E1EDAC157D2D480E006395B76F948E3E07A45A05FE10230D88A7"
                "993C71F97AE4B1F2D11F4AFA8FA1BC8827AD4C0F682C03A8B671DCDF6B5C4D"
                "E36D44243A684103EF8825BA44241B3BD880770BFA4DA21C71805768318553"
                "68CBEC6A3154FDE4A7676E3012E8230864E95A58C60FD61430D7E1B4D33531"
                "95F2981DC12B0C7C0950FFAC30CD365592B8EE40489BA01AE2F7555CAC9C98"
                "3145871DC82A42A31CF5BAE7D986E83A7D2ECE3AD5FA87AB2195AE015C9504"
                "69ABF0B72EAACED318F74886AE9089308AF3B8B10B7192C4E613E1D2E4D9BA"
                "64B2EE2D5232402AE82A6A7220D953";

            if (auto ret = strUnHex(withLedgerSequence); ret)
            {
                SerialIter sit(makeSlice(*ret));

                auto val = std::make_shared<STValidation>(
                    std::ref(sit),
                    [nodeID](PublicKey const& pk) { return nodeID; },
                    false);

                BEAST_EXPECT(val);
                BEAST_EXPECT(calcNodeID(val->getSignerPublic()) == nodeID);
                BEAST_EXPECT(val->isFieldPresent(sfLedgerSequence));
            }
        }
        catch (std::exception const& ex)
        {
            fail(std::string("Unexpected exception thrown: ") + ex.what());
        }

        try
        {
            std::string const withoutLedgerSequence =
                "22800000012926460DC55185937F7F41DD7977F21B9DF95FCB619E5132ABB0"
                "D7ADEA0F7CE8A9347871A34250179D85BDE824F57FFE0AC8F89B55FCB89277"
                "272A1D83D08ADEC98096A88EF723137321029D19FB0940E5C0D85873FA7119"
                "99944A687D129DA5C33E928C2751FC1B31EB3276473045022100BE2EA49CF2"
                "FFB7FE7A03F6860B8C35FEA04A064C7023FE28EC97E5A32E85DEC4022003B8"
                "5D1D497F504B34F089D5BDB91BD888690C3D3A242A0FEF1DD52875FBA02E03"
                "13C13F00C1FC4A53E60AB02C864641002B3172F38677E29C26C5406685179B"
                "37E1EDAC157D2D480E006395B76F948E3E07A45A05FE10230D88A7993C71F9"
                "7AE4B1F2D11F4AFA8FA1BC8827AD4C0F682C03A8B671DCDF6B5C4DE36D4424"
                "3A684103EF8825BA44241B3BD880770BFA4DA21C7180576831855368CBEC6A"
                "3154FDE4A7676E3012E8230864E95A58C60FD61430D7E1B4D3353195F2981D"
                "C12B0C7C0950FFAC30CD365592B8EE40489BA01AE2F7555CAC9C983145871D"
                "C82A42A31CF5BAE7D986E83A7D2ECE3AD5FA87AB2195AE015C950469ABF0B7"
                "2EAACED318F74886AE9089308AF3B8B10B7192C4E613E1D2E4D9BA64B2EE2D"
                "5232402AE82A6A7220D953";

            if (auto ret = strUnHex(withoutLedgerSequence); ret)
            {
                SerialIter sit(makeSlice(*ret));

                auto val = std::make_shared<STValidation>(
                    std::ref(sit),
                    [nodeID](PublicKey const& pk) { return nodeID; },
                    false);

                fail("Expected exception not thrown from validation");
            }
        }
        catch (std::exception const& ex)
        {
            pass();
        }
    }

public:
    void
    run() override
    {
        testChangeTrusted();
        testRCLValidatedLedger();
        testLedgerTrieRCLValidatedLedger();
        testLedgerSequence();
    }
};

BEAST_DEFINE_TESTSUITE(RCLValidations, app, ripple);

}  // namespace test
}  // namespace ripple
