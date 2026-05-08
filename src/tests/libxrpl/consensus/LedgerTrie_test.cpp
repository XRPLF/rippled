#include <xrpld/consensus/LedgerTrie.h>

#include <csf/ledgers.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <random>

namespace xrpl::test {

class LedgerTrieTest : public ::testing::Test
{
protected:
    static void
    testInsert()
    {
        using namespace csf;
        // Single entry by itself
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 1);

            t.insert(h["abc"]);
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
        }
        // Suffix of existing (extending tree)
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            EXPECT_TRUE(t.checkInvariants());
            // extend with no siblings
            t.insert(h["abcd"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 1);

            // extend with existing sibling
            t.insert(h["abce"]);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 3);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abce"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abce"]) == 1);
        }
        // uncommitted of existing node
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            EXPECT_TRUE(t.checkInvariants());
            // uncommitted with no siblings
            t.insert(h["abcdf"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcdf"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcdf"]) == 1);

            // uncommitted with existing child
            t.insert(h["abc"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 3);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcdf"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcdf"]) == 1);
        }
        // Suffix + uncommitted of existing node
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            EXPECT_TRUE(t.checkInvariants());
            t.insert(h["abce"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abce"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abce"]) == 1);
        }
        // Suffix + uncommitted with existing child
        {
            //  abcd : abcde, abcf

            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            EXPECT_TRUE(t.checkInvariants());
            t.insert(h["abcde"]);
            EXPECT_TRUE(t.checkInvariants());
            t.insert(h["abcf"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 3);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcf"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcf"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abcde"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcde"]) == 1);
        }

        // Multiple counts
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"], 4);
            EXPECT_TRUE(t.tipSupport(h["ab"]) == 4);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 4);
            EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["a"]) == 4);

            t.insert(h["abc"], 2);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["ab"]) == 4);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 6);
            EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["a"]) == 6);
        }
    }

    static void
    testRemove()
    {
        using namespace csf;
        // Not in trie
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);

            EXPECT_TRUE(!t.remove(h["ab"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(!t.remove(h["a"]));
            EXPECT_TRUE(t.checkInvariants());
        }
        // In trie but with 0 tip support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"]);
            t.insert(h["abce"]);

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
            EXPECT_TRUE(!t.remove(h["abc"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
        }
        // In trie with > 1 tip support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"], 2);

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.remove(h["abc"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);

            t.insert(h["abc"], 1);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.remove(h["abc"], 2));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);

            t.insert(h["abc"], 3);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 3);
            EXPECT_TRUE(t.remove(h["abc"], 300));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
        }
        // In trie with = 1 tip support, no children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);

            EXPECT_TRUE(t.tipSupport(h["ab"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 1);

            EXPECT_TRUE(t.remove(h["abc"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["ab"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 0);
        }
        // In trie with = 1 tip support, 1 child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abcd"]);

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 1);

            EXPECT_TRUE(t.remove(h["abc"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abcd"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abcd"]) == 1);
        }
        // In trie with = 1 tip support, > 1 children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"]);

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 3);

            EXPECT_TRUE(t.remove(h["abc"]));
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["abc"]) == 2);
        }

        // In trie with = 1 tip support, parent compaction
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["abc"]);
            t.insert(h["abd"]);
            EXPECT_TRUE(t.checkInvariants());
            t.remove(h["ab"]);
            EXPECT_TRUE(t.checkInvariants());
            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["abd"]) == 1);
            EXPECT_TRUE(t.tipSupport(h["ab"]) == 0);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 2);

            t.remove(h["abd"]);
            EXPECT_TRUE(t.checkInvariants());

            EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
            EXPECT_TRUE(t.branchSupport(h["ab"]) == 1);
        }
    }

    static void
    testEmpty()
    {
        using namespace csf;
        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;
        EXPECT_TRUE(t.empty());

        Ledger const genesis = h[""];
        t.insert(genesis);
        EXPECT_TRUE(!t.empty());
        t.remove(genesis);
        EXPECT_TRUE(t.empty());

        t.insert(h["abc"]);
        EXPECT_TRUE(!t.empty());
        t.remove(h["abc"]);
        EXPECT_TRUE(t.empty());
    }

    static void
    testSupport()
    {
        using namespace csf;

        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;
        EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["axy"]) == 0);

        EXPECT_TRUE(t.branchSupport(h["a"]) == 0);
        EXPECT_TRUE(t.branchSupport(h["axy"]) == 0);

        t.insert(h["abc"]);
        EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["ab"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
        EXPECT_TRUE(t.tipSupport(h["abcd"]) == 0);

        EXPECT_TRUE(t.branchSupport(h["a"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["ab"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["abc"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["abcd"]) == 0);

        t.insert(h["abe"]);
        EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["ab"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["abc"]) == 1);
        EXPECT_TRUE(t.tipSupport(h["abe"]) == 1);

        EXPECT_TRUE(t.branchSupport(h["a"]) == 2);
        EXPECT_TRUE(t.branchSupport(h["ab"]) == 2);
        EXPECT_TRUE(t.branchSupport(h["abc"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["abe"]) == 1);

        t.remove(h["abc"]);
        EXPECT_TRUE(t.tipSupport(h["a"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["ab"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["abc"]) == 0);
        EXPECT_TRUE(t.tipSupport(h["abe"]) == 1);

        EXPECT_TRUE(t.branchSupport(h["a"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["ab"]) == 1);
        EXPECT_TRUE(t.branchSupport(h["abc"]) == 0);
        EXPECT_TRUE(t.branchSupport(h["abe"]) == 1);
    }

    static void
    testGetPreferred()
    {
        using namespace csf;
        using Seq = Ledger::Seq;
        // Empty
        {
            LedgerTrie<Ledger> const t;
            EXPECT_TRUE(t.getPreferred(Seq{0}) == std::nullopt);
            EXPECT_TRUE(t.getPreferred(Seq{2}) == std::nullopt);
        }
        // Genesis support is NOT empty
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            Ledger const genesis = h[""];
            t.insert(genesis);

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{0})->id == genesis.id());
            EXPECT_TRUE(t.remove(genesis));
            EXPECT_TRUE(t.getPreferred(Seq{0}) == std::nullopt);
            EXPECT_TRUE(!t.remove(genesis));
        }
        // Single node no children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
        }
        // Single node smaller child support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());
        }
        // Single node larger child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"], 2);

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abcd"].id());

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abcd"].id());
        }
        // Single node smaller children support
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"]);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());

            t.insert(h["abc"]);

            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }
        // Single node larger children
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"], 2);
            t.insert(h["abce"]);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());

            t.insert(h["abcd"]);

            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abcd"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abcd"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }
        // Tie-breaker by id
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abcd"], 2);
            t.insert(h["abce"], 2);

            EXPECT_TRUE(h["abce"].id() > h["abcd"].id());

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abce"].id());

            t.insert(h["abcd"]);
            EXPECT_TRUE(h["abce"].id() > h["abcd"].id());

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abcd"].id());
        }

        // Tie-breaker not needed
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"]);
            t.insert(h["abce"], 2);
            // abce only has a margin of 1, but it owns the tie-breaker
            EXPECT_TRUE(h["abce"].id() > h["abcd"].id());

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abce"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abce"].id());

            // Switch support from abce to abcd, tie-breaker now needed
            t.remove(h["abce"]);
            t.insert(h["abcd"]);

            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }

        // Single node larger grand child
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcd"], 2);
            t.insert(h["abcde"], 4);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abcde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abcde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["abcde"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }

        // Too much uncommitted support from competing branches
        {
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["abc"]);
            t.insert(h["abcde"], 2);
            t.insert(h["abcfg"], 2);
            // 'de' and 'fg' are tied without 'abc' vote
            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abc"].id());
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["abc"].id());

            t.remove(h["abc"]);
            t.insert(h["abcd"]);

            // 'de' branch has 3 votes to 2, so earlier sequences see it as preferred
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abcde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["abcde"].id());

            // However, if you validated a ledger with Seq 5, potentially on
            // a different branch, you do not yet know if they chose abcd
            // or abcf because of you, so abc remains preferred
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["abc"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }

        // Changing largestSeq perspective changes preferred branch
        {
            /** Build the tree below with initial tip support annotated
                   A
                  / \
               B(1)  C(1)
              /  |   |
             H   D   F(1)
                 |
                 E(2)
                 |
                 G
            */
            LedgerTrie<Ledger> t;
            LedgerHistoryHelper h;
            t.insert(h["ab"]);
            t.insert(h["ac"]);
            t.insert(h["acf"]);
            t.insert(h["abde"], 2);

            // B has more branch support
            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{1})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{2})->id == h["ab"].id());

            // But if you last validated D,F or E, you do not yet know
            // if someone used that validation to commit to B or C
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["a"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["a"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)

            /** One of E advancing to G doesn't change anything
                   A
                  / \
               B(1)  C(1)
              /  |   |
             H   D   F(1)
                 |
                 E(1)
                 |
                 G(1)
            */
            t.remove(h["abde"]);
            t.insert(h["abdeg"]);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{1})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{2})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["a"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["a"].id());
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["a"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)

            /** C advancing to H does advance the seq 3 preferred ledger
                   A
                  / \
               B(1)  C
              /  |   |
             H(1)D   F(1)
                 |
                 E(1)
                 |
                 G(1)
            */
            t.remove(h["ac"]);
            t.insert(h["abh"]);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{1})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{2})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["a"].id());
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["a"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)

            /** F advancing to E also moves the preferred ledger forward
                   A
                  / \
               B(1)  C
              /  |   |
             H(1)D   F
                 |
                 E(2)
                 |
                 G(1)
            */
            t.remove(h["acf"]);
            t.insert(h["abde"]);

            // NOLINTBEGIN(bugprone-unchecked-optional-access)
            EXPECT_TRUE(t.getPreferred(Seq{1})->id == h["abde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{2})->id == h["abde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{3})->id == h["abde"].id());
            EXPECT_TRUE(t.getPreferred(Seq{4})->id == h["ab"].id());
            EXPECT_TRUE(t.getPreferred(Seq{5})->id == h["ab"].id());
            // NOLINTEND(bugprone-unchecked-optional-access)
        }
    }

    static void
    testRootRelated()
    {
        using namespace csf;
        // Since the root is a special node that breaks the no-single child
        // invariant, do some tests that exercise it.

        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;
        EXPECT_TRUE(!t.remove(h[""]));
        EXPECT_TRUE(t.branchSupport(h[""]) == 0);
        EXPECT_TRUE(t.tipSupport(h[""]) == 0);

        t.insert(h["a"]);
        EXPECT_TRUE(t.checkInvariants());
        EXPECT_TRUE(t.branchSupport(h[""]) == 1);
        EXPECT_TRUE(t.tipSupport(h[""]) == 0);

        t.insert(h["e"]);
        EXPECT_TRUE(t.checkInvariants());
        EXPECT_TRUE(t.branchSupport(h[""]) == 2);
        EXPECT_TRUE(t.tipSupport(h[""]) == 0);

        EXPECT_TRUE(t.remove(h["e"]));
        EXPECT_TRUE(t.checkInvariants());
        EXPECT_TRUE(t.branchSupport(h[""]) == 1);
        EXPECT_TRUE(t.tipSupport(h[""]) == 0);
    }

    static void
    testStress()
    {
        using namespace csf;
        LedgerTrie<Ledger> t;
        LedgerHistoryHelper h;

        // Test quasi-randomly add/remove supporting for different ledgers
        // from a branching history.

        // Ledgers have sequence 1,2,3,4
        std::uint32_t const depthConst = 4;
        // Each ledger has 4 possible children
        std::uint32_t const width = 4;

        std::uint32_t const iterations = 10000;

        // Use explicit seed to have same results for CI
        std::mt19937 gen{42};
        std::uniform_int_distribution<> depthDist(0, depthConst - 1);
        std::uniform_int_distribution<> widthDist(0, width - 1);
        std::uniform_int_distribution<> flip(0, 1);
        for (std::uint32_t i = 0; i < iterations; ++i)
        {
            // pick a random ledger history
            std::string curr;
            char const depth = depthDist(gen);
            char offset = 0;
            for (char d = 0; d < depth; ++d)
            {
                char const a = offset + widthDist(gen);
                curr += a;
                offset = (a + 1) * width;
            }

            // 50-50 to add remove
            if (flip(gen) == 0)
            {
                t.insert(h[curr]);
            }
            else
            {
                t.remove(h[curr]);
            }
            EXPECT_TRUE(t.checkInvariants());
            if (!(t.checkInvariants()))
                return;
        }
    }

    static void
    run()
    {
        testInsert();
        testRemove();
        testEmpty();
        testSupport();
        testGetPreferred();
        testRootRelated();
        testStress();
    }
};

TEST_F(LedgerTrieTest, ledger_trie)
{
    run();
}
}  // namespace xrpl::test
