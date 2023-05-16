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
            create_genesis,
            config,
            std::vector<uint256>{},
            env.app().getNodeFamily());
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
            BEAST_EXPECT(next->read(keylet::fees()));
            if (forceHash)
            {
                next->setImmutable();
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
            create_genesis,
            config,
            std::vector<uint256>{},
            env.app().getNodeFamily());
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

public:
    void
    run() override
    {
        testChangeTrusted();
        testRCLValidatedLedger();
        testLedgerTrieRCLValidatedLedger();
    }
};

BEAST_DEFINE_TESTSUITE(RCLValidations, app, ripple);

}  // namespace test
}  // namespace ripple
