#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>

// Tests for the DustSplit credit-path primitive introduced in this PR:
// nullptr-policy semantics (plain payments never touch sfDust, and
// pre-existing sfDust survives a plain accountSend), and the
// removeEmptyHolding guard that refuses to delete a line while its
// sfDust reservoir is non-zero.
//
// No vault_dust:: consumer is exercised here — that lands in a follow-up
// PR and gets its own end-to-end suite.

namespace xrpl::test {

class DustSplitCreditPath_test : public beast::unit_test::suite
{
    FeatureBitset const all_{jtx::testableAmendments()};

    void
    testNullptrPathUnchanged()
    {
        testcase("Ordinary payments never acquire sfDust (nullptr policy)");

        using namespace jtx;
        Env env{*this, all_};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10'000), alice, bob, carol);
        env.close();
        PrettyAsset const asset = alice["USD"];
        env(trust(bob, asset(10'000)));
        env(trust(carol, asset(10'000)));
        env.close();
        env(pay(alice, bob, asset(1'000)));
        env.close();
        env(pay(bob, carol, asset(Number{1, -7})));
        env.close();

        // carol trusts the ISSUER (alice), not bob directly, so bob's
        // payment to carol ripples through alice — the two lines actually
        // touched are (alice,bob) and (alice,carol), not (bob,carol).
        auto const lineAB =
            env.le(keylet::trustLine(alice.id(), bob.id(), asset.raw().get<Issue>().currency));
        auto const lineAC =
            env.le(keylet::trustLine(alice.id(), carol.id(), asset.raw().get<Issue>().currency));
        if (BEAST_EXPECT(lineAB))
        {
            BEAST_EXPECT(
                !lineAB->isFieldPresent(sfDust) || Number{lineAB->at(sfDust)} == beast::kZero);
        }
        if (BEAST_EXPECT(lineAC))
        {
            BEAST_EXPECT(
                !lineAC->isFieldPresent(sfDust) || Number{lineAC->at(sfDust)} == beast::kZero);
        }
    }

    // A plain (dust-unaware) payment routed through a trust line that
    // already carries sfDust must not alter that reservoir.
    // testNullptrPathUnchanged pins the "no dust is ever CREATED" half
    // of the contract; this test pins the complementary "existing dust
    // is PRESERVED" half. The failure mode we are protecting against is
    // a nullptr-policy caller inadvertently reading or clobbering
    // sfDust when routing across a line seeded by a prior dust-aware
    // operation.
    void
    testNullptrPathPreservesExistingDust()
    {
        testcase("Plain (nullptr-policy) payment preserves pre-existing sfDust on the line");

        using namespace jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};

        Account const issuer{"issuer_p"};
        Account const alice{"alice_p"};
        Account const bob{"bob_p"};
        env.fund(XRP(10'000), issuer, alice, bob);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(alice, asset(10'000)));
        env(trust(bob, asset(10'000)));
        env.close();
        env(pay(issuer, alice, asset(1'000)));
        env(pay(issuer, bob, asset(1'000)));
        env.close();

        Issue const iouIssue = asset.raw().get<Issue>();
        Number const seededDust{7, -12};

        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            Sandbox sb(&view, TapNone);

            // Seed sfDust on Alice's line. Sign convention flips based on
            // whether alice is low or high account; we set the field in
            // its raw ledger form (low-account positive) so the direction
            // does not matter for the "unchanged after payment" check.
            auto const aliceLine = sb.peek(keylet::trustLine(alice.id(), iouIssue));
            if (!BEAST_EXPECT(aliceLine))
                return false;
            aliceLine->at(sfDust) = seededDust;
            sb.update(aliceLine);

            // Plain (nullptr-policy) accountSend across the alice-issuer
            // line (redeem back to issuer). This is the exact hot path a
            // Payment transactor would take.
            auto const r = accountSend(sb, alice.id(), issuer.id(), asset(10), j);
            BEAST_EXPECT(isTesSuccess(r));

            // Re-read the line and assert sfDust is byte-for-byte the
            // same value we seeded — the nullptr-policy path must be
            // dust-agnostic.
            auto const aliceLineAfter = sb.peek(keylet::trustLine(alice.id(), iouIssue));
            if (!BEAST_EXPECT(aliceLineAfter))
                return false;
            BEAST_EXPECT(Number{aliceLineAfter->at(sfDust)} == seededDust);

            // Bob's line was never touched by any dust-aware code, so
            // sfDust must be absent or zero (SoeDefault(0) semantics).
            auto const bobLine = sb.peek(keylet::trustLine(bob.id(), iouIssue));
            if (BEAST_EXPECT(bobLine))
                BEAST_EXPECT(Number{bobLine->at(sfDust)} == beast::kZero);
            return false;  // do not persist the sandboxed mutations
        });
    }

    // Verify the removeEmptyHolding guard by exercising it directly on a
    // synthetically dust-carrying custody line. Under the natural vault
    // flows, sfDust is always Drained before delete; getting a line
    // into (sfBalance=0, sfDust>0, everything-else-clean) state end-to-
    // end is not reachable. This test surgically installs a non-zero
    // sfDust on the vault custody line via a Sandbox, calls the helper,
    // and asserts tecHAS_OBLIGATIONS. Then zeroes sfDust and asserts
    // deletion succeeds. Together this pins both directions of the
    // guard in src/libxrpl/ledger/helpers/RippleStateHelpers.cpp.
    void
    testRemoveEmptyHoldingBlockedByDust()
    {
        testcase("removeEmptyHolding returns tecHAS_OBLIGATIONS when sfDust is non-zero");

        using namespace jtx;
        Env env{*this, all_ | featureLendingProtocolV1_1};

        Account const issuer{"issuer_reh"};
        Account const lender{"lender_reh"};
        env.fund(XRP(1'000'000), issuer, lender);
        env.close();
        PrettyAsset const asset = issuer["USD"];
        env(trust(lender, asset(500'000)));
        env.close();
        env(pay(issuer, lender, asset(400'000)));
        env.close();

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        AccountID const vaultAccount = vaultSle->at(sfAccount);
        Issue const iouIssue = asset.raw().get<Issue>();

        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            // Sub-case A: line with sfDust != 0 must NOT be deleted.
            {
                Sandbox sb(&view, TapNone);
                auto const line = sb.peek(keylet::trustLine(vaultAccount, iouIssue));
                if (!BEAST_EXPECT(line))
                    return false;
                // Vault side of the line is at index low or high depending
                // on address ordering. Set sfDust to a small non-zero
                // value; sign convention does not matter for the guard
                // (only zero-vs-non-zero).
                line->at(sfDust) = Number{1, -12};
                sb.update(line);

                auto const dummyTx = *env.jt(noop(lender)).stx;
                BEAST_EXPECT(
                    removeEmptyHolding({sb, dummyTx}, vaultAccount, iouIssue, j) ==
                    tecHAS_OBLIGATIONS);
                // Guard must not have deleted the line either.
                BEAST_EXPECT(sb.peek(keylet::trustLine(vaultAccount, iouIssue)) != nullptr);
            }

            // Sub-case B: same line with sfDust == 0 IS deleted (guard
            // is the only thing blocking it in this setup).
            {
                Sandbox sb(&view, TapNone);
                auto const line = sb.peek(keylet::trustLine(vaultAccount, iouIssue));
                if (!BEAST_EXPECT(line))
                    return false;
                line->at(sfDust) = Number{0};
                sb.update(line);

                auto const dummyTx = *env.jt(noop(lender)).stx;
                BEAST_EXPECT(
                    removeEmptyHolding({sb, dummyTx}, vaultAccount, iouIssue, j) == tesSUCCESS);
                BEAST_EXPECT(sb.peek(keylet::trustLine(vaultAccount, iouIssue)) == nullptr);
            }

            return false;  // do not persist the sandboxed mutations
        });
    }

public:
    void
    run() override
    {
        testNullptrPathUnchanged();
        testNullptrPathPreservesExistingDust();
        testRemoveEmptyHoldingBlockedByDust();
    }
};

BEAST_DEFINE_TESTSUITE(DustSplitCreditPath, app, xrpl);

}  // namespace xrpl::test
