#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/multisign.h>
#include <test/jtx/ticket.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/Feature.h>

#include "test/jtx/check.h"
#include "test/jtx/did.h"
#include "test/jtx/sponsor.h"

namespace xrpl {
namespace test {

static STAmount
accountReserve(jtx::Env& env, std::uint32_t count = 1)
{
    return env.current()->fees().reserve * count;
}

static STAmount
reserve(jtx::Env& env, std::uint32_t count)
{
    return env.current()->fees().accountReserve(count);
}

static void
adjustAccountXRPBalance(jtx::Env& env, jtx::Account const& account, STAmount const& balanceTo)
{
    using namespace test::jtx;
    XRPL_ASSERT(isXRP(balanceTo), "adjustAccountXRPBalance: balanceTo must be XRP");
    auto const currentBalance = env.balance(account);
    if (currentBalance == balanceTo)
        return;

    auto const baseFee = env.current()->fees().base;
    if (currentBalance > balanceTo)
        env(pay(account, env.master, currentBalance - (balanceTo)),
            fee(XRP(1)),
            sponsor::as(env.master, spfSponsorFee),
            sig(sfSponsorSignature, env.master));
    else
        env(pay(env.master, account, balanceTo - currentBalance), fee(baseFee));

    env.close();
}

class Sponsor_test : public beast::unit_test::suite
{
public:
    void
    testDisabled()
    {
        testcase("Disabled");
        using namespace test::jtx;
        Env env{*this, testable_amendments() - featureSponsor};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);

        // check Sponsor fields
        auto const jt = noop(alice);
        auto jt1 = jt;
        jt1[sfSponsor.jsonName] = sponsor.human();
        env(jt1, ter(temDISABLED));
        env(jt, sig(sfSponsorSignature, sponsor), ter(temDISABLED));

        auto jt2 = jt;
        jt2[sfSponsorFlags.jsonName] = spfSponsorFee | spfSponsorReserve;
        env(jt2, ter(temDISABLED));

        // check Sponsor transactions
        env(sponsor::transfer(alice, 0), ter(temDISABLED));
        env(sponsor::set(sponsor, 0), ter(temDISABLED));
    }

    void
    testInvalidSponsorshipSet()
    {
        testcase("Invalid SponsorshipSet");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const noFunded("noFunded");
        Account const gw("gw");

        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, sponsor, gw);
        env.close();

        //
        // preflight
        //

        // Invalid flags
        {
            env(sponsor::set(sponsor, ~tfSponsorshipSetMask - tfInnerBatchTxn),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee | tfSponsorshipClearRequireSignForFee),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForReserve |
                        tfSponsorshipClearRequireSignForReserve),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            for (auto flag :
                 {tfSponsorshipSetRequireSignForFee,
                  tfSponsorshipClearRequireSignForFee,
                  tfSponsorshipSetRequireSignForReserve,
                  tfSponsorshipClearRequireSignForReserve})
            {
                env(sponsor::set(sponsor, tfDeleteObject | flag),
                    sponsor::sponseeAcc(alice),
                    ter(temINVALID_FLAG));
            }
        }

        // invalid SponsorAccount / Sponsee
        // Account = Sponsor
        env(sponsor::set(alice, tfDeleteObject),
            sponsor::counterpartySponsor(alice),
            ter(temMALFORMED));
        // Account = Sponsee
        env(sponsor::set(alice, tfDeleteObject), sponsor::sponseeAcc(alice), ter(temMALFORMED));
        // Both Sponsor and Sponsee are specified
        env(sponsor::set(alice, 0),
            sponsor::counterpartySponsor(sponsor),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));

        // Invalid feeAmount
        for (auto const& amt : {XRP(-1), USD(1)})
        {
            env(sponsor::set_fee(sponsor, 0, amt), sponsor::sponseeAcc(alice), ter(temBAD_AMOUNT));
        }
        // Invalid MaxFee
        for (auto const& amt : {XRP(-1), USD(1)})
        {
            env(sponsor::set_fee(sponsor, 0, XRP(1), amt),
                sponsor::sponseeAcc(alice),
                ter(temBAD_AMOUNT));
        }

        // Invalid Delete operation
        env(sponsor::set_reserve(sponsor, tfDeleteObject, 1),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));
        env(sponsor::set_fee(sponsor, tfDeleteObject, XRP(1)),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));
        env(sponsor::set_max_fee(sponsor, tfDeleteObject, XRP(1)),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));

        // Invalid SponsorAccount with non-Delete operation
        env(sponsor::set_reserve(sponsor, 0, 100),
            sponsor::counterpartySponsor(alice),
            ter(temMALFORMED));
        env(sponsor::set_fee(sponsor, 0, XRP(1), XRP(1)),
            sponsor::counterpartySponsor(alice),
            ter(temMALFORMED));

        //
        // preclaim
        //

        // Invalid Sponsee
        env(sponsor::set(sponsor, 0), sponsor::sponseeAcc(noFunded), ter(tecNO_DST));
        env.close();

        // Invalid Sponsor
        env(sponsor::set(sponsor, tfDeleteObject),
            sponsor::counterpartySponsor(noFunded),
            ter(tecNO_DST));
        env.close();

        // Invalid Delete operation (sponsorship not found)
        env(sponsor::set(sponsor, tfDeleteObject), sponsor::sponseeAcc(alice), ter(tecNO_ENTRY));
        env.close();

        // insufficient balance to sponsor fee
        adjustAccountXRPBalance(env, sponsor, env.current()->fees().reserve);
        env(sponsor::set_fee(sponsor, 0, XRP(4)), sponsor::sponseeAcc(alice), ter(tecUNFUNDED));
        env.close();

        // insufficent reserve to create sponsorship
        adjustAccountXRPBalance(env, sponsor, XRP(100) + XRP(1) + reserve(env, 1) - drops(1));
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tecUNFUNDED));
        env.close();

        //  FeeAmount + Fee > Balance
        /// Balance = 1000XRP, FeeAmount = 1001XRP
        adjustAccountXRPBalance(env, sponsor, XRP(1000));
        env(sponsor::set_fee(sponsor, 0, XRP(1001)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tecUNFUNDED));
        env.close();
        /// Balance = 1000XRP, FeeAmount = 999XRP, Fee=2XRP
        adjustAccountXRPBalance(env, sponsor, XRP(1000));
        env(sponsor::set_fee(sponsor, 0, XRP(999)),
            sponsor::sponseeAcc(alice),
            fee(XRP(2)),
            ter(tecUNFUNDED));
        env.close();

        // create sponsor to use above tests
        // need feeAmount(1000) + fee(1) + reserve(~250) = ~1251
        adjustAccountXRPBalance(env, sponsor, XRP(1000) + XRP(1) + reserve(env, 1));
        env(sponsor::set(sponsor, 0, 100, XRP(1000)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tesSUCCESS));
        env.close();

        // delta-based balance check
        // After create: sponsor balance ~ 0, feeAmount = XRP(1000)

        // Decreasing feeAmount should succeed (refund, negative delta)
        adjustAccountXRPBalance(env, sponsor, XRP(500));
        env(sponsor::set_fee(sponsor, 0, XRP(800)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tesSUCCESS));
        env.close();
        // balance was 500, delta = 800-1000 = -200 (refund), balance = 500+200-1 = 699

        // Increasing feeAmount within delta budget should succeed
        adjustAccountXRPBalance(env, sponsor, XRP(500));
        env(sponsor::set_fee(sponsor, 0, XRP(850)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tesSUCCESS));
        env.close();
        // balance was 500, delta = 850-800 = 50, balance = 500-50-1 = 449

        // Increasing feeAmount where delta exceeds balance should fail
        adjustAccountXRPBalance(env, sponsor, XRP(310));
        env(sponsor::set_fee(sponsor, 0, XRP(1200)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tecUNFUNDED));
        env.close();

        // Increasing feeAmount to reach insufficient reserve
        auto const currentFeeAmount =
            env.le(keylet::sponsor(sponsor.id(), alice.id()))->getFieldAmount(sfFeeAmount).xrp();
        adjustAccountXRPBalance(env, sponsor, XRP(310));
        env(sponsor::set_fee(sponsor, 0, currentFeeAmount + XRP(309)),
            sponsor::sponseeAcc(alice),
            fee(XRP(1)),
            ter(tecUNFUNDED));
        env.close();
    }

    void
    testPseudoAccountSponsorship()
    {
        testcase("Pseudo account sponsorship");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sp("sponsor");

        Asset const asset = gw["IOU"].asset();

        env.fund(XRP(1000000), alice, bob, gw, sp);
        env.close();

        // Create a vault to get a pseudo account
        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
        env(tx);
        env.close();

        auto const vaultSle = env.le(keylet);
        BEAST_EXPECT(vaultSle);
        Account const pseudoAcc("vault", vaultSle->getAccountID(sfAccount));
        env.memoize(pseudoAcc);

        // Sponsee is a pseudo account -> tecNO_PERMISSION
        env(sponsor::set(sp, 0, 100, XRP(100)),
            sponsor::sponseeAcc(pseudoAcc),
            ter(tecNO_PERMISSION));
        env.close();

        // Sponsor is a pseudo account -> tecNO_PERMISSION
        // (submitted by bob with counterpartySponsor pointing to pseudo account)
        env(sponsor::set(bob, tfDeleteObject),
            sponsor::counterpartySponsor(pseudoAcc),
            ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testSingleSigning()
    {
        testcase("Single signing");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Signature doesn't exist
        auto tx = noop(alice);
        tx[sfSponsor.jsonName] = sponsor.human();
        tx[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = strHex(sponsor.pk().slice());

        env(tx, fee(XRP(1)), sponsor::as(sponsor, spfSponsorReserve), ter(telENV_RPC_FAILED));

        // Invalid signature
        tx[sfSponsorSignature.jsonName][sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx, fee(XRP(1)), sponsor::as(sponsor, spfSponsorReserve), ter(telENV_RPC_FAILED));

        // Signer account doesn't exist
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(invalid, spfSponsorReserve),
            sig(sfSponsorSignature, invalid),
            ter(terNO_ACCOUNT));

        // Success
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, spfSponsorReserve),
            sig(sfSponsorSignature, sponsor),
            ter(tesSUCCESS));
    }

    void
    testMultiSigning()
    {
        testcase("Multi signing");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        Account const signer1("signer1");
        Account const signer2("signer2");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        env(signers(sponsor, 1, {{signer1, 1}, {signer2, 1}}));
        env.close();

        // Invalid signature
        auto tx = noop(alice);
        auto& signers1 = tx[sfSponsorSignature.jsonName][sfSigners.jsonName][0U][sfSigner.jsonName];
        signers1[sfAccount.jsonName] = signer1.human();
        signers1[sfSigningPubKey.jsonName] = strHex(signer1.pk().slice());
        signers1[sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx, fee(XRP(1)), sponsor::as(sponsor, spfSponsorReserve), ter(telENV_RPC_FAILED));

        // Signer account doesn't exist
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(invalid, spfSponsorReserve),
            msig(sfSponsorSignature, {signer1}),
            ter(tefNOT_MULTI_SIGNING));

        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, spfSponsorReserve),
            msig(sfSponsorSignature, {signer1}),
            ter(tesSUCCESS));
        env.close();

        env(signers(sponsor, 2, {{signer1, 1}, {signer2, 1}}));
        env.close();

        // test calculateBaseFee for multisigned sponsor
        auto const baseFee = env.current()->fees().base;
        env(noop(alice),
            fee(baseFee + 2 * baseFee - 1),
            sponsor::as(sponsor, spfSponsorReserve),
            msig(sfSponsorSignature, {signer1, signer2}),
            ter(telINSUF_FEE_P));

        env(noop(alice),
            fee(baseFee + 2 * baseFee),
            sponsor::as(sponsor, spfSponsorReserve),
            msig(sfSponsorSignature, {signer1, signer2}),
            ter(tesSUCCESS));
    }

    void
    testInvalidSponsorField()
    {
        testcase("Invalid Sponsor Field");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const noFunded("noFunded");
        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Invalid Sponsor Account (Account = Sponsor.Account)
        env(noop(alice), sponsor::as(alice, spfSponsorFee), ter(temMALFORMED));

        // Invalid Sponsor Account
        // (SponsorSignature is specified but Sponsor.Account is not specified)
        env(noop(alice), sig(sfSponsorSignature, sponsor), ter(temMALFORMED));

        // Invalid Sponsor Account (Sponsor.Account doesn't exist)
        env(noop(alice), sponsor::as(noFunded, spfSponsorReserve), ter(terNO_SPONSORSHIP));
        env(noop(alice),
            sponsor::as(noFunded, spfSponsorReserve),
            sig(sfSponsorSignature, noFunded),
            ter(terNO_ACCOUNT));

        // Invalid Flags
        env(noop(alice),
            sponsor::as(sponsor, (spfSponsorFee | spfSponsorReserve) + 1),
            ter(temINVALID_FLAG));

        // SponsorFlags=0 with valid sponsor (no sponsorship purpose)
        env(noop(alice), sponsor::as(sponsor, 0), ter(temINVALID_FLAG));

        // no SponsorFlag with valid sponsor
        auto tx = noop(alice);
        tx[sfSponsor.jsonName] = sponsor.human();
        env(tx, ter(temINVALID_FLAG));

        // Invalid Flags without sponsor
        tx = noop(alice);
        tx[sfSponsorFlags.jsonName] = spfSponsorFee | spfSponsorReserve;
        env(tx, ter(temINVALID_FLAG));
    }

    void
    testSimpleSponsorshipSet()
    {
        testcase("Simple SponsorshipSet");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);
        env.close();

        {
            // create sponsorship
            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee | tfSponsorshipSetRequireSignForReserve,
                    100,
                    XRP(100),
                    XRP(1)),
                fee(XRP(1)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            auto sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 100);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(1));
            BEAST_EXPECT(sle->isFlag(lsfSponsorshipRequireSignForFee));
            BEAST_EXPECT(sle->isFlag(lsfSponsorshipRequireSignForReserve));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(1));

            // update sponsorship (decrement)
            env(sponsor::set(sponsor, 0, 50, XRP(50), XRP(0.5)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 50);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(50));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(0.5));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(2));

            // update sponsorship (increment)
            env(sponsor::set(sponsor, 0, 200, XRP(200), XRP(2)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 200);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(200));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(2));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(3));

            // delete from sponsor
            env(sponsor::del(sponsor), sponsor::sponseeAcc(alice), fee(XRP(1)), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - XRP(4));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee | tfSponsorshipSetRequireSignForReserve,
                    100,
                    XRP(100),
                    XRP(1)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            // delete from sponsee
            env(sponsor::del(alice), sponsor::counterpartySponsor(sponsor), ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(!env.le(keylet::sponsor(sponsor, alice)));

            // create sponsorship with zero value
            env(sponsor::set(sponsor, 0, 0, XRP(0), XRP(0)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(!sle->isFieldPresent(sfReserveCount));
            BEAST_EXPECT(!sle->isFieldPresent(sfFeeAmount));
            BEAST_EXPECT(!sle->isFieldPresent(sfMaxFee));
            // verify flags from previous sponsorship are not carried over
            BEAST_EXPECT(!sle->isFlag(lsfSponsorshipRequireSignForFee));
            BEAST_EXPECT(!sle->isFlag(lsfSponsorshipRequireSignForReserve));

            // update sponsorship with non-zero value
            env(sponsor::set(sponsor, 0, 100, XRP(100), XRP(1)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 100);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(1));

            // update sponsorship with zero value
            env(sponsor::set(sponsor, 0, 0, XRP(0), XRP(0)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(!sle->isFieldPresent(sfReserveCount));
            BEAST_EXPECT(!sle->isFieldPresent(sfFeeAmount));
            BEAST_EXPECT(!sle->isFieldPresent(sfMaxFee));
        }

        {
            // Update Sponsorship (FeeAmount)
            // set empty FeeAmount
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::sponseeAcc(alice), ter(tesSUCCESS));
            env.close();

            // add FeeAmount
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::counterpartySponsor(sponsor), ter(tesSUCCESS));
            env.close();
        }
        {
            // Update Sponsorship (ReserveCount)
            // set empty ReserveCount
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            // add ReserveCount
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::sponseeAcc(alice), ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::counterpartySponsor(sponsor), ter(tesSUCCESS));
            env.close();
        }
        {
            // delete Sponsorship (only with FeeAmount)
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::counterpartySponsor(sponsor), ter(tesSUCCESS));
            env.close();
        }
        {
            // delete Sponsorship (only with ReserveCount)
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::sponseeAcc(alice), ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::counterpartySponsor(sponsor), ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testPreFundAndCosign()
    {
        testcase("PreFund and Cosign");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");

        {
            // both pre-funded and co-signed,pre-funded value is used
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100), XRP(1)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            env(did::set(alice),
                did::uri("uri"),
                sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                sig(sfSponsorSignature, sponsor),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();

            auto sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 99);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(99));

            env(did::del(alice), ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfReserveCount) == 100);  // paybacked
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(99));
        }

        {
            // if pre-funded value is not enough, error
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 10, XRP(10), XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            // fee insufficient
            env(ticket::create(alice, 1),
                sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                sig(sfSponsorSignature, sponsor),
                fee(XRP(11)),
                ter(terINSUF_FEE_B));
            env.close();

            // reserve insufficient
            env(ticket::create(alice, 11),
                sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                sig(sfSponsorSignature, sponsor),
                fee(XRP(1)),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testTransferSponsor()
    {
        testcase("Transfer Sponsor");
        using namespace test::jtx;

        {
            // invalid fields
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);
            env.close();

            env(sponsor::transfer(
                    alice, (tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd) + 1),
                ter(temINVALID_FLAG));

            // invalid combination of flags
            for (auto flag : {
                     tfSponsorshipCreate | tfSponsorshipReassign,
                     tfSponsorshipCreate | tfSponsorshipEnd,
                     tfSponsorshipReassign | tfSponsorshipEnd,
                     tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd,
                 })
                env(sponsor::transfer(alice, flag), ter(temINVALID_FLAG));

            // invalid tfSponsorshipCreate
            // no sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipCreate), ter(temINVALID_FLAG));
            // sponsee field present
            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::sponseeAcc(bob),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(temMALFORMED));

            // invalid tfSponsorshipReassign
            // no sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipReassign), ter(temINVALID_FLAG));
            // sponsee field present
            env(sponsor::transfer(alice, tfSponsorshipReassign),
                sponsor::sponseeAcc(bob),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(temMALFORMED));

            // invalid tfSponsorshipEnd
            // sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipEnd),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(temINVALID_FLAG));
            // account = sponsee
            env(sponsor::transfer(alice, tfSponsorshipEnd),
                sponsor::sponseeAcc(alice),
                ter(temMALFORMED));
        }

        {
            // Invalid SponsorshipEnd permission (sponsor object/sponsor account)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            {
                // sponsor object
                env(did::set(alice),
                    did::uri("uri"),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                auto const keylet = keylet::did(alice);
                env(sponsor::transfer(bob, tfSponsorshipEnd, keylet.key),
                    sponsor::sponseeAcc(alice),
                    ter(tecNO_PERMISSION));
            }
            {
                // sponsor object
                env(sponsor::transfer(alice, tfSponsorshipCreate),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                env(sponsor::transfer(bob, tfSponsorshipEnd),
                    sponsor::sponseeAcc(alice),
                    ter(tecNO_PERMISSION));
            }
        }

        {
            // sponsor account
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);

            // sfSponsor provided but sfSponsorSignature not provided
            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(temMALFORMED));
            env.close();

            adjustAccountXRPBalance(env, sponsor1, accountReserve(env, 2) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor1, accountReserve(env, 2));

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 1);
            auto const sle1 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sle1->getAccountID(sfSponsor) == sponsor1.id());

            // transfer sponsor
            adjustAccountXRPBalance(env, sponsor2, accountReserve(env, 2) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipReassign),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor2, accountReserve(env, 2));

            env(sponsor::transfer(alice, tfSponsorshipReassign),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 1);
            BEAST_EXPECT(
                !env.le(keylet::account(sponsor1))->isFieldPresent(sfSponsoringAccountCount));
            auto const sle2 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sle2->getAccountID(sfSponsor) == sponsor2.id());

            // sponsor 2 accounts
            adjustAccountXRPBalance(env, sponsor2, accountReserve(env, 3));
            env(sponsor::transfer(bob, tfSponsorshipCreate),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2));
            env.close();

            // dissolve sponsors
            adjustAccountXRPBalance(env, alice, accountReserve(env, 1) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipEnd), ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, alice, accountReserve(env, 1));

            env(sponsor::transfer(alice, tfSponsorshipEnd));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 1);
            auto const sle3 = env.le(keylet::account(alice));
            BEAST_EXPECT(!sle3->isFieldPresent(sfSponsor));

            env(sponsor::transfer(bob, tfSponsorshipEnd));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            BEAST_EXPECT(
                !env.le(keylet::account(sponsor2))->isFieldPresent(sfSponsoringAccountCount));
            auto const sle4 = env.le(keylet::account(bob));
            BEAST_EXPECT(!sle4->isFieldPresent(sfSponsor));

            // not sponsored
            env(sponsor::transfer(bob, tfSponsorshipEnd), ter(tecNO_PERMISSION));
            env.close();
        }
        {
            // dissolve account sponsorship from sponsor
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(env.le(alice)->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 1);

            env(sponsor::transfer(sponsor, tfSponsorshipEnd), sponsor::sponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 0);
        }

        {
            // sponsor object (co-signing)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);
            env.close();

            adjustAccountXRPBalance(env, sponsor1, reserve(env, 1) - drops(1));
            adjustAccountXRPBalance(env, sponsor2, reserve(env, 1) - drops(1));

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(pay(alice, sponsor1, drops(1)));
            env.close();

            // Invalid ObjectID (not found)
            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet::check(alice, 0).key),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1),
                ter(tecNO_ENTRY));
            env.close();

            // Invalid Owner
            env(sponsor::transfer(bob, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1),
                ter(tecNO_PERMISSION));
            env.close();

            // Valid Owner
            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor1));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            auto const sle1 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sle1->getAccountID(sfSponsor) == sponsor1.id());

            // transfer sponsor
            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2),
                ter(tecINSUFFICIENT_RESERVE));

            env(pay(alice, sponsor2, drops(1)));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            auto const sle2 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sle2->getAccountID(sfSponsor) == sponsor2.id());

            // dissolve sponsor
            adjustAccountXRPBalance(env, alice, reserve(env, 1) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipEnd, checkId), ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, alice, reserve(env, 1));

            // object doesn't sponsored
            auto const ticketSeq = env.seq(alice);
            env(ticket::create(alice, 1));
            env.close();
            auto ticketId = keylet::ticket(alice, ticketSeq + 1).key;
            BEAST_EXPECT(env.le(keylet::unchecked(ticketId)));
            env(sponsor::transfer(alice, tfSponsorshipEnd, ticketId), ter(tecNO_PERMISSION));
            env.close();
            env(noop(alice), ticket::use(ticketSeq + 1));
            env.close();

            adjustAccountXRPBalance(env, alice, reserve(env, 1));

            env(sponsor::transfer(alice, tfSponsorshipEnd, checkId));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            BEAST_EXPECT(
                !env.le(keylet::account(sponsor2))->isFieldPresent(sfSponsoringOwnerCount));
            auto const sle3 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(!sle3->isFieldPresent(sfSponsor));
        }
        {
            // sponsor object (pre-funded + no ltSponsorship entry)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(terNO_SPONSORSHIP));
            env.close();

            env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor2, spfSponsorReserve));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(terNO_SPONSORSHIP));
            env.close();
        }
        {
            // sponsor object (pre-funded)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            // insufficient reserve count
            env(sponsor::set_fee(sponsor1, 0, XRP(100)), sponsor::sponseeAcc(alice));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(sponsor::set_reserve(sponsor1, 0, 100), sponsor::sponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor1, spfSponsorReserve));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            auto checkSle = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(checkSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(checkSle->getAccountID(sfSponsor) == sponsor1.id());
            auto sponsor1Sle = env.le(keylet::sponsor(sponsor1, alice));
            BEAST_EXPECT(sponsor1Sle->getFieldU32(sfReserveCount) == 99);

            // transfer sponsor
            env(sponsor::set_reserve(sponsor2, 0, 100), sponsor::sponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::as(sponsor2, spfSponsorReserve));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            checkSle = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(checkSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(checkSle->getAccountID(sfSponsor) == sponsor2.id());
            sponsor1Sle = env.le(keylet::sponsor(sponsor1, alice));
            BEAST_EXPECT(sponsor1Sle->getFieldU32(sfReserveCount) == 100);  // paybacked
            auto sponsor2Sle = env.le(keylet::sponsor(sponsor2, alice));
            BEAST_EXPECT(sponsor2Sle->getFieldU32(sfReserveCount) == 99);

            // dissolve sponsor
            adjustAccountXRPBalance(env, alice, reserve(env, 1));
            env(sponsor::transfer(alice, tfSponsorshipEnd, checkId));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            BEAST_EXPECT(
                !env.le(keylet::account(sponsor2))->isFieldPresent(sfSponsoringOwnerCount));
            checkSle = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(!checkSle->isFieldPresent(sfSponsor));
            sponsor2Sle = env.le(keylet::sponsor(sponsor2, alice));
            BEAST_EXPECT(sponsor2Sle->getFieldU32(sfReserveCount) == 100);  // paybacked
        }

        {
            // Dissolve object sponsorship from sponsor(no-ltSponsorship)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::unchecked(checkId))->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // not the owner of the object
            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), ter(tecNO_PERMISSION));
            env.close();

            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), sponsor::sponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(keylet::unchecked(checkId))->isFieldPresent(sfSponsor));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // Dissolve object sponsorship from sponsor (with ltSponsorship)
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::sponseeAcc(alice));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::unchecked(checkId))->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(
                env.le(keylet::sponsor(sponsor, alice))->getFieldU32(sfReserveCount) == 100);

            // not the owner of the object
            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), ter(tecNO_PERMISSION));
            env.close();

            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), sponsor::sponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(keylet::unchecked(checkId))->isFieldPresent(sfSponsor));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(
                env.le(keylet::sponsor(sponsor, alice))->getFieldU32(sfReserveCount) == 101);
        }

        {
            // sponsor trustline
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");

            auto const& highAcc = alice > bob ? alice : bob;
            auto const& lowAcc = alice > bob ? bob : alice;

            for (bool const isIssuerHigh : {false, true})
            {
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, sponsor);
                env.close();

                auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
                auto const& user = isIssuerHigh ? lowAcc : highAcc;

                auto const USD = issuer["USD"];
                auto const currency = USD.currency;

                env(trust(user, issuer["USD"](100)));
                env.close();

                auto const trustId = keylet::line(user, issuer, currency);
                BEAST_EXPECT(env.le(trustId));

                // transfer sponsor
                env(sponsor::transfer(user, tfSponsorshipCreate, trustId.key),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                BEAST_EXPECT(env.le(trustId));

                BEAST_EXPECT(
                    env.le(trustId)->getAccountID(isIssuerHigh ? sfLowSponsor : sfHighSponsor) ==
                    sponsor.id());
                BEAST_EXPECT(
                    !env.le(trustId)->isFieldPresent(isIssuerHigh ? sfHighSponsor : sfLowSponsor));

                // dissolve sponsor
                env(sponsor::transfer(user, tfSponsorshipEnd, trustId.key));
                env.close();

                BEAST_EXPECT(env.le(trustId));
                BEAST_EXPECT(
                    !env.le(trustId)->isFieldPresent(isIssuerHigh ? sfLowSponsor : sfHighSponsor));
                BEAST_EXPECT(
                    !env.le(trustId)->isFieldPresent(isIssuerHigh ? sfHighSponsor : sfLowSponsor));
            }
        }

        {
            // invalid transfer
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // create owner dir
            env(ticket::create(alice, 1));
            env.close();

            // AccountRoot
            // Amendments
            // LedgerHashes
            // FeeSettings
            // NegativeUNL
            // DirNode
            auto const keylets = {
                keylet::account(alice),
                // keylet::amendments(),
                keylet::skip(),
                keylet::fees(),
                // keylet::negativeUNL(),
                keylet::ownerDir(alice),
            };
            for (auto const& keylet : keylets)
            {
                env(sponsor::transfer(alice, tfSponsorshipCreate, keylet.key),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor),
                    ter(tecNO_PERMISSION));
            }
        }
    }

    void
    testSponsorFee()
    {
        using namespace test::jtx;

        testcase("Sponsor Fee");

        {
            // co-signing
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob);
            env.close();

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor),
                    ter(terNO_ACCOUNT));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env.fund(XRP(1000), sponsor);
            env.close();

            {
                // Sponsor pays the fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt),
                    fee(feeAmt),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance - feeAmt);
            }

            {
                // insufficient balance to pay fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor),
                    ter(terINSUF_FEE_B));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            {
                // fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto const feeAmt = XRP(10);

                env(pay(alice, bob, XRP(20000)),
                    fee(feeAmt),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor),
                    ter(tecUNFUNDED_PAYMENT));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance - feeAmt);
            }

            {
                // below reserve
                adjustAccountXRPBalance(env, sponsor, env.current()->fees().reserve);
                env.close();
                auto const feeAmt = XRP(4);

                env(noop(alice),
                    fee(env.current()->fees().base),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor),
                    ter(terINSUF_FEE_B));
                env.close();

                env(noop(alice),
                    fee(XRP(10)),
                    sponsor::as(sponsor, spfSponsorFee),
                    sig(sfSponsorSignature, sponsor),
                    ter(terINSUF_FEE_B));
                env.close();
            }
        }

        {
            // pre funded
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            auto const sponsorFeeBalance = [&](Account const& sponsor, Account const& sponsee) {
                return env.le(keylet::sponsor(sponsor, sponsee))->getFieldAmount(sfFeeAmount).xrp();
            };

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, spfSponsorFee),
                    ter(terNO_SPONSORSHIP));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env(sponsor::set_fee(sponsor, 0, XRP(100)), sponsor::sponseeAcc(alice));
            env.close();

            {
                // Sponsor pays the fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt), fee(feeAmt), sponsor::as(sponsor, spfSponsorFee));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }

            {
                // insufficient balance to pay fee
                {
                    // > FeeAmount
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        fee(XRP(90) + drops(1)),
                        sponsor::as(sponsor, spfSponsorFee),
                        ter(terINSUF_FEE_B));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance);
                    BEAST_EXPECT(env.balance(bob) == bobBalance);
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee);
                }
                // use all FeeAmount
                {
                    // = FeeAmount
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);

                    env(pay(alice, bob, XRP(100)),
                        fee(XRP(90)),
                        sponsor::as(sponsor, spfSponsorFee),
                        ter(tesSUCCESS));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
                    BEAST_EXPECT(env.balance(bob) == bobBalance + XRP(100));
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(
                        !env.le(keylet::sponsor(sponsor, alice))->isFieldPresent(sfFeeAmount));
                }

                // reset FeeAmount and MaxFee
                env(sponsor::del(sponsor), sponsor::sponseeAcc(alice));
                env.close();
                env(sponsor::set_fee(sponsor, 0, XRP(10), XRP(1)), sponsor::sponseeAcc(alice));
                env.close();

                {
                    // > MaxFee
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        fee(XRP(1) + drops(1)),
                        sponsor::as(sponsor, spfSponsorFee),
                        ter(terINSUF_FEE_B));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance);
                    BEAST_EXPECT(env.balance(bob) == bobBalance);
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee);
                }
            }

            {
                // fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);
                auto const feeAmt = XRP(1);

                env(pay(alice, bob, XRP(20000)),
                    fee(feeAmt),
                    sponsor::as(sponsor, spfSponsorFee),
                    ter(tecUNFUNDED_PAYMENT));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }

            // make sfFeeAmount absent if tec error and all fee is paid
            {
                // reset FeeAmount and MaxFee
                env(sponsor::del(sponsor), sponsor::sponseeAcc(alice));
                env(sponsor::set_fee(sponsor, 0, XRP(10)), sponsor::sponseeAcc(alice));
                env.close();

                BEAST_EXPECT(env.le(keylet::sponsor(sponsor, alice))->isFieldPresent(sfFeeAmount));
                auto sponsorAvailableFee = sponsorFeeBalance(sponsor, alice);
                env(check::cancel(alice, uint256(1)),
                    fee(sponsorAvailableFee),
                    sponsor::as(sponsor, spfSponsorFee),
                    ter(tecNO_ENTRY));
                env.close();
                BEAST_EXPECT(!env.le(keylet::sponsor(sponsor, alice))->isFieldPresent(sfFeeAmount));
            }
        }

        // test lsfSponsorshipRequireSignForFee
        {
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            env(pay(alice, bob, XRP(100)),
                fee(XRP(10)),
                sponsor::as(sponsor, spfSponsorFee),
                ter(terNO_SPONSORSHIP));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::sponsor(sponsor, alice))->getFieldAmount(sfFeeAmount) == XRP(10));

            // clear flag
            env(sponsor::set_fee(sponsor, tfSponsorshipClearRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            // Payment is re-applied
            BEAST_EXPECT(!env.le(keylet::sponsor(sponsor, alice))->isFieldPresent(sfFeeAmount));
        }

        // RequireSignForFee: co-signing should succeed
        {
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            // pre-funded (no sig) should fail
            env(pay(alice, bob, XRP(100)),
                fee(XRP(1)),
                sponsor::as(sponsor, spfSponsorFee),
                ter(terNO_SPONSORSHIP));
            env.close();

            // co-signing (with sig) should succeed
            env(pay(alice, bob, XRP(100)),
                fee(XRP(1)),
                sponsor::as(sponsor, spfSponsorFee),
                sig(sfSponsorSignature, sponsor),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::sponsor(sponsor, alice))->getFieldAmount(sfFeeAmount) == XRP(9));
        }
    }

    void
    testSponsorAccount()
    {
        testcase("Sponsor Account");
        using namespace test::jtx;

        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");
        Account const sponsor3("sponsor3");
        Account const bob("bob");
        Account const charlie("charlie");
        Account const dave("dave");
        Account const gw("gw");
        auto const USD = gw["USD"];

        {
            // Disabled
            Env env{*this, testable_amendments() - featureSponsor};
            env.fund(XRP(10000), alice, sponsor);
            env.close();
            env(pay(alice, bob, XRP(100)), txflags(tfSponsorCreatedAccount), ter(temDISABLED));
            env.close();
        }

        Env env{*this, testable_amendments()};
        env.fund(XRP(10000), alice, sponsor, sponsor2, sponsor3);
        env.close();

        // Invalid flags
        for (auto flag : {
                 tfNoRippleDirect,
                 tfPartialPayment,
                 tfLimitQuality,
             })
        {
            env(pay(alice, bob, XRP(100)),
                txflags(tfSponsorCreatedAccount | flag),
                ter(temINVALID_FLAG));
            env.close();
        }

        // Invalid amount(iou)
        env(pay(alice, bob, USD(100)), txflags(tfSponsorCreatedAccount), ter(temBAD_AMOUNT));
        env.close();

        // Account is not sponsored by normal Sponsor specification
        {
            env(pay(alice, bob, drops(env.current()->fees().accountReserve(0))),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            auto const bobSle = env.le(keylet::account(bob));
            BEAST_EXPECT(!bobSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 0);
        }

        // Use tfSponsorCreatedAccount to sponsor an account
        {
            // to funded account
            env(pay(sponsor2, bob, drops(1)),
                txflags(tfSponsorCreatedAccount),
                fee(XRP(1)),
                ter(tecNO_SPONSOR_PERMISSION));
            env.close();

            BEAST_EXPECT(env.balance(sponsor2) == XRP(9999));

            // to non-funded account / insufficient balance for reserve
            env(pay(sponsor2, charlie, XRP(9999) - env.current()->fees().reserve + drops(1)),
                txflags(tfSponsorCreatedAccount),
                ter(tecUNFUNDED_PAYMENT));
            env.close();

            // to non-funded account
            auto const sponsor2BalanceBefore = env.balance(sponsor2);
            env(pay(sponsor2, charlie, drops(1)), txflags(tfSponsorCreatedAccount), fee(XRP(1)));
            env.close();

            auto const charlieSle = env.le(keylet::account(charlie));
            BEAST_EXPECT(charlieSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(charlieSle->getAccountID(sfSponsor) == sponsor2.id());
            BEAST_EXPECT(sponsoredOwnerCount(env, charlie) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 1);
            // verify sponsor balance decreased by payment + fee
            BEAST_EXPECT(env.balance(sponsor2) == sponsor2BalanceBefore - drops(1) - XRP(1));
        }
        {
            // insufficient reserve to sponsor acount

            auto const sendAmount = drops(1);
            // 2 account reserve + send amount
            auto const requireBalance = accountReserve(env, 2) + sendAmount;
            adjustAccountXRPBalance(env, sponsor3, requireBalance - drops(1));
            env(pay(sponsor3, dave, sendAmount),
                txflags(tfSponsorCreatedAccount),
                fee(XRP(1)),
                ter(tecUNFUNDED_PAYMENT));
            env.close();

            adjustAccountXRPBalance(env, sponsor3, requireBalance);
            env(pay(sponsor3, dave, sendAmount),
                txflags(tfSponsorCreatedAccount),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testRequireFlag()
    {
        using namespace test::jtx;
        {
            testcase("SponsorshipRequireSignForReserve");

            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_reserve(sponsor, tfSponsorshipSetRequireSignForReserve, 10),
                sponsor::sponseeAcc(alice));
            env.close();

            env(check::create(alice, bob, XRP(100)),
                fee(XRP(10)),
                sponsor::as(sponsor, spfSponsorReserve),
                ter(terNO_SPONSORSHIP));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // clear flag
            env(sponsor::set_reserve(sponsor, tfSponsorshipClearRequireSignForReserve, 1),
                sponsor::sponseeAcc(alice));
            env.close();

            // CheckCreate is re-applied
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }

        {
            testcase("SponsorshipRequireSignForFee");

            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            env(check::create(alice, bob, XRP(100)),
                fee(XRP(10)),
                sponsor::as(sponsor, spfSponsorFee),
                ter(terNO_SPONSORSHIP));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(
                env.le(keylet::sponsor(sponsor, alice))->getFieldAmount(sfFeeAmount) == XRP(10));

            // clear flag
            env(sponsor::set_fee(sponsor, tfSponsorshipClearRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            // CheckCreate is re-applied
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(!env.le(keylet::sponsor(sponsor, alice))->isFieldPresent(sfFeeAmount));
        }
    }

    void
    testSponsorReserveSimple(bool cosigning)
    {
        testcase("SponsorReserveSimple");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // test Sufficient sponsor balance
        if (cosigning)
        {
            adjustAccountXRPBalance(env, sponsor, reserve(env, 99));

            env(ticket::create(alice, 100),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 100));

            env(ticket::create(alice, 100),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor),
                ter(tesSUCCESS));
            env.close();
        }
        else
        {
            env(sponsor::set_reserve(sponsor, 0, 250), sponsor::sponseeAcc(alice));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 99 + 1 /* sponsor object*/));

            env(ticket::create(alice, 100),
                sponsor::as(sponsor, spfSponsorReserve),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 100 + 1 /* sponsor object*/));

            env(ticket::create(alice, 100),
                sponsor::as(sponsor, spfSponsorReserve),
                ter(tesSUCCESS));
            env.close();
        }
    }

    // test helper for both cosigning and pre-funded sponsorship
    template <typename SubmitCallback>
    void
    testEachSponsorship(
        test::jtx::Env& env,
        bool cosigning,
        jtx::Account const& sponsor,
        jtx::Account const& sponsee,
        uint32_t reserveCount,
        uint32_t sponsorReserveCount,
        TER insufficientReserveResult,
        SubmitCallback callback,
        std::optional<std::function<void()>> expected = std::nullopt)
    {
        using namespace test::jtx;
        // auto const sponsorOwnerCountBefore = ownerCount(env, sponsor);
        auto const sponseeOwnerCountBefore = ownerCount(env, sponsee);
        auto const sponseeSponsoredOwnerCountBefore = sponsoredOwnerCount(env, sponsee);
        auto const sponseeSponsoringOwnerCountBefore = sponsoringOwnerCount(env, sponsee);
        auto const sponsorSponsoringOwnerCountBefore = sponsoringOwnerCount(env, sponsor);

        std::optional<sig> sponsorSig =
            cosigning ? std::optional<sig>(sig(sfSponsorSignature, sponsor)) : std::nullopt;

        auto const sponsorCurrentOwnerCount = ownerCount(env, sponsor) -
            sponsoredOwnerCount(env, sponsor) + sponsoringOwnerCount(env, sponsor);

        auto submit = [&](TER _ter) {
            return [&, _ter](Json::Value const& jv, auto const&... fN) {
                if (sponsorSig)
                    env(jv, fN..., sponsor::as(sponsor, spfSponsorReserve), *sponsorSig, ter(_ter));
                else
                    env(jv, fN..., sponsor::as(sponsor, spfSponsorReserve), ter(_ter));
            };
        };

        // Insufficient Reserve
        {
            if (cosigning)
            {
                adjustAccountXRPBalance(
                    env,
                    sponsor,
                    reserve(env, sponsorCurrentOwnerCount + sponsorReserveCount) - drops(1));
            }
            else
            {
                // cleanup previous sponsorship
                if (env.le(keylet::sponsor(sponsor, sponsee)))
                {
                    env(sponsor::del(sponsor), sponsor::sponseeAcc(sponsee));
                    env.close();
                }

                if (sponsorReserveCount - 1 > 0)
                    env(sponsor::set(sponsor, 0, sponsorReserveCount - 1, XRP(1)),
                        sponsor::sponseeAcc(sponsee));
                else
                    // just create sponsor object
                    env(sponsor::set(sponsor, 0, std::nullopt, XRP(1)),
                        sponsor::sponseeAcc(sponsee));
                env.close();
            }
            callback(env, submit(insufficientReserveResult));
            env.close();
        }

        // Success
        {
            if (cosigning)
            {
                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, sponsorCurrentOwnerCount + sponsorReserveCount));
            }
            else
            {
                // reset sponsorship
                env(sponsor::del(sponsor), sponsor::sponseeAcc(sponsee));
                env(sponsor::set(sponsor, 0, sponsorReserveCount, XRP(1)),
                    sponsor::sponseeAcc(sponsee));
                env.close();
            }
            callback(env, submit(tesSUCCESS));
            env.close();

            if (!cosigning)
            {
                // cleanup sponsorship
                env(sponsor::del(sponsor), sponsor::sponseeAcc(sponsee));
                env.close();
            }
        }

        if (expected)
            (*expected)();
        else
        {
            BEAST_EXPECT(ownerCount(env, sponsee) - sponseeOwnerCountBefore == reserveCount);
            BEAST_EXPECT(
                sponsoredOwnerCount(env, sponsee) - sponseeSponsoredOwnerCountBefore ==
                sponsorReserveCount);
            BEAST_EXPECT(
                sponsoringOwnerCount(env, sponsee) - sponseeSponsoringOwnerCountBefore == 0);
            BEAST_EXPECT(
                sponsoringOwnerCount(env, sponsor) - sponsorSponsoringOwnerCountBefore ==
                sponsorReserveCount);
        }
    };

    void
    testAMM(bool cosigning)
    {
        testcase("AMM");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const ammCreate = [&](Env& env,
                                   Account const& account,
                                   STAmount const& amount1,
                                   STAmount const& amount2) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::AMMCreate;
            jv[jss::Account] = account.human();
            jv[jss::Amount] = amount1.getJson(JsonOptions::none);
            jv[jss::Amount2] = amount2.getJson(JsonOptions::none);
            jv[jss::TradingFee] = 0;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            return jv;
        };

        auto const ammDeposit = [&](Env& env,
                                    Account const& account,
                                    STAmount const& amount1,
                                    STAmount const& amount2) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::AMMDeposit;
            jv[jss::Account] = account.human();
            jv[jss::Asset] = STIssue(sfAsset, amount1.asset()).getJson(JsonOptions::none);
            jv[jss::Asset2] = STIssue(sfAsset, amount2.asset()).getJson(JsonOptions::none);
            jv[jss::Amount] = amount1.value().getJson(JsonOptions::none);
            jv[jss::Amount2] = amount2.value().getJson(JsonOptions::none);
            jv[jss::Flags] = tfTwoAsset;
            return jv;
        };

        {
            // AMMCreate
            // - sponsor LPToken
            // - doesn't sponsor AMM object
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, gw, sponsor);
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR(10000)));
            env.close();

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, alice, EUR(1000)));
            env.close();

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUF_RESERVE_LINE,
                [&](Env& env, auto const& submit) {
                    submit(ammCreate(env, alice, USD(100), EUR(100)));
                },
                [&]() {
                    auto const amm = env.current()->read(keylet::amm(USD.issue(), EUR.issue()));
                    auto const ammAccount = Account("amm", amm->getAccountID(sfAccount));
                    BEAST_EXPECT(ownerCount(env, alice) == 3);  // RippleState (USD,EUR/LP Token)
                    BEAST_EXPECT(ownerCount(env, ammAccount) == 2);      //  USD, EUR
                    BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);  // LPToken
                    BEAST_EXPECT(sponsoredOwnerCount(env, ammAccount) == 0);
                    BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // LPToken
                    BEAST_EXPECT(
                        !env.le(keylet::amm(USD.issue(), EUR.issue()))->isFieldPresent(sfSponsor));
                });

            auto const ammKeylet = keylet::amm(USD.issue(), EUR.issue());
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipCreate, ammKeylet.key),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor),
                    ter(tecNO_PERMISSION));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(alice));
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(alice));
                env(sponsor::transfer(alice, tfSponsorshipCreate, ammKeylet.key),
                    sponsor::as(sponsor, spfSponsorReserve),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
        {
            // AMMDeposit
            // - sponsor new LPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor);
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR(10000)));
            env(trust(bob, USD(10000)));
            env(trust(bob, EUR(10000)));
            env.close();

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, alice, EUR(1000)));
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, bob, EUR(1000)));
            env.close();

            env(ammCreate(env, alice, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 2);  // RippleState (USD,EUR)

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUF_RESERVE_LINE,
                [&](Env& env, auto const& submit) {
                    submit(ammDeposit(env, bob, USD(100), EUR(100)));
                });
        }
        {
            // AMMWithdraw
            {
                // Single Asset Withdraw
                // - sponsor new RippleState
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, gw, sponsor);
                env.close();

                env(trust(alice, USD(10000)));
                env(trust(alice, EUR(10000)));
                env.close();

                env(pay(gw, alice, USD(1000)));
                env(pay(gw, alice, EUR(1000)));
                env.close();

                env(ammCreate(env, alice, USD(1000), EUR(1000)),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                env(trust(alice, USD(0)));
                env(trust(alice, EUR(0)));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);              // LPToken
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);     // LPToken
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // LPToken

                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = STIssue(sfAsset, USD.issue()).getJson(JsonOptions::none);
                jv[jss::Asset2] = STIssue(sfAsset, EUR.issue()).getJson(JsonOptions::none);
                jv[jss::Amount] = USD(100).value().getJson(JsonOptions::none);
                jv[jss::Flags] = tfSingleAsset;

                env(ticket::create(sponsor, 1));  // adjust for free
                env.close();

                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    1,
                    1,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(jv); });
            }
            {
                // Double Asset Withdraw
                // - sponsor new RippleState * 2
                // - remove sponsored LPToken
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, gw, sponsor);
                env.close();

                env(trust(alice, USD(10000)));
                env(trust(alice, EUR(10000)));
                env.close();

                env(pay(gw, alice, USD(1000)));
                env(pay(gw, alice, EUR(1000)));
                env.close();

                env(ammCreate(env, alice, USD(1000), EUR(1000)),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                env(trust(alice, USD(0)));
                env(trust(alice, EUR(0)));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);  // LPToken
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] = STIssue(sfAsset, USD.issue()).getJson(JsonOptions::none);
                jv[jss::Asset2] = STIssue(sfAsset, EUR.issue()).getJson(JsonOptions::none);
                jv[jss::Flags] = tfWithdrawAll;

                env(ticket::create(sponsor, 1));  // adjust for free trustline
                env.close();

                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    2,
                    2,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(jv); },
                    [&]() {
                        // LPToken deleted, USD, EUR created
                        BEAST_EXPECT(ownerCount(env, alice) == 2);
                        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
                    });
            }
        }
        {
            // AMMClawback
            // - doesn't sponsor holder's new RippleState
            // - remove sponsored LPToken
            Account const gw2("gw2");
            auto const EUR2 = gw2["EUR"];

            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, gw, gw2, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR2(10000)));
            env.close();

            env(pay(gw, alice, USD(100)));
            env(pay(gw2, alice, EUR2(100)));
            env.close();

            env(ammCreate(env, alice, USD(100), EUR2(100)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            env(trust(alice, USD(0)));
            env(trust(alice, EUR2(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // LPToken
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            {
                // doesn't sponsor holder's new RippleState
                env(amm::ammClawback(gw, alice, USD, EUR2, USD(10)),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);  // LPToken, EUR2
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            }
            {
                // remove sponsored LPToken
                env(amm::ammClawback(gw, alice, USD, EUR2, std::nullopt));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);  // EUR2
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
        }
        {
            // AMMDelete
            // - remove sponsored LPToken trustlines
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee = XRPAmount(1);
                    return cfg;
                }),
                testable_amendments());
            env.fund(XRP(20'000), alice, gw, sponsor);
            env.close();
            env(trust(alice, USD(10'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();

            AMM amm(env, gw, XRP(10'000), USD(10'000));
            for (auto i = 0; i < (maxDeletableAMMTrustLines * 2) + 10; ++i)
            {
                Account const a{std::to_string(i)};
                env.fund(XRP(1'000), a);
                if (cosigning)
                {
                    env(trust(a, STAmount{amm.lptIssue(), 10'000}),
                        sponsor::as(sponsor, spfSponsorReserve),
                        sig(sfSponsorSignature, sponsor));
                    env.close();
                }
                else
                {
                    env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(a));
                    env.close();
                    env(trust(a, STAmount{amm.lptIssue(), 10'000}),
                        sponsor::as(sponsor, spfSponsorReserve));
                    env.close();
                }
            }

            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == maxDeletableAMMTrustLines * 2 + 10);

            // The trustlines are partially deleted.
            amm.withdrawAll(gw);
            BEAST_EXPECT(amm.ammExists());

            // AMMDelete has to be called twice to delete AMM.
            amm.ammDelete(alice, ter(tecINCOMPLETE));
            BEAST_EXPECT(amm.ammExists());

            // Deletes remaining trustlines and deletes AMM.
            amm.ammDelete(alice);
            BEAST_EXPECT(!amm.ammExists());
            BEAST_EXPECT(!env.le(keylet::ownerDir(amm.ammAccount())));

            BEAST_EXPECT(
                !env.le(keylet::account(sponsor))->isFieldPresent(sfSponsoringAccountCount));
        }
    }

    void
    testCheck(bool cosigning)
    {
        testcase("Check");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const USD = gw["USD"];

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, sponsor, sponsor2);
            env.close();

            // CheckCreate -> Check = 0Cancel

            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(check::create(alice, bob, XRP(1)));
                });

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // Check
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            auto const keylet = keylet::check(alice, seq);
            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor.id());

            if (cosigning)
            {
                // transfer sponsor
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                // transfer sponsor
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // Check
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor2.id());

            // CheckCancel
            env(check::cancel(alice, keylet.key));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // CheckCreate -> = 0 CheckCash
            uint32_t seq2 = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq2 = env.seq(alice);
                    submit(check::create(alice, bob, XRP(1)));
                });

            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);

            // CheckCash
            auto const checkId2 = keylet::check(alice, seq2).key;
            env(check::cash(bob, checkId2, XRP(1)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        // RippleState sponsor (CheckCashMakesTrustLine)
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env.trust(USD(100), alice);
            env.close();
            env(pay(gw, alice, USD(100)));
            env.close();

            // CheckCreat = 0e -> CheckCash
            uint32_t seq2 = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq2 = env.seq(alice);
                    submit(check::create(alice, bob, USD(1)));
                });

            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);

            auto const keylet = keylet::check(alice, seq2);
            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor.id());

            // CheckCash
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecNO_LINE_INSUF_RESERVE,
                [&](Env& env, auto const& submit) { submit(check::cash(bob, keylet.key, USD(1))); },
                [&]() {
                    BEAST_EXPECT(ownerCount(env, alice) == 1);  // RippleState
                    BEAST_EXPECT(ownerCount(env, bob) == 1);    // RippleState
                    BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                    BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
                    BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
                });
        }
    }

    void
    testOffer(bool cosigning)
    {
        testcase("Offer");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor1("sponsor1");
        Account const sponsor2("sponsor2");

        auto USD = gw["USD"];
        auto EUR = gw["EUR"];

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(10000), alice, gw, sponsor1, sponsor2);
            env.close();

            // OfferCreate
            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor1,
                alice,
                1,
                1,
                tecINSUF_RESERVE_OFFER,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(offer(alice, USD(1), XRP(1)));
                });

            // transfer sponsor
            auto const keylet = keylet::offer(alice, seq);
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor2.id());

            // OfferCancel
            env(offer_cancel(alice, seq));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(10000), alice, gw, sponsor1, sponsor2);
            env.close();

            // OfferCreate
            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor1,
                alice,
                1,
                1,
                tecINSUF_RESERVE_OFFER,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(offer(alice, USD(1), XRP(1)));
                });

            // OfferCreate with Cancel (new sponsor)
            auto const seq2 = env.seq(alice);
            if (cosigning)
            {
                env(offer(alice, USD(1), XRP(1)),
                    json(jss::OfferSequence, seq),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(offer(alice, USD(1), XRP(1)),
                    json(jss::OfferSequence, seq),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // OfferCreate with Cancel (no sponsor)
            env(offer(alice, USD(1), XRP(1)), json(jss::OfferSequence, seq2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        // test Offer Execution doesn't sponsor new trustline
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor1, sponsor2);
            env.close();

            env(trust(alice, USD(100)));
            env(trust(bob, EUR(100)));
            env.close();

            env(pay(gw, alice, USD(100)));
            env(pay(gw, bob, EUR(100)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // OfferCreate
            if (cosigning)
            {
                env(offer(alice, EUR(1), USD(1)),
                    sponsor::as(sponsor1, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor1));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor1, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(offer(alice, EUR(1), USD(1)), sponsor::as(sponsor1, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);

            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // OfferCreate (cross offer)
            if (cosigning)
            {
                env(offer(bob, USD(1), EUR(1)),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(bob));
                env.close();

                env(offer(bob, USD(1), EUR(1)), sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);

            // does not sponsor new trustline by cross offer
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testTicket(bool cosigning)
    {
        testcase("Ticket");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // TicketCreate
            uint32_t ticketSeq = 0;

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                250,
                250,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    ticketSeq = env.seq(alice) + 1;
                    submit(ticket::create(alice, 250));
                });

            auto const keylet = keylet::ticket(alice, ticketSeq);
            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor.id());

            // transfer sponsor
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 250);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 250);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor2.id());

            // use a Ticket
            env(noop(alice), ticket::use(ticketSeq));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 249);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testCredentials(bool cosigning)
    {
        testcase("Credentials");
        using namespace test::jtx;
        Account const issuer("issuer");
        Account const subject("subject");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const credType = std::string("credType");
        auto const credTypeSlice = Slice(credType.data(), credType.size());

        // CredentialsCreate
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), issuer, subject, sponsor, sponsor2);
            env.close();

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                issuer,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(credentials::create(subject, issuer, credType), credentials::uri("uri"));
                });

            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);

            // transfer sponsor
            auto const keylet = keylet::credential(subject, issuer, credTypeSlice);
            if (cosigning)
            {
                env(sponsor::transfer(issuer, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(issuer));
                env.close();

                env(sponsor::transfer(issuer, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // CredentialsAccept
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                subject,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(credentials::accept(subject, issuer, credType));
                });

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // transfer accepted credential
            if (cosigning)
            {
                env(sponsor::transfer(subject, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(subject));
                env.close();

                env(sponsor::transfer(subject, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            // CredentialsDelete
            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), issuer, subject, sponsor);
            env.close();

            // Accept Sponsored Credentials without sponsoring
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                issuer,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(credentials::create(subject, issuer, credType));
                });

            env(credentials::accept(subject, issuer, credType));
            env.close();

            // sponsorship is removed
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(!env.le(keylet::credential(subject, issuer, credTypeSlice))
                              ->isFieldPresent(sfSponsor));

            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();
        }
    }

    void
    testDelegate(bool cosigning)
    {
        testcase("Delegate");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // DelegateSet
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(delegate::set(alice, bob, {"Payment"}));
                });

            // transfer sponsor
            auto const keylet = keylet::delegate(alice, bob);
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // delete
            env(delegate::set(alice, bob, {}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
    }

    void
    testDepositPreauth(bool cosigning)
    {
        testcase("DepositPreauth");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // DepositPreauthSet
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) { submit(deposit::auth(alice, sponsor)); });

            // transfer sponsor
            auto const keylet = keylet::depositPreauth(alice, sponsor);
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // DepositPreauthDelete
            env(deposit::unauth(alice, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testDID(bool cosigning)
    {
        testcase("DID");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // DIDSet
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) { submit(did::set(alice), did::uri("uri")); });

            // transfer sponsor
            auto const keylet = keylet::did(alice);
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // DIDDelete
            env(did::del(alice));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testEscrow(bool cosigning)
    {
        testcase("Escrow");
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");
        {
            // Native Escrow
            Env env{*this, testable_amendments()};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // EscrowCreate
            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(
                        escrow::create(alice, bob, XRP(100)),
                        escrow::condition(escrow::cb1),
                        escrow::cancel_time(env.now() + 100s));
                });
            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))->getAccountID(sfSponsor) == sponsor.id());

            // transfer sponsor
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::escrow(alice, seq).key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::escrow(alice, seq).key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))->getAccountID(sfSponsor) == sponsor2.id());

            // EscrowFinish
            env(escrow::finish(bob, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        Account const gw("gw");
        auto const USD = gw["USD"];
        {
            // IOU Escrow
            Env env{*this, testable_amendments()};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env.trust(USD(1000000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // EscrowCreate
            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(
                        escrow::create(alice, bob, USD(100)),
                        escrow::condition(escrow::cb1),
                        escrow::cancel_time(env.now() + 100s));
                });

            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))->getAccountID(sfSponsor) == sponsor.id());

            // EscrowFinish
            testEachSponsorship(
                env,
                cosigning,
                sponsor2,
                bob,
                1,
                1,
                tecNO_LINE_INSUF_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(
                        escrow::finish(bob, alice, seq),
                        escrow::condition(escrow::cb1),
                        escrow::fulfillment(escrow::fb1),
                        fee(baseFee * 150));
                });

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            BEAST_EXPECT(
                env.le(keylet::line(bob, gw, USD.currency))->getAccountID(sfHighSponsor) ==
                sponsor2.id());
        }
        {
            // MPT Escrow
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // create Escrow from alice to bob
            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, MPT(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 100s));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // finish Escrow
            env(escrow::finish(bob, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor),
                fee(XRP(1)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
    }

    void
    testMPToken(bool cosigning)
    {
        testcase("MPToken");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // MPTokenIssuanceCreate
            Json::Value jv = {};
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
            MPTID mptid;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    mptid = makeMptID(env.seq(alice), alice.id());
                    submit(jv);
                });

            // transfer sponsor
            auto const mptIssuanceKeylet = keylet::mptIssuance(mptid);

            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, mptIssuanceKeylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, mptIssuanceKeylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            // MPTokenAuthorize
            jv = {};
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfAccount] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);

            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
                env(ticket::create(sponsor, 2));  // adjust for free mptoken
                env.close();
            }

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) { submit(jv); });

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);

            // transfer sponsor
            auto const mptTokenKeylet = keylet::mptoken(mptid, bob);
            if (cosigning)
            {
                env(sponsor::transfer(bob, tfSponsorshipReassign, mptTokenKeylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(bob));
                env.close();

                env(sponsor::transfer(bob, tfSponsorshipReassign, mptTokenKeylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);

            // MPTokenAuthorize Unauthorize
            jv = {};
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfAccount] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            jv[sfFlags] = tfMPTUnauthorize;
            env(jv);
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // MPTokenIssuanceDestroy
            jv = {};
            jv[sfTransactionType] = jss::MPTokenIssuanceDestroy;
            jv[sfAccount] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            env(jv);
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
        {
            // check INSUFFICIENT_RESERVE for MPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // MPTokenAuthorize
            Json::Value jv = {};
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
            auto const mptid = makeMptID(env.seq(alice), alice.id());
            env(jv);
            env.close();

            // for free mptoken checks
            // adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
            std::uint32_t const ticketSeq{env.seq(sponsor) + 1};
            env(ticket::create(sponsor, 2));
            env.close();

            // adjustAccountXRPBalance(env, sponsor, reserve(env, 3) -
            // drops(1));
            jv = {};
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfAccount] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            // error (non-free mptoken)
            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 3) - drops(1));
                env(jv,
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
            else
            {
                env(sponsor::set(sponsor, 0, std::nullopt, XRP(1)), sponsor::sponseeAcc(bob));
                env.close();

                env(jv, sponsor::as(sponsor, spfSponsorReserve), ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }

            env(noop(sponsor), ticket::use(ticketSeq));
            env.close();

            // pass (free mptoken)
            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2) - drops(1));
                env(jv,
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor),
                    ter(tesSUCCESS));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(bob));
                env.close();
                env(jv, sponsor::as(sponsor, spfSponsorReserve), ter(tesSUCCESS));
                env.close();
            }
        }
    }

    void
    testNFToken(bool cosigning)
    {
        testcase("NFToken");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // NFTokenMint
            uint256 nftId;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    nftId = token::getNextID(env, alice, 0);
                    submit(token::mint(alice));
                });

            // transfer sponsor
            auto const keylet = keylet::nftpage_max(alice);
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
            }
            // NFTokenBurn
            env(token::burn(alice, nftId));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // NFTokenMintOffer
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                2,
                2,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(token::mint(alice), token::amount(XRP(100)));
                });
        }

        {
            // multiple nft page process
            Env env{*this, testable_amendments()};

            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            auto const nftCount = 200;

            // NFTokenMint
            if (cosigning)
            {
                for (auto i = 0; i < nftCount; i++)
                {
                    env(token::mint(alice),
                        sponsor::as(sponsor, spfSponsorReserve),
                        sig(sfSponsorSignature, sponsor));
                }
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 8), sponsor::sponseeAcc(alice));
                env.close();
                for (auto i = 0; i < nftCount; i++)
                {
                    env(token::mint(alice), sponsor::as(sponsor, spfSponsorReserve));
                }
            }
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == sponsoredOwnerCount(env, alice));
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == sponsoringOwnerCount(env, sponsor));

            // NFTokenBurn
            for (auto i = 0; i < nftCount; i++)
            {
                auto const nftId = token::getID(env, alice, 0, i, 0, 0);
                env(token::burn(alice, nftId));
            }
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
    }

    void
    testNFTokenOffer(bool cosigning)
    {
        testcase("NFTokenOffer");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const broker("broker");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const taxon = 0u;

        {
            // Mint + CreateOffer + CancelOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // Mint
            uint256 const nftId{token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            // NFTokenOfferCreate
            uint256 offerIndex1;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    offerIndex1 = keylet::nftoffer(alice, env.seq(alice)).key;
                    submit(
                        token::createOffer(alice, nftId, XRP(1)),
                        token::destination(bob),
                        txflags(tfSellNFToken));
                });

            uint256 offerIndex2;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    offerIndex2 = keylet::nftoffer(alice, env.seq(alice)).key;
                    submit(
                        token::createOffer(alice, nftId, XRP(1)),
                        token::destination(bob),
                        txflags(tfSellNFToken));
                });

            // transfer sponsor
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, offerIndex1),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, offerIndex1),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // NFTokenOfferCancel
            env(token::cancelOffer(alice, {offerIndex1, offerIndex2}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // Mint + CreateSellOffer + AcceptSellOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // Mint
            uint256 const nftId{token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            // NFTokenOfferCreate
            uint256 offerIndex;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    offerIndex = keylet::nftoffer(alice, env.seq(alice)).key;
                    submit(
                        token::createOffer(alice, nftId, XRP(1)),
                        token::destination(bob),
                        txflags(tfSellNFToken));
                });

            // NFTokenOfferAccept
            env(token::acceptSellOffer(bob, offerIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // Mint + CreateBuyOffer + AcceptBuyOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // Mint
            uint256 const nftId{token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            // NFTokenOfferCreate
            uint256 offerIndex;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    offerIndex = keylet::nftoffer(bob, env.seq(bob)).key;
                    submit(
                        token::createOffer(bob, nftId, XRP(1)),
                        token::owner(alice),
                        token::destination(alice));
                });

            // NFTokenOfferAccept
            env(token::acceptBuyOffer(alice, offerIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
        {
            // Broker
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, broker, sponsor, sponsor2);
            env.close();

            // Mint
            uint256 const nftId{token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // NFTokenOfferCreate (BuyOffer)
            uint256 buyOfferIndex;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    buyOfferIndex = keylet::nftoffer(bob, env.seq(bob)).key;
                    submit(
                        token::createOffer(bob, nftId, XRP(1)),
                        token::owner(alice),
                        token::destination(broker));
                });

            // NFTokenOfferCreate (SellOffer)
            uint256 sellOfferIndex;
            testEachSponsorship(
                env,
                cosigning,
                sponsor2,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    sellOfferIndex = keylet::nftoffer(alice, env.seq(alice)).key;
                    submit(
                        token::createOffer(alice, nftId, XRP(1)),
                        txflags(tfSellNFToken),
                        token::destination(broker));
                });

            // NFTokenOfferAccept
            env(token::brokerOffers(broker, buyOfferIndex, sellOfferIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testPayChan(bool cosigning)
    {
        testcase("PayChan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // PayChanCreate
            auto const pk = alice.pk();
            auto const settleDelay = 10s;
            uint256 chan;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    chan = paychan::channel(alice, bob, env.seq(alice));
                    submit(paychan::create(alice, bob, XRP(100), settleDelay, pk));
                });

            // transfer sponsor
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, chan),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, chan),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            env.close(env.now() + settleDelay);
            // PayChanClaim (delete PayChan)
            env(paychan::claim(bob, chan), txflags(tfClose));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testPermissionedDomain(bool cosigning)
    {
        testcase("PermissionedDomain");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // PermissionedDomainSet
            pdomain::Credentials credentials{{alice, "first credential"}};
            uint32_t seq = 0;
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    seq = env.seq(alice);
                    submit(pdomain::setTx(alice, credentials));
                });

            // transfer sponsor
            auto const keylet = keylet::permissionedDomain(alice, seq);

            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                env.close();
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // PermissionedDomainDelete
            auto objects = pdomain::getObjects(alice, env);
            auto const domain = objects.begin()->first;
            env(pdomain::deleteTx(alice, domain));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
    }

    void
    testOracle(bool cosigning)
    {
        testcase("Oracle");
        using namespace test::jtx;
        using namespace std::chrono;
        using DataSeries =
            std::vector<std::tuple<std::string, std::string, std::uint32_t, std::uint8_t>>;

        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const oracleSet = [](Env& env, Account const& account, uint8_t dataSeriesSize) {
            auto const now = env.timeKeeper().now();
            env.close(now + oracle::testStartTime - epoch_offset);
            Json::Value jv;
            jv[jss::TransactionType] = jss::OracleSet;
            jv[jss::Account] = to_string(account);
            jv[jss::OracleDocumentID] = 1;
            jv[jss::LastUpdateTime] = to_string(
                duration_cast<seconds>(env.current()->header().closeTime.time_since_epoch())
                    .count() +
                epoch_offset.count() + 100);
            jv[jss::PriceDataSeries] = Json::arrayValue;
            jv[jss::Provider] = strHex(std::string{"provider"});
            jv[jss::AssetClass] = strHex(std::string{"currency"});

            DataSeries const series = {
                {"XRP", "US1", 740, 1},
                {"XRP", "US2", 750, 1},
                {"XRP", "US3", 740, 1},
                {"XRP", "US4", 750, 1},
                {"XRP", "US5", 740, 1},
                {"XRP", "US6", 750, 1},
                {"XRP", "US7", 740, 1},
                {"XRP", "US8", 750, 1},
                {"XRP", "US9", 740, 1},
                {"XRP", "U10", 750, 1},
            };

            DataSeries const actualSeries(series.begin(), series.begin() + dataSeriesSize);

            Json::Value dataSeries(Json::arrayValue);
            for (auto const& data : actualSeries)
            {
                Json::Value priceData;
                Json::Value price;
                price[jss::BaseAsset] = std::get<0>(data);
                price[jss::QuoteAsset] = std::get<1>(data);
                price[jss::AssetPrice] = std::get<2>(data);
                price[jss::Scale] = std::get<3>(data);
                priceData[jss::PriceData] = price;
                dataSeries.append(priceData);
            }
            jv[jss::PriceDataSeries] = dataSeries;
            return jv;
        };

        auto const oracleDelete = [&](Account const& account) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::OracleDelete;
            jv[jss::Account] = to_string(account);
            jv[jss::OracleDocumentID] = 1;
            return jv;
        };

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            {
                // OracleSet (reserve 1)
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    1,
                    1,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 5)); });

                // transfer sponsor
                auto const keylet = keylet::oracle(alice, 1);
                if (cosigning)
                {
                    env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                        sponsor::as(sponsor2, spfSponsorReserve),
                        sig(sfSponsorSignature, sponsor2));
                    env.close();
                }
                else
                {
                    env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
                    env.close();
                    env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                        sponsor::as(sponsor2, spfSponsorReserve));
                    env.close();
                }

                BEAST_EXPECT(ownerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 2)
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    2,
                    2,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 6)); });

                // transfer sponsor
                auto const keylet = keylet::oracle(alice, 1);
                if (cosigning)
                {
                    env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                        sponsor::as(sponsor2, spfSponsorReserve),
                        sig(sfSponsorSignature, sponsor2));
                    env.close();
                }
                else
                {
                    env(sponsor::set_reserve(sponsor2, 0, 2), sponsor::sponseeAcc(alice));
                    env.close();
                    env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                        sponsor::as(sponsor2, spfSponsorReserve));
                    env.close();
                }

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 1->2, sponsor1 -> no-sponsor)
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    1,
                    1,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 5)); });

                // reserve 1->2
                env(oracleSet(env, alice, 6));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
            {
                // OracleSet (reserve 1->2, sponsor1 -> sponsor2)
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    1,
                    1,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 5)); });
                // return;

                // reserve 1->2
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor2,
                    alice,
                    1,
                    2,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 6)); },
                    [&]() {
                        BEAST_EXPECT(ownerCount(env, alice) == 2);
                        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);
                    });

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 1->2, non-sponsor -> sponsor1)
                env(oracleSet(env, alice, 5));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);

                // reserve 1->2
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    1,
                    2,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(oracleSet(env, alice, 6)); });

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
            for (bool const isTwoOwnerCount : {false, true})
            {
                // test sponsor transfer
                auto const dataSeriesSize = isTwoOwnerCount ? 6 : 5;
                auto const ocount = isTwoOwnerCount ? 2 : 1;

                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    alice,
                    ocount,
                    ocount,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) {
                        submit(oracleSet(env, alice, dataSeriesSize));
                    });

                // transfer sponsor
                if (cosigning)
                {
                    env(sponsor::transfer(
                            alice, tfSponsorshipReassign, keylet::oracle(alice, 1).key),
                        sponsor::as(sponsor2, spfSponsorReserve),
                        sig(sfSponsorSignature, sponsor2));
                    env.close();
                }
                else
                {
                    env(sponsor::set_reserve(sponsor2, 0, ocount), sponsor::sponseeAcc(alice));
                    env.close();
                    env(sponsor::transfer(
                            alice, tfSponsorshipReassign, keylet::oracle(alice, 1).key),
                        sponsor::as(sponsor2, spfSponsorReserve));
                    env.close();
                }

                BEAST_EXPECT(ownerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == ocount);

                // dissolve sponsor
                env(sponsor::transfer(alice, tfSponsorshipEnd, keylet::oracle(alice, 1).key));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

                // remove sponsor
                env(oracleDelete(alice));
                env.close();
            }
        }
    }

    void
    testSignerList(bool cosigning)
    {
        testcase("SignerList");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        Env env{*this, testable_amendments()};
        env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
        env.close();

        // SignerListSet
        testEachSponsorship(
            env,
            cosigning,
            sponsor,
            alice,
            1,
            1,
            tecINSUFFICIENT_RESERVE,
            [&](Env& env, auto const& submit) { submit(signers(alice, 1, {{bob, 1}})); });

        // transfer sponsor
        if (cosigning)
        {
            // invalid signer list owner 1
            // account doesn't have signer list but specified signer list exists
            env(sponsor::transfer(bob, tfSponsorshipReassign, keylet::signers(alice).key),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2),
                ter(tecNO_PERMISSION));
            // invalid signer list owner 2
            // account has signer list and specified signer list exists
            env(signers(bob, 1, {{alice, 1}}));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(bob).key),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2),
                ter(tecNO_PERMISSION));
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(alice).key),
                sponsor::as(sponsor2, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor2));
            env.close();
        }
        else
        {
            env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(alice));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(alice).key),
                sponsor::as(sponsor2, spfSponsorReserve));
            env.close();
        }

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

        // Delete
        env(signers(alice, none));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
    }

    void
    testTrustSet(bool cosigning)
    {
        testcase("TrustSet");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const charlie("charlie");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const validateSponsoredTrustline =
            [&](std::shared_ptr<const SLE> const& sle, bool isIssuerHigh, Account const& sponsor) {
                BEAST_EXPECT(
                    sle->getAccountID(isIssuerHigh ? sfLowSponsor : sfHighSponsor) == sponsor.id());
                BEAST_EXPECT(!sle->isFieldPresent(isIssuerHigh ? sfHighSponsor : sfLowSponsor));
            };

        auto const& highAcc = alice > bob ? alice : bob;
        auto const& lowAcc = alice > bob ? bob : alice;

        // create and delete
        for (bool const isIssuerHigh : {false, true})
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, charlie, sponsor, sponsor2);
            env.close();

            auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
            auto const& user = isIssuerHigh ? lowAcc : highAcc;

            auto const USD = issuer["USD"];
            auto const currency = USD.currency;

            // create TrustLine
            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
                env(ticket::create(sponsor, 2));  // adjust for free trustline
                env.close();
            }

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                user,
                1,
                1,
                tecNO_LINE_INSUF_RESERVE,
                [&](Env& env, auto const& submit) { submit(trust(user, USD(100))); });

            auto const keylet = keylet::line(user, issuer, currency);

            if (cosigning)
            {
                // invalid owner
                env(sponsor::transfer(charlie, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2),
                    ter(tecNO_PERMISSION));
                // invalid reserve owner
                env(sponsor::transfer(issuer, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2),
                    ter(tecNO_PERMISSION));
                env(sponsor::transfer(user, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::sponseeAcc(user));
                env.close();
                env(sponsor::transfer(user, tfSponsorshipReassign, keylet.key),
                    sponsor::as(sponsor2, spfSponsorReserve));
                env.close();
            }

            // delete TrustLine
            env(trust(user, USD(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, user) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, user) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            BEAST_EXPECT(!env.le(keylet));
        }

        // update
        for (bool const isIssuerHigh : {false, true})
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
            auto const& user = isIssuerHigh ? lowAcc : highAcc;

            auto const USD = issuer["USD"];
            auto const currency = USD.currency;

            // create TrustLine from issuer
            env(trust(issuer, user["USD"](100)));
            env.close();

            BEAST_EXPECT(env.le(keylet::line(user, issuer, currency)));

            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
                env(ticket::create(sponsor, 2));  // adjust for free trustline
                env.close();
            }

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                user,
                1,
                1,
                tecINSUF_RESERVE_LINE,
                [&](Env& env, auto const& submit) { submit(trust(user, USD(100))); });

            auto const line = env.le(keylet::line(user, issuer, currency));
            validateSponsoredTrustline(line, isIssuerHigh, sponsor);

            // update TrustLine from user to clear reserve
            env(trust(user, USD(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, user) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, user) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(env.le(keylet::line(user, issuer, currency)));

            // remove TrustLine from issuer
            env(trust(issuer, user["USD"](0)));
            env.close();
            BEAST_EXPECT(!env.le(keylet::line(user, issuer, currency)));
        }

        // both High and Low sponsored
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // create TrustLines
            env(trust(alice, bob["USD"](100)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();
            env(trust(bob, alice["USD"](100)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            auto sle = env.le(keylet::line(alice, bob, alice["USD"].currency));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->isFlag(lsfHighReserve));
            BEAST_EXPECT(sle->isFlag(lsfLowReserve));
            BEAST_EXPECT(sle->getAccountID(sfHighSponsor) == sponsor.id());
            BEAST_EXPECT(sle->getAccountID(sfLowSponsor) == sponsor.id());

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // clear TrustLines
            env(trust(alice, bob["USD"](0)));
            env.close();
            env(trust(bob, alice["USD"](0)));
            env.close();

            sle = env.le(keylet::line(alice, bob, alice["USD"].currency));
            BEAST_EXPECT(!sle);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
    }

    void
    testVault(bool cosigning)
    {
        testcase("Vault");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        Asset asset = gw["IOU"].asset();

        // VaultCreate
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});

            env(ticket::create(sponsor, 2));
            env.close();

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                3,  // Vault, PseudoAccount, MPToken(Share Token)
                3,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    auto result = vault.create({.owner = alice, .asset = asset});
                    submit(std::get<0>(result));
                    keylet = std::get<1>(result);
                });
            BEAST_EXPECT(env.le(keylet)->getAccountID(sfSponsor) == sponsor.id());
        }
        // VaultDeposit
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            env(trust(bob, asset(1000)));
            env.close();
            env(pay(gw, bob, asset(1000)));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);  // RippleState

            auto const depositTx =
                vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)});

            env(ticket::create(sponsor, 2));  // for free MPToken
            env.close();

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) { submit(depositTx); });
        }
        // VaultWithdraw
        {
            // RippleState Vault
            {
                Env env{*this, testable_amendments()};
                env.fund(XRP(1000000), alice, bob, gw, sponsor);
                env.close();

                Vault const vault{env};
                auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
                env(tx);
                env.close();

                env(trust(bob, asset(100)));
                env.close();
                env(pay(gw, bob, asset(100)));
                env.close();

                auto const depositTx =
                    vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)});

                env(ticket::create(sponsor, 2));  // for free MPToken
                env.close();

                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    bob,
                    1,
                    1,
                    tecINSUFFICIENT_RESERVE,
                    [&](Env& env, auto const& submit) { submit(depositTx); });

                env(trust(bob, asset(0)));  // remove trustline
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);                // MPToken(share)
                BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);       // MPToken(share)
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // MPToken(share)

                // create Trustline with vault withdraw
                testEachSponsorship(
                    env,
                    cosigning,
                    sponsor,
                    bob,
                    1,
                    1,
                    tecNO_LINE_INSUF_RESERVE,
                    [&](Env& env, auto const& submit) {
                        submit(vault.withdraw(
                            {.depositor = bob, .id = keylet.key, .amount = asset(50)}));
                    });

                BEAST_EXPECT(ownerCount(env, bob) == 2);           // RippleState, MPToken(share)
                BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 2);  // RippleState, MPToken(share)
                BEAST_EXPECT(
                    sponsoringOwnerCount(env, sponsor) == 2);  // RippleState, MPToken(share)

                // remove sponsored MPToken(share)
                env(vault.withdraw({.depositor = bob, .id = keylet.key, .amount = asset(50)}));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);                // RippleState
                BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);       // RippleState
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // RippleState
            }
            // MPToken Vault
            {
                // VaultWithdraw doesn't create MPToken for depositor
            }
        }
        // VaultClawback
        {
            // remove sponsored shares MPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            env(trust(bob, asset(100)));
            env.close();
            env(pay(gw, bob, asset(100)));
            env.close();

            auto const depositTx =
                vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)});

            env(ticket::create(sponsor, 2));  // for free MPToken
            env.close();

            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                bob,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) { submit(depositTx); });

            BEAST_EXPECT(ownerCount(env, bob) == 2);                // RippleState, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);       // MPToken(share)
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // MPToken(share)

            env(vault.clawback({.issuer = gw, .id = keylet.key, .holder = bob, .amount = asset(0)}),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);  // RippleState
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
        // VaultDelete
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx, sponsor::as(sponsor, spfSponsorReserve), sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 3);  // Vault, PseudoAccount, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 3);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 3);

            env(vault.del({.owner = alice, .id = keylet.key}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
    }

    void
    testXChain(bool cosigning)
    {
        testcase("XChain");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const doorA("doorA");
        Account const signer("signer");
        Account const sponsor("sponsor");

        Env env{*this, testable_amendments()};
        env.fund(XRP(1000000), alice, bob, sponsor, doorA);
        env.close();

        auto jvb = bridge(doorA, XRP, env.master, XRP);

        env(signers(doorA, 1, {signer}));
        env.close();

        // XChainCreateBridge
        {
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                doorA,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(bridge_create(doorA, jvb, XRP(1), XRP(1)));
                });
        }
        // XChainCreateClaimID
        {
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(xchain_create_claim_id(alice, jvb, XRP(1), bob));
                });
        }
        // XChainCommit
        {
            BEAST_EXPECT(ownerCount(env, alice) == 1);  // XChainOwnedClaimID
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            if (cosigning)
            {
                env(xchain_commit(alice, jvb, 1, XRP(100), bob),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(xchain_commit(alice, jvb, 1, XRP(100), bob),
                    sponsor::as(sponsor, spfSponsorReserve));
                env.close();

                env(sponsor::del(sponsor), sponsor::sponseeAcc(alice));
                env.close();
            }

            // doesn't sponsor anything
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
        }
        // XChainAddClaimAttestation
        {
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            if (cosigning)
            {
                env(claim_attestation(alice, jvb, bob, XRP(1), bob, false, 1, bob, signer),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::sponseeAcc(alice));
                env.close();

                env(claim_attestation(alice, jvb, bob, XRP(1), bob, false, 1, bob, signer),
                    sponsor::as(sponsor, spfSponsorReserve));
                env.close();

                env(sponsor::del(sponsor), sponsor::sponseeAcc(alice));
                env.close();
            }

            // XChainOwnedClaimID deleted
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
        // XChainClaim
        {
            // prepare for claim
            {
                env(xchain_create_claim_id(alice, jvb, XRP(1), bob),
                    sponsor::as(sponsor, spfSponsorReserve),
                    sig(sfSponsorSignature, sponsor));
                env(xchain_commit(alice, jvb, 2, XRP(100)));  // omit destination
                env(claim_attestation(
                    alice, jvb, bob, XRP(100), bob, false, 2, std::nullopt, signer));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            env(xchain_claim(alice, jvb, 2, XRP(100), bob));
            env.close();

            // XChainOwnedClaimID deleted
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
    }

    void
    testLending(bool cosigning)
    {
        testcase("Lending");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const issuer("issuer");
        Account const sponsor("sponsor");

        // LoanBrokerSet / LoanBrokerDelete
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            PrettyAsset const asset{xrpIssue(), 1'000'000};

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            BEAST_EXPECT(
                ownerCount(env, alice) == 3);  // Vault, PseudoAccount(Vault), MPToken(Vault)

            // LoanBrokerSet
            testEachSponsorship(
                // Both the Pseudo-account and LoanBroker objects are created, but only the
                // LoanBroker is sponsored.
                env,
                cosigning,
                sponsor,
                alice,
                2,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(loanBroker::set(alice, keylet.key, 0));
                });

            BEAST_EXPECT(
                ownerCount(env, alice) ==
                5);  // LoanBroker, PseudoAccount(LB), (Vault, PseudoAccount(Vault), MPToken(Vault))
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // LoanBrokerDelete
            auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice) - 1);
            env(loanBroker::del(alice, brokerKeylet.key, 0));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        // LoanBrokerConverDeposit/Withdraw/Clawback
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob, issuer, sponsor);
            env.close();

            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            env.close();
            PrettyAsset const asset = mptt["MPT"];
            mptt.authorize({.account = alice});
            env.close();

            env(pay(issuer, alice, asset(100)));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 1);

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            env(loanBroker::set(alice, keylet.key, 0));
            env.close();
            BEAST_EXPECT(
                ownerCount(env, alice) ==
                6);  // LoanBroker, PseudoAccount(LB), (Vault, PseudoAccount(Vault),
                     // MPToken(Vault), MPToken(issuer))

            auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice) - 1);
            // LoanBrokerCoverDeposit
            // doesn't sponsor anything
            env(loanBroker::coverDeposit(alice, brokerKeylet.key, asset(100)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 6);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // remove MPToken(issuer)
            mptt.authorize({.account = alice, .flags = tfMPTUnauthorize});
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == 5);

            env(ticket::create(sponsor, 2));  // for avoid free MPToken
            env.close();

            // LoanBrokerCoverWithdraw
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(loanBroker::coverWithdraw(alice, brokerKeylet.key, asset(10)));
                });

            BEAST_EXPECT(ownerCount(env, alice) == 6);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // LoanBrokerCoverClawback
            // doesn't sponsor anything
            env(loanBroker::coverClawback(issuer),
                loanBroker::loanBrokerID(brokerKeylet.key),
                amount(asset(1)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 6);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
        // LoanSet
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, issuer, sponsor);
            env.close();

            MPTTester mptt{env, issuer, mptInitNoFund};
            mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            env.close();
            PrettyAsset const asset = mptt["MPT"];
            mptt.authorize({.account = alice});
            mptt.authorize({.account = bob});
            env.close();

            env(pay(issuer, alice, asset(1000)));
            env(pay(issuer, bob, asset(1000)));
            env.close();

            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = bob, .asset = asset});
            env(tx);
            env.close();
            env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = asset(100)}));
            env.close();

            auto const brokerKeylet = keylet::loanbroker(bob.id(), env.seq(bob));
            env(loanBroker::set(bob, keylet.key, 0));
            env.close();
            env(loanBroker::coverDeposit(bob, brokerKeylet.key, asset(100)));
            env.close();

            auto broker = env.le(brokerKeylet);
            BEAST_EXPECT(broker->getFieldU32(sfOwnerCount) == 0);
            BEAST_EXPECT(!broker->isFieldPresent(sfSponsoredOwnerCount));
            BEAST_EXPECT(!broker->isFieldPresent(sfSponsoringOwnerCount));

            auto const loanSeq = broker->getFieldU32(sfLoanSequence);
            testEachSponsorship(
                env,
                cosigning,
                sponsor,
                alice,
                1,
                1,
                tecINSUFFICIENT_RESERVE,
                [&](Env& env, auto const& submit) {
                    submit(
                        loan::set(alice, brokerKeylet.key, 10),
                        sig(sfCounterpartySignature, bob),
                        fee(XRP(1)));
                });
            broker = env.le(brokerKeylet);
            // broker'object doesn't sponsored
            BEAST_EXPECT(broker->getFieldU32(sfOwnerCount) == 1);
            BEAST_EXPECT(!broker->isFieldPresent(sfSponsoredOwnerCount));
            BEAST_EXPECT(!broker->isFieldPresent(sfSponsoringOwnerCount));

            auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSeq);

            auto sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfOwnerCount) == 0);
            BEAST_EXPECT(!sponsorSle->isFieldPresent(sfSponsoredOwnerCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringOwnerCount) == 1);

            // LoanManage
            env(loan::manage(bob, loanKeylet.key, lsfLoanImpaired),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            // doesn't sponsor anything
            sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfOwnerCount) == 0);
            BEAST_EXPECT(!sponsorSle->isFieldPresent(sfSponsoredOwnerCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringOwnerCount) == 1);

            // LoanPay
            env(loan::pay(alice, loanKeylet.key, asset(10)),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            // doesn't sponsor anything
            sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfOwnerCount) == 0);
            BEAST_EXPECT(!sponsorSle->isFieldPresent(sfSponsoredOwnerCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringOwnerCount) == 1);

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);

            // LoanDelete
            env(loan::del(alice, loanKeylet.key),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            // Sponsored ltLoan is deleted
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            // Sponsor for ltLoan object is deleted
            sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfOwnerCount) == 0);
            BEAST_EXPECT(!sponsorSle->isFieldPresent(sfSponsoredOwnerCount));
        }
    }

    void
    testAccountDelete()
    {
        testcase("AccountDelete");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        {
            // Delete Sponsor/Sponsee Account with ltSponsorship (tecHAS_OBLIGATIONS)
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // set sponsor
            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            incLgrSeqForAccDel(env, sponsor);

            auto const keylet = keylet::sponsor(sponsor, alice);
            auto const sponsorObj = env.le(keylet);
            BEAST_EXPECT(sponsorObj);

            // AccountDelete
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob), fee(requiredFee), ter(tecHAS_OBLIGATIONS));
            env(acctdelete(sponsor, bob), fee(requiredFee), ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete SponsoredAccount
            Env env{*this, testable_amendments()};
            env.memoize(alice);
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            // create SponsoredAccount
            env(pay(sponsor, alice, XRP(10000)), txflags(tfSponsorCreatedAccount));
            env.close();

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = non-sponsor
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob), fee(requiredFee), ter(tecNO_SPONSOR_PERMISSION));

            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringAccountCount) == 1);

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = sponsor
            env(acctdelete(alice, sponsor), fee(requiredFee), ter(tesSUCCESS));

            auto const sponsorSle2 = env.le(keylet::account(sponsor));
            BEAST_EXPECT(!sponsorSle2->isFieldPresent(sfSponsoringAccountCount));
        }

        {
            // Sponsor with sfSponsoringOwnerCount cannot delete (tecHAS_OBLIGATIONS)
            Env env{*this, testable_amendments()};
            Account const gw("gw");
            env.fund(XRP(1000000), alice, bob, sponsor, gw);
            env.close();

            auto const USD = gw["USD"];

            // Create sponsorship allowing reserve sponsoring
            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            // Create a trust line for alice
            env(trust(alice, USD(1000)));
            env.close();

            // Transfer reserve sponsorship of trust line to sponsor
            auto const trustId = keylet::line(alice, gw, USD.currency);
            BEAST_EXPECT(env.le(trustId));

            env(sponsor::transfer(alice, tfSponsorshipCreate, trustId.key),
                sponsor::as(sponsor, spfSponsorReserve),
                sig(sfSponsorSignature, sponsor));
            env.close();

            // Verify sfSponsoringOwnerCount is set on sponsor
            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->isFieldPresent(sfSponsoringOwnerCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringOwnerCount) >= 1);

            incLgrSeqForAccDel(env, sponsor);

            // AccountDelete should fail
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(sponsor, bob), fee(requiredFee), ter(tecHAS_OBLIGATIONS));
        }

        {
            // Sponsor with sfSponsoringAccountCount cannot delete (tecHAS_OBLIGATIONS)
            Env env{*this, testable_amendments()};
            env.memoize(alice);
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            // Create SponsoredAccount (sets sfSponsoringAccountCount on sponsor)
            env(pay(sponsor, alice, XRP(10000)), txflags(tfSponsorCreatedAccount));
            env.close();

            // Verify sfSponsoringAccountCount is set on sponsor
            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->isFieldPresent(sfSponsoringAccountCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringAccountCount) == 1);

            incLgrSeqForAccDel(env, sponsor);

            // AccountDelete should fail
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(sponsor, bob), fee(requiredFee), ter(tecHAS_OBLIGATIONS));
        }
    }

    void
    testDelegatePermission()
    {
        testcase("DelegatePermission");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        //
        // SponsorshipTransfer
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const keylet = keylet::check(alice, seq);

            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet.key),
                sponsor::as(bob, spfSponsorReserve),
                sig(sfSponsorSignature, bob),
                delegate::as(carol),
                ter(terNO_DELEGATE_PERMISSION));

            env(delegate::set(alice, carol, {"SponsorshipTransfer"}));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet.key),
                sponsor::as(bob, spfSponsorReserve),
                sig(sfSponsorSignature, bob),
                delegate::as(carol),
                ter(tesSUCCESS));
            env.close();
        }
        //
        // SponsorshipSet
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::sponseeAcc(bob),
                delegate::as(carol),
                ter(terNO_DELEGATE_PERMISSION));

            env(delegate::set(alice, carol, {"SponsorshipSet"}));
            env.close();

            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::sponseeAcc(bob),
                delegate::as(carol),
                ter(tesSUCCESS));
            env.close();
        }

        //
        // Permission SponsorFee
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();
            auto const testFeePermission = [&](TER result) {
                // FeeAmount
                env(sponsor::set(alice, 0, std::nullopt, XRP(100)),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // MaxFee
                env(sponsor::set(alice, 0, std::nullopt, std::nullopt, XRP(100)),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // SetRequireSignForFee flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForFee),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // ClearRequireSignForFee flag
                env(sponsor::set(alice, tfSponsorshipClearRequireSignForFee),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                env.close();
            };

            // no delegated
            testFeePermission(terNO_DELEGATE_PERMISSION);

            // set non-SponsorFee Permission
            env(delegate::set(alice, carol, {"SponsorReserve"}));
            env.close();

            testFeePermission(terNO_DELEGATE_PERMISSION);

            // set SponsorFee Permission
            env(delegate::set(alice, carol, {"SponsorFee"}));
            env.close();

            testFeePermission(tesSUCCESS);

            // test with SponsorReserve (should failed)
            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::sponseeAcc(bob),
                delegate::as(carol),
                ter(terNO_DELEGATE_PERMISSION));
        }

        //
        // Permission SponsorReserve
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            auto const testReservePermission = [&](TER result) {
                // ReserveCount
                env(sponsor::set(alice, 0, 100),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // SetRequireSignForReserve flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForReserve),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // ClearRequireSignForReserve flag
                env(sponsor::set(alice, tfSponsorshipClearRequireSignForReserve),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                env.close();
            };

            // no delegated
            testReservePermission(terNO_DELEGATE_PERMISSION);

            // set non-SponsorReserve Permission
            env(delegate::set(alice, carol, {"SponsorFee"}));
            env.close();

            testReservePermission(terNO_DELEGATE_PERMISSION);

            // set SponsorReserve Permission
            env(delegate::set(alice, carol, {"SponsorReserve"}));
            env.close();

            testReservePermission(tesSUCCESS);

            // test with SponsorFee (should failed)
            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::sponseeAcc(bob),
                delegate::as(carol),
                ter(terNO_DELEGATE_PERMISSION));
        }
    }

    void
    testBatch()
    {
        testcase("Batch");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        //
        // Outer transaction
        //
        {
            // test outer transaction with co-signing sponsor
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::inner(noop(alice), seq + 1),
                batch::inner(ticket::create(alice, 1), seq + 2),
                sponsor::as(sponsor, spfSponsorFee),
                sig(sfSponsorSignature, sponsor),
                ter(tesSUCCESS));
            env.close();

            // does not affect reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // fee is paid by sponsor
            BEAST_EXPECT(env.balance(alice) == XRP(1000));
            BEAST_EXPECT(env.balance(sponsor) == XRP(1000 - 1));
        }
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            // spfSponsorReserve on outer Batch is rejected
            for (auto const flags : {spfSponsorReserve | spfSponsorFee, spfSponsorReserve})
            {
                auto const seq = env.seq(alice);
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::inner(noop(alice), seq + 1),
                    batch::inner(noop(alice), seq + 2),
                    sponsor::as(sponsor, flags),
                    sig(sfSponsorSignature, sponsor),
                    ter(temINVALID_FLAG));
                env.close();
            }
        }
        {
            // test outer transaction with prefunded sponsor
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob);
            env.fund(XRP(1001), sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::inner(noop(alice), seq + 1),
                batch::inner(ticket::create(alice, 1), seq + 2),
                sponsor::as(sponsor, spfSponsorFee),
                ter(tesSUCCESS));
            env.close();

            // does not affect reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // fee is paid by sponsor object
            BEAST_EXPECT(env.balance(alice) == XRP(1000));
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            auto const sponsorshipSle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sponsorshipSle);
            BEAST_EXPECT(sponsorshipSle->at(sfFeeAmount) == XRP(100 - 1));
            BEAST_EXPECT(sponsorshipSle->at(sfReserveCount) == 100);
        }
        //
        // Inner transaction
        //
        {
            // test invalid inner transaction with co-signing sponsor
            Account const signerAccount("signer");
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob, sponsor, signerAccount);
            env.close();

            env(signers(sponsor, 1, {signer(signerAccount, 1)}));
            env.close();

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                    sig(sfSponsorSignature, sponsor));
                jt.jv.removeMember(sfTxnSignature.jsonName);

                auto const seq = env.seq(alice);
                // should fail because inner transaction cannot include SponsorSignature with
                // TxnSignature
                BEAST_EXPECT(jt.jv[sfSponsorSignature.jsonName].isMember(sfTxnSignature.jsonName));
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::inner(jt.jv, seq + 1),
                    batch::inner(ticket::create(alice, 1), seq + 2),
                    ter(temBAD_SIGNATURE));
            }

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                    msig(sfSponsorSignature, sponsor, signerAccount));
                jt.jv.removeMember(sfTxnSignature.jsonName);

                auto const seq = env.seq(alice);
                // should fail because inner transaction cannot include SponsorSignature with
                // Signers
                BEAST_EXPECT(jt.jv[sfSponsorSignature.jsonName].isMember(sfSigners.jsonName));
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::inner(jt.jv, seq + 1),
                    batch::inner(ticket::create(alice, 1), seq + 2),
                    ter(temBAD_SIGNER));
            }

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                    sig(sfSponsorSignature, sponsor));
                jt.jv.removeMember(sfTxnSignature.jsonName);
                jt.jv[sfSponsorSignature.jsonName].removeMember(sfTxnSignature.jsonName);
                jt.jv[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = "";

                auto const seq = env.seq(alice);
                // should fail BatchSigners does have signer for SponsorSignature
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::inner(jt.jv, seq + 1),
                    batch::inner(ticket::create(alice, 1), seq + 2),
                    ter(temBAD_SIGNER));
            }
        }

        {
            // test inner transaction with prefunded sponsor
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob);
            env.fund(XRP(1001), sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::sponseeAcc(alice),
                fee(XRP(1)),
                ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            auto jt = env.jtnofill(
                ticket::create(alice, 1), sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee));
            // remove txn signature since it is filled by env.jtnofill()
            jt.jv.removeMember(jss::TxnSignature);

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::inner(noop(alice), seq + 1),
                batch::inner(jt.jv, seq + 2),
                ter(tesSUCCESS));
            env.close();

            // affect sponsor reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // fee is paid by outer transaction originator (alice)
            BEAST_EXPECT(env.balance(alice) == XRP(999));
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            // reserve count is decreased
            auto const sponsorshipSle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sponsorshipSle);
            BEAST_EXPECT(sponsorshipSle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sponsorshipSle->at(sfReserveCount) == 99);
        }

        {
            // test inner transaction with co-signing sponsor
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            auto jt = env.jtnofill(
                ticket::create(alice, 1),
                sponsor::as(sponsor, spfSponsorReserve | spfSponsorFee),
                sig(sfSponsorSignature, sponsor));
            // remove txn signature since it is filled by env.jtnofill()
            jt.jv.removeMember(sfTxnSignature.jsonName);
            jt.jv[sfSponsorSignature.jsonName].removeMember(sfTxnSignature.jsonName);
            jt.jv[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = "";

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::inner(noop(alice), seq + 1),
                batch::inner(jt.jv, seq + 2),
                batch::sig(sponsor),
                ter(tesSUCCESS));
            env.close();

            // affect sponsor reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // fee is paid by outer transaction originator (alice)
            BEAST_EXPECT(env.balance(alice) == XRP(999));
            BEAST_EXPECT(env.balance(sponsor) == XRP(1000));
        }
    }

    void
    testSponsorReserve(bool cosigning)
    {
        testRequireFlag();
        testSponsorReserveSimple(cosigning);
        testAMM(cosigning);
        testCheck(cosigning);
        testOffer(cosigning);
        testTicket(cosigning);
        testCredentials(cosigning);
        testDelegate(cosigning);
        testDepositPreauth(cosigning);
        testDID(cosigning);
        testEscrow(cosigning);
        testMPToken(cosigning);
        testNFToken(cosigning);
        testNFTokenOffer(cosigning);
        testPayChan(cosigning);
        testPermissionedDomain(cosigning);
        testOracle(cosigning);
        testSignerList(cosigning);
        testTrustSet(cosigning);
        testVault(cosigning);
        testXChain(cosigning);
        testLending(cosigning);
    }

protected:
    void
    testSponsor()
    {
        testDisabled();
        testInvalidSponsorshipSet();
        testPseudoAccountSponsorship();

        testSingleSigning();
        testMultiSigning();

        testInvalidSponsorField();

        testSimpleSponsorshipSet();

        testPreFundAndCosign();

        testTransferSponsor();
        testSponsorFee();
        testSponsorAccount();

        testAccountDelete();

        testDelegatePermission();
        testBatch();
    }

    void
    testTxSponsor(bool cosigning)
    {
        testSponsorReserve(cosigning);
    }

public:
    void
    run() override
    {
        testSponsor();
    }
};

class SponsorTxCosigning_test : public Sponsor_test
{
    void
    run() override
    {
        testTxSponsor(true);
    }
};

class SponsorTxPrefunded_test : public Sponsor_test
{
    void
    run() override
    {
        testTxSponsor(false);
    }
};

BEAST_DEFINE_TESTSUITE(Sponsor, app, xrpl);
BEAST_DEFINE_TESTSUITE(SponsorTxCosigning, app, xrpl);
BEAST_DEFINE_TESTSUITE(SponsorTxPrefunded, app, xrpl);

}  // namespace test
}  // namespace xrpl
