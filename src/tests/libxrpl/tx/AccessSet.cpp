// Tests for per-transactor static access-set extraction (Plan 1, Phase 1).
//
// Two layers:
//   1. AccessSet semantics (conflictsWith / keys) — pure, no ledger.
//   2. accessSetOf(tx, view) content — the declared footprint of each migrated
//      transactor matches expectation, and un-migrated / dynamic ones report
//      touchesGlobal.
//
// The *subset* safety net (declared footprint ⊇ what apply actually touched) is
// enforced continuously: in DEBUG builds Transactor::operator() asserts it on
// every successful apply, so every env.submit() below — and every other test in
// the suite that exercises a migrated transactor — validates it for free.

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol_autogen/transactions/AccountSet.h>
#include <xrpl/protocol_autogen/transactions/DepositPreauth.h>
#include <xrpl/protocol_autogen/transactions/OfferCreate.h>
#include <xrpl/protocol_autogen/transactions/Payment.h>
#include <xrpl/protocol_autogen/transactions/SetRegularKey.h>
#include <xrpl/protocol_autogen/transactions/SignerListSet.h>
#include <xrpl/protocol_autogen/transactions/TicketCreate.h>
#include <xrpl/protocol_autogen/transactions/TrustSet.h>
#include <xrpl/tx/AccessSet.h>
#include <xrpl/tx/applySteps.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <helpers/IOU.h>
#include <helpers/TxTest.h>

#include <cstdint>
#include <set>

namespace xrpl::test {

namespace {

// Build a signed STTx from a builder with an explicit sequence (the signature
// is irrelevant to accessSetOf; it never checks it).
template <class Builder>
std::shared_ptr<STTx const>
sttxOf(Builder builder, Account const& signer, std::uint32_t seq)
{
    return builder.setSequence(seq).setFee(XRPAmount{10}).build(signer.pk(), signer.sk()).getSTTx();
}

std::set<uint256>
keysOf(std::initializer_list<Keylet> ks)
{
    std::set<uint256> out;
    for (auto const& k : ks)
        out.insert(k.key);
    return out;
}

}  // namespace

//------------------------------------------------------------------------------
// 1. AccessSet semantics
//------------------------------------------------------------------------------

TEST(AccessSet, ConflictDisjoint)
{
    Account const a("a");
    Account const b("b");

    AccessSet s1;
    s1.accounts.insert(keylet::account(a.id()).key);
    AccessSet s2;
    s2.accounts.insert(keylet::account(b.id()).key);

    EXPECT_FALSE(s1.conflictsWith(s2));
    EXPECT_FALSE(s2.conflictsWith(s1));
}

TEST(AccessSet, ConflictSharedAccount)
{
    Account const a("a");
    Account const b("b");

    AccessSet s1;
    s1.accounts.insert(keylet::account(a.id()).key);
    s1.accounts.insert(keylet::account(b.id()).key);

    AccessSet s2;  // shares account a
    s2.accounts.insert(keylet::account(a.id()).key);

    EXPECT_TRUE(s1.conflictsWith(s2));
    EXPECT_TRUE(s2.conflictsWith(s1));
}

TEST(AccessSet, ConflictAcrossCategories)
{
    // The same key appearing under different categories still conflicts:
    // conflict is decided on the flat union, not per-category.
    Account const a("a");

    AccessSet s1;
    s1.accounts.insert(keylet::account(a.id()).key);
    AccessSet s2;
    s2.miscObjects.insert(keylet::account(a.id()).key);

    EXPECT_TRUE(s1.conflictsWith(s2));
}

TEST(AccessSet, GlobalConflictsWithEverything)
{
    Account const a("a");
    AccessSet local;
    local.accounts.insert(keylet::account(a.id()).key);

    AccessSet const g = AccessSet::global();
    EXPECT_TRUE(g.touchesGlobal);
    EXPECT_TRUE(g.conflictsWith(local));
    EXPECT_TRUE(local.conflictsWith(g));
    EXPECT_TRUE(g.conflictsWith(AccessSet{}));  // even with an empty set
}

TEST(AccessSet, KeysIsUnionAcrossCategories)
{
    Account const a("a");
    Account const b("b");

    AccessSet s;
    s.accounts.insert(keylet::account(a.id()).key);
    s.trustlines.insert(keylet::account(b.id()).key);  // any key; category is cosmetic
    s.accounts.insert(keylet::account(a.id()).key);     // duplicate

    EXPECT_EQ(s.keys().size(), 2u);
}

//------------------------------------------------------------------------------
// 2. accessSetOf content
//------------------------------------------------------------------------------

TEST(AccessSet, AccountSetTouchesOnlyActor)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::AccountSetBuilder{alice}, alice, env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(acc.keys(), keysOf({keylet::account(alice.id())}));
}

TEST(AccessSet, SetRegularKeyTouchesOnlyActor)
{
    TxTest env;
    Account const alice("alice");
    Account const reg("reg");
    env.createAccount(alice, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::SetRegularKeyBuilder{alice}.setRegularKey(reg.id()),
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(acc.keys(), keysOf({keylet::account(alice.id())}));
}

TEST(AccessSet, SignerListSetTouchesActorAndSignerList)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    env.createAccount(alice, XRP(10000));
    env.close();

    STArray signerEntries(1);
    signerEntries.push_back(STObject::makeInnerObject(sfSignerEntry));
    signerEntries.back()[sfAccount] = bob.id();
    signerEntries.back()[sfSignerWeight] = std::uint16_t{1};

    auto const stx = sttxOf(
        transactions::SignerListSetBuilder{alice, 1}.setSignerEntries(signerEntries),
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(acc.keys(), keysOf({keylet::account(alice.id()), keylet::signers(alice.id())}));
}

TEST(AccessSet, DepositPreauthTouchesActorAndPreauthObject)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    env.createAccount(alice, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::DepositPreauthBuilder{alice}.setAuthorize(bob.id()),
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(
        acc.keys(),
        keysOf({keylet::account(alice.id()), keylet::depositPreauth(alice.id(), bob.id())}));
}

TEST(AccessSet, TrustSetTouchesBothEndpointsAndLine)
{
    TxTest env;
    Account const alice("alice");
    Account const gw("gateway");
    IOU const usd("USD", gw);
    env.createAccount(alice, XRP(10000));
    env.createAccount(gw, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::TrustSetBuilder{alice}.setLimitAmount(usd.amount(10)),
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(
        acc.keys(),
        keysOf(
            {keylet::account(alice.id()),
             keylet::account(gw.id()),
             keylet::line(alice.id(), gw.id(), usd.issue().currency)}));
}

TEST(AccessSet, PaymentXrpDirectTouchesSrcDstAndPreauth)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    env.createAccount(alice, XRP(10000));
    env.createAccount(bob, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::PaymentBuilder{alice, bob, XRP(1)},
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    EXPECT_EQ(
        acc.keys(),
        keysOf(
            {keylet::account(alice.id()),
             keylet::account(bob.id()),
             keylet::depositPreauth(bob.id(), alice.id())}));
}

TEST(AccessSet, TicketCreateDeclaresSequenceRange)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10000));
    env.close();

    std::uint32_t const seq = env.getAccountRoot(alice.id()).getSequence();
    std::uint32_t const count = 3;

    auto const stx =
        sttxOf(transactions::TicketCreateBuilder{alice, count}, alice, seq);
    auto const acc = xrpl::accessSetOf(*stx, env.getClosedLedger());

    EXPECT_FALSE(acc.touchesGlobal);
    // Inclusive range [seq, seq + count] covers both sequence- and ticket-based
    // apply (the machinery may advance the sequence by one before creating).
    std::set<uint256> expected{keylet::account(alice.id()).key};
    for (std::uint32_t i = 0; i <= count; ++i)
        expected.insert(keylet::kTicket(alice.id(), seq + i).key);
    EXPECT_EQ(acc.keys(), expected);
}

//------------------------------------------------------------------------------
// 3. Fail-safe: dynamic-footprint transactions report touchesGlobal
//------------------------------------------------------------------------------

TEST(AccessSet, PaymentWithSendMaxIsGlobal)
{
    // Even an all-XRP payment with SendMax routes through the flow engine.
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    env.createAccount(alice, XRP(10000));
    env.createAccount(bob, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::PaymentBuilder{alice, bob, XRP(1)}.setSendMax(XRP(2)),
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    EXPECT_TRUE(xrpl::accessSetOf(*stx, env.getClosedLedger()).touchesGlobal);
}

TEST(AccessSet, IouPaymentIsGlobal)
{
    TxTest env;
    Account const gw("gateway");
    Account const alice("alice");
    IOU const usd("USD", gw);
    env.createAccount(gw, XRP(10000), asfDefaultRipple);
    env.createAccount(alice, XRP(10000), asfDefaultRipple);
    env.close();

    auto const stx = sttxOf(
        transactions::PaymentBuilder{gw, alice, usd.amount(5)},
        gw,
        env.getAccountRoot(gw.id()).getSequence());
    EXPECT_TRUE(xrpl::accessSetOf(*stx, env.getClosedLedger()).touchesGlobal);
}

TEST(AccessSet, UnmigratedTransactorIsGlobal)
{
    // OfferCreate is intentionally not migrated (offer crossing has a
    // state-dependent footprint); it must fall back to the global default.
    TxTest env;
    Account const alice("alice");
    Account const gw("gateway");
    IOU const usd("USD", gw);
    env.createAccount(alice, XRP(10000));
    env.close();

    auto const stx = sttxOf(
        transactions::OfferCreateBuilder{alice, usd.amount(10), XRP(10)},
        alice,
        env.getAccountRoot(alice.id()).getSequence());
    EXPECT_TRUE(xrpl::accessSetOf(*stx, env.getClosedLedger()).touchesGlobal);
}

//------------------------------------------------------------------------------
// 4. Subset safety net (explicit). In DEBUG these apply paths assert the
//    declared footprint ⊇ the touched footprint; tesSUCCESS means it held.
//------------------------------------------------------------------------------

TEST(AccessSet, SubsetPaymentToNewAccount)
{
    // Exercises the absence-probe path: the brand-new destination is read
    // (returns nullptr) and then inserted; both must fall within the declared
    // {src, dst, depositPreauth(dst, src)}.
    TxTest env;
    Account const alice("alice");
    Account const carol("carol");  // never created
    env.createAccount(alice, XRP(10000));
    env.close();

    EXPECT_EQ(
        env.submit(transactions::PaymentBuilder{alice, carol, XRP(100)}, alice).ter, tesSUCCESS);
}

TEST(AccessSet, SubsetTicketCreateThenUse)
{
    TxTest env;
    Account const alice("alice");
    env.createAccount(alice, XRP(10000));
    env.close();

    std::uint32_t const seq = env.getAccountRoot(alice.id()).getSequence();
    EXPECT_EQ(env.submit(transactions::TicketCreateBuilder{alice, 1}, alice).ter, tesSUCCESS);
    env.close();

    // Use the ticket (a ticket-based AccountSet) — exercises common-footprint
    // ticket handling on the consume side.
    std::uint32_t const ticketSeq = seq + 1;
    EXPECT_EQ(
        env.submit(transactions::AccountSetBuilder{alice}.setTicketSequence(ticketSeq), alice).ter,
        tesSUCCESS);
}

TEST(AccessSet, SubsetTrustSetAndSignerList)
{
    TxTest env;
    Account const alice("alice");
    Account const bob("bob");
    Account const gw("gateway");
    IOU const usd("USD", gw);
    env.createAccount(alice, XRP(10000), asfDefaultRipple);
    env.createAccount(gw, XRP(10000), asfDefaultRipple);
    env.close();

    EXPECT_EQ(
        env.submit(transactions::TrustSetBuilder{alice}.setLimitAmount(usd.amount(10)), alice).ter,
        tesSUCCESS);
    env.close();

    STArray signerEntries(1);
    signerEntries.push_back(STObject::makeInnerObject(sfSignerEntry));
    signerEntries.back()[sfAccount] = bob.id();
    signerEntries.back()[sfSignerWeight] = std::uint16_t{1};

    EXPECT_EQ(
        env.submit(
               transactions::SignerListSetBuilder{alice, 1}.setSignerEntries(signerEntries), alice)
            .ter,
        tesSUCCESS);
    env.close();

    // Remove the signer list (destroy path).
    EXPECT_EQ(env.submit(transactions::SignerListSetBuilder{alice, 0}, alice).ter, tesSUCCESS);
}

}  // namespace xrpl::test
