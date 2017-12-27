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

#include <BeastConfig.h>
#include <ripple/app/consensus/RCLValidations.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/View.h>
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class RCLValidations_test : public beast::unit_test::suite
{

public:
    void
    run() override
    {
        beast::Journal j;

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
            create_genesis, config,
            std::vector<uint256>{}, env.app().family());
        history.push_back(prev);
        for (auto i = 0; i < (2*maxAncestors + 1); ++i)
        {
            auto next = std::make_shared<Ledger>(
                *prev,
                env.app().timeKeeper().closeTime());
            next->updateSkipList();
            history.push_back(next);
            prev = next;
        }

        // altHistory agrees with first half of regular history
        Seq const diverge = history.size()/2;
        std::vector<std::shared_ptr<Ledger const>> altHistory(
            history.begin(), history.begin() + diverge);
        // advance clock too get new ledgers
        env.timeKeeper().set(env.timeKeeper().now() + 1200s);
        prev = altHistory.back();
        bool forceHash = true;
        while(altHistory.size() < history.size())
        {
            auto next = std::make_shared<Ledger>(
                *prev,
                env.app().timeKeeper().closeTime());
            // Force a different hash on the first iteration
            next->updateSkipList();
            if(forceHash)
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
            RCLValidatedLedger a{ledger, j};
            BEAST_EXPECT(a.seq() == ledger->info().seq);
            BEAST_EXPECT(
                a.minSeq() == a.seq() - maxAncestors);
            // Ensure the ancestral 256 ledgers have proper ID
            for(Seq s = a.seq(); s > 0; s--)
            {
                if(s >= a.minSeq())
                    BEAST_EXPECT(a[s] == history[s-1]->info().hash);
                else
                    BEAST_EXPECT(a[s] == ID{0});
            }
        }

        // Mismatch tests

        // Empty with non-empty
        {
            RCLValidatedLedger a{RCLValidatedLedger::MakeGenesis{}};

            for (auto ledger : {history.back(),
                                history[maxAncestors - 1]})
            {
                RCLValidatedLedger b{ledger, j};
                BEAST_EXPECT(mismatch(a, b) == 1);
                BEAST_EXPECT(mismatch(b, a) == 1);
            }
        }
        // Same chains, different seqs
        {
            RCLValidatedLedger a{history.back(), j};
            for(Seq s = a.seq(); s > 0; s--)
            {
                RCLValidatedLedger b{history[s-1], j};
                if(s >= a.minSeq())
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
            for(Seq s = 1; s < history.size(); ++s)
            {
                RCLValidatedLedger a{history[s-1], j};
                RCLValidatedLedger b{altHistory[s-1], j};

                BEAST_EXPECT(a.seq() == b.seq());
                if(s <= diverge)
                {
                    BEAST_EXPECT(a[a.seq()] == b[b.seq()]);
                    BEAST_EXPECT(mismatch(a,b) == a.seq() + 1);
                    BEAST_EXPECT(mismatch(b,a) == a.seq() + 1);
                }
                else
                {
                    BEAST_EXPECT(a[a.seq()] != b[b.seq()]);
                    BEAST_EXPECT(mismatch(a,b) == diverge + 1);
                    BEAST_EXPECT(mismatch(b,a) == diverge + 1);
                }
            }
        }
        // Different chains, different seqs
        {
            // Compare around the divergence point
            for(auto ledger : {history[diverge]})
            {
                RCLValidatedLedger a{ledger, j};
                for(Seq offset = diverge/2; offset < 3*diverge/2; ++offset)
                {
                    RCLValidatedLedger b{altHistory[offset-1], j};
                    if(offset <= diverge)
                    {
                        BEAST_EXPECT(mismatch(a,b) == b.seq() + 1);
                    }
                    else
                    {
                        BEAST_EXPECT(mismatch(a,b) == diverge + 1);
                    }
                }
            }
        }


    }
};  // namespace test

BEAST_DEFINE_TESTSUITE(RCLValidations, app, ripple);

}  // namespace test
}  // namespace ripple
