#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/check.h>
#include <test/jtx/delegate.h>
#include <test/jtx/deposit.h>
#include <test/jtx/did.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

static STAmount
accountReserve(jtx::Env& env, std::uint32_t count = 1)
{
    return env.current()->fees().reserve * count;
}

static STAmount
reserve(jtx::Env& env, std::uint32_t count)
{
    return baseAccountReserve(*env.current(), count);
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
    {
        env(pay(account, env.master, currentBalance - (balanceTo)),
            Fee(XRP(1)),
            sponsor::As(env.master, spfSponsorFee),
            Sig(sfSponsorSignature, env.master));
    }
    else
    {
        env(pay(env.master, account, balanceTo - currentBalance), Fee(baseFee));
    }

    env.close();
}

class Sponsor_test : public beast::unit_test::Suite
{
public:
    void
    testDisabled()
    {
        testcase("Disabled");
        using namespace test::jtx;
        Env env{*this, testableAmendments() - featureSponsor};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);

        // check Sponsor fields
        auto const jt = noop(alice);
        auto jt1 = jt;
        jt1[sfSponsor.jsonName] = sponsor.human();
        env(jt1, Ter(temDISABLED));
        env(jt, Sig(sfSponsorSignature, sponsor), Ter(temDISABLED));

        auto jt2 = jt;
        jt2[sfSponsorFlags.jsonName] = spfSponsorFee | spfSponsorReserve;
        env(jt2, Ter(temDISABLED));

        // check Sponsor transactions
        env(sponsor::transfer(alice, 0), Ter(temDISABLED));
        env(sponsor::set(sponsor, 0), Ter(temDISABLED));
    }

    void
    testInvalidSponsorshipSet()
    {
        testcase("Invalid SponsorshipSet");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const noFunded("noFunded");
        Account const gw("gw");

        auto const usd = gw["usd"];
        env.fund(XRP(10000), alice, sponsor, gw);
        env.close();

        //
        // preflight
        //

        // Invalid flags
        {
            env(sponsor::set(sponsor, ~tfSponsorshipSetMask - tfInnerBatchTxn),
                sponsor::SponseeAcc(alice),
                Ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee | tfSponsorshipClearRequireSignForFee),
                sponsor::SponseeAcc(alice),
                Ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForReserve |
                        tfSponsorshipClearRequireSignForReserve),
                sponsor::SponseeAcc(alice),
                Ter(temINVALID_FLAG));

            for (auto flag :
                 {tfSponsorshipSetRequireSignForFee,
                  tfSponsorshipClearRequireSignForFee,
                  tfSponsorshipSetRequireSignForReserve,
                  tfSponsorshipClearRequireSignForReserve})
            {
                env(sponsor::set(sponsor, tfDeleteObject | flag),
                    sponsor::SponseeAcc(alice),
                    Ter(temINVALID_FLAG));
            }
        }

        // invalid SponsorAccount / Sponsee
        // Account = Sponsor
        env(sponsor::set(alice, tfDeleteObject),
            sponsor::CounterpartySponsor(alice),
            Ter(temMALFORMED));
        // Account = Sponsee
        env(sponsor::set(alice, tfDeleteObject), sponsor::SponseeAcc(alice), Ter(temMALFORMED));
        // Both Sponsor and Sponsee are specified
        env(sponsor::set(alice, 0),
            sponsor::CounterpartySponsor(sponsor),
            sponsor::SponseeAcc(alice),
            Ter(temMALFORMED));

        // Invalid feeAmount
        for (auto const& amt : {XRP(-1), usd(1)})
        {
            env(sponsor::set_fee(sponsor, 0, amt), sponsor::SponseeAcc(alice), Ter(temBAD_AMOUNT));
        }
        // Invalid MaxFee
        for (auto const& amt : {XRP(-1), usd(1)})
        {
            env(sponsor::set_fee(sponsor, 0, XRP(1), amt),
                sponsor::SponseeAcc(alice),
                Ter(temBAD_AMOUNT));
        }

        // Invalid Delete operation
        env(sponsor::set_reserve(sponsor, tfDeleteObject, 1),
            sponsor::SponseeAcc(alice),
            Ter(temMALFORMED));
        env(sponsor::set_fee(sponsor, tfDeleteObject, XRP(1)),
            sponsor::SponseeAcc(alice),
            Ter(temMALFORMED));
        env(sponsor::set_max_fee(sponsor, tfDeleteObject, XRP(1)),
            sponsor::SponseeAcc(alice),
            Ter(temMALFORMED));

        // Invalid SponsorAccount with non-Delete operation
        env(sponsor::set_reserve(sponsor, 0, 100),
            sponsor::CounterpartySponsor(alice),
            Ter(temMALFORMED));
        env(sponsor::set_fee(sponsor, 0, XRP(1), XRP(1)),
            sponsor::CounterpartySponsor(alice),
            Ter(temMALFORMED));

        //
        // preclaim
        //

        // Invalid Sponsee
        env(sponsor::set(sponsor, 0), sponsor::SponseeAcc(noFunded), Ter(tecNO_DST));
        env.close();

        // Invalid Sponsor
        env(sponsor::set(sponsor, tfDeleteObject),
            sponsor::CounterpartySponsor(noFunded),
            Ter(tecNO_DST));
        env.close();

        // Invalid Delete operation (sponsorship not found)
        env(sponsor::set(sponsor, tfDeleteObject), sponsor::SponseeAcc(alice), Ter(tecNO_ENTRY));
        env.close();

        // insufficient balance to sponsor Fee
        adjustAccountXRPBalance(env, sponsor, env.current()->fees().reserve);
        env(sponsor::set_fee(sponsor, 0, XRP(4)), sponsor::SponseeAcc(alice), Ter(tecUNFUNDED));
        env.close();

        // insufficent reserve to create sponsorship
        adjustAccountXRPBalance(env, sponsor, XRP(100) + XRP(1) + reserve(env, 1) - drops(1));
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tecUNFUNDED));
        env.close();

        //  FeeAmount + Fee > Balance
        /// Balance = 1000XRP, FeeAmount = 1001XRP
        adjustAccountXRPBalance(env, sponsor, XRP(1000));
        env(sponsor::set_fee(sponsor, 0, XRP(1001)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tecUNFUNDED));
        env.close();
        /// Balance = 1000XRP, FeeAmount = 999XRP, Fee=2XRP
        adjustAccountXRPBalance(env, sponsor, XRP(1000));
        env(sponsor::set_fee(sponsor, 0, XRP(999)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(2)),
            Ter(tecUNFUNDED));
        env.close();

        // create sponsor to use above tests
        // need feeAmount(1000) + Fee(1) + reserve(~250) = ~1251
        adjustAccountXRPBalance(env, sponsor, XRP(1000) + XRP(1) + reserve(env, 1));
        env(sponsor::set(sponsor, 0, 100, XRP(1000)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tesSUCCESS));
        env.close();

        // delta-based balance check
        // After create: sponsor balance ~ 0, feeAmount = XRP(1000)

        // Decreasing feeAmount should succeed (refund, negative delta)
        adjustAccountXRPBalance(env, sponsor, XRP(500));
        env(sponsor::set_fee(sponsor, 0, XRP(800)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tesSUCCESS));
        env.close();
        // balance was 500, delta = 800-1000 = -200 (refund), balance = 500+200-1 = 699

        // Increasing feeAmount within delta budget should succeed
        adjustAccountXRPBalance(env, sponsor, XRP(500));
        env(sponsor::set_fee(sponsor, 0, XRP(850)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tesSUCCESS));
        env.close();
        // balance was 500, delta = 850-800 = 50, balance = 500-50-1 = 449

        // Increasing feeAmount where delta exceeds balance should fail
        adjustAccountXRPBalance(env, sponsor, XRP(310));
        env(sponsor::set_fee(sponsor, 0, XRP(1200)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tecUNFUNDED));
        env.close();

        // Increasing feeAmount to reach insufficient reserve
        auto const currentFeeAmount = env.le(keylet::sponsorship(sponsor.id(), alice.id()))
                                          ->getFieldAmount(sfFeeAmount)
                                          .xrp();
        adjustAccountXRPBalance(env, sponsor, XRP(310));
        env(sponsor::set_fee(sponsor, 0, currentFeeAmount + XRP(309)),
            sponsor::SponseeAcc(alice),
            Fee(XRP(1)),
            Ter(tecUNFUNDED));
        env.close();
    }

    void
    testPseudoAccountSponsorship()
    {
        testcase("Pseudo account sponsorship");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
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
            sponsor::SponseeAcc(pseudoAcc),
            Ter(tecNO_PERMISSION));
        env.close();

        // Sponsor is a pseudo account -> tecNO_PERMISSION
        // (submitted by bob with counterpartySponsor pointing to pseudo account)
        env(sponsor::set(bob, tfDeleteObject),
            sponsor::CounterpartySponsor(pseudoAcc),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testSingleSigning()
    {
        testcase("Single signing");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Signature doesn't exist
        auto tx = noop(alice);
        tx[sfSponsor.jsonName] = sponsor.human();
        tx[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = strHex(sponsor.pk().slice());

        env(tx, Fee(XRP(1)), sponsor::As(sponsor, spfSponsorReserve), Ter(telENV_RPC_FAILED));

        // Invalid signature
        tx[sfSponsorSignature.jsonName][sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx, Fee(XRP(1)), sponsor::As(sponsor, spfSponsorReserve), Ter(telENV_RPC_FAILED));

        // Signer account doesn't exist
        env(noop(alice),
            Fee(XRP(1)),
            sponsor::As(invalid, spfSponsorReserve),
            Sig(sfSponsorSignature, invalid),
            Ter(terNO_ACCOUNT));

        // Success
        env(noop(alice),
            Fee(XRP(1)),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tesSUCCESS));
    }

    void
    testMultiSigning()
    {
        testcase("Multi signing");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        Account const signer1("signer1");
        Account const signer2("signer2");

        env.fund(XRP(10000), alice, bob, sponsor);
        env.close();

        env(signers(sponsor, 1, {{signer1, 1}, {signer2, 1}}));
        env.close();

        // Invalid signature
        auto tx = noop(alice);
        auto& signers1 = tx[sfSponsorSignature.jsonName][sfSigners.jsonName][0U][sfSigner.jsonName];
        signers1[sfAccount.jsonName] = signer1.human();
        signers1[sfSigningPubKey.jsonName] = strHex(signer1.pk().slice());
        signers1[sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx, Fee(XRP(1)), sponsor::As(sponsor, spfSponsorReserve), Ter(telENV_RPC_FAILED));

        // bob is not a multi-signing account.
        env(noop(alice),
            Fee(XRP(1)),
            sponsor::As(bob, spfSponsorReserve),
            Msig(sfSponsorSignature, {signer1}),
            Ter(tefNOT_MULTI_SIGNING));

        env(noop(alice),
            Fee(XRP(1)),
            sponsor::As(sponsor, spfSponsorReserve),
            Msig(sfSponsorSignature, {signer1}),
            Ter(tesSUCCESS));
        env.close();

        env(signers(sponsor, 2, {{signer1, 1}, {signer2, 1}}));
        env.close();

        // test calculateBaseFee for multisigned sponsor
        auto const baseFee = env.current()->fees().base;
        env(noop(alice),
            Fee(baseFee + 2 * baseFee - 1),
            sponsor::As(sponsor, spfSponsorReserve),
            Msig(sfSponsorSignature, {signer1, signer2}),
            Ter(telINSUF_FEE_P));

        env(noop(alice),
            Fee(baseFee + 2 * baseFee),
            sponsor::As(sponsor, spfSponsorReserve),
            Msig(sfSponsorSignature, {signer1, signer2}),
            Ter(tesSUCCESS));
    }

    void
    testInvalidSponsorField()
    {
        testcase("Invalid Sponsor Field");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const noFunded("noFunded");
        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Invalid Sponsor Account (Account = Sponsor.Account)
        env(noop(alice), sponsor::As(alice, spfSponsorFee), Ter(temMALFORMED));

        // Invalid Sponsor Account
        // (SponsorSignature is specified but Sponsor.Account is not specified)
        env(noop(alice), Sig(sfSponsorSignature, sponsor), Ter(temMALFORMED));

        // Invalid Sponsor Account (Sponsor.Account doesn't exist)
        env(noop(alice), sponsor::As(noFunded, spfSponsorReserve), Ter(terNO_ACCOUNT));
        env(noop(alice),
            sponsor::As(noFunded, spfSponsorReserve),
            Sig(sfSponsorSignature, noFunded),
            Ter(terNO_ACCOUNT));

        // Invalid Flags
        env(noop(alice),
            sponsor::As(sponsor, (spfSponsorFee | spfSponsorReserve) + 1),
            Ter(temINVALID_FLAG));

        // SponsorFlags=0 with valid sponsor (no sponsorship purpose)
        env(noop(alice), sponsor::As(sponsor, 0), Ter(temINVALID_FLAG));

        // no SponsorFlag with valid sponsor
        auto tx = noop(alice);
        tx[sfSponsor.jsonName] = sponsor.human();
        env(tx, Ter(temINVALID_FLAG));

        // Invalid Flags without sponsor
        tx = noop(alice);
        tx[sfSponsorFlags.jsonName] = spfSponsorFee | spfSponsorReserve;
        env(tx, Ter(temINVALID_FLAG));
    }

    void
    testSimpleSponsorshipSet()
    {
        testcase("Simple SponsorshipSet");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
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
                Fee(XRP(1)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            auto sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 100);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(1));
            BEAST_EXPECT(sle->isFlag(lsfSponsorshipRequireSignForFee));
            BEAST_EXPECT(sle->isFlag(lsfSponsorshipRequireSignForReserve));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(1));

            // update sponsorship (decrement)
            env(sponsor::set(sponsor, 0, 50, XRP(50), XRP(0.5)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 50);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(50));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(0.5));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(2));

            // update sponsorship (increment)
            env(sponsor::set(sponsor, 0, 200, XRP(200), XRP(2)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 200);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(200));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(2));
            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - sle->at(sfFeeAmount) - XRP(3));

            // delete from sponsor
            env(sponsor::del(sponsor), sponsor::SponseeAcc(alice), Fee(XRP(1)), Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(sponsor) == XRP(10000) - XRP(4));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee | tfSponsorshipSetRequireSignForReserve,
                    100,
                    XRP(100),
                    XRP(1)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            // delete from sponsee
            env(sponsor::del(alice), sponsor::CounterpartySponsor(sponsor), Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(!env.le(keylet::sponsorship(sponsor, alice)));

            // create sponsorship with zero value
            env(sponsor::set(sponsor, 0, 0, XRP(0), XRP(0)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(!sle->isFieldPresent(sfRemainingOwnerCount));
            BEAST_EXPECT(!sle->isFieldPresent(sfFeeAmount));
            BEAST_EXPECT(!sle->isFieldPresent(sfMaxFee));
            // verify flags from previous sponsorship are not carried over
            BEAST_EXPECT(!sle->isFlag(lsfSponsorshipRequireSignForFee));
            BEAST_EXPECT(!sle->isFlag(lsfSponsorshipRequireSignForReserve));

            // update sponsorship with non-zero value
            env(sponsor::set(sponsor, 0, 100, XRP(100), XRP(1)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 100);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sle->at(sfMaxFee) == XRP(1));

            // update sponsorship with zero value
            env(sponsor::set(sponsor, 0, 0, XRP(0), XRP(0)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(!sle->isFieldPresent(sfRemainingOwnerCount));
            BEAST_EXPECT(!sle->isFieldPresent(sfFeeAmount));
            BEAST_EXPECT(!sle->isFieldPresent(sfMaxFee));
        }

        {
            // Update Sponsorship (FeeAmount)
            // set empty FeeAmount
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::SponseeAcc(alice), Ter(tesSUCCESS));
            env.close();

            // add FeeAmount
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::CounterpartySponsor(sponsor), Ter(tesSUCCESS));
            env.close();
        }
        {
            // Update Sponsorship (ReserveCount)
            // set empty ReserveCount
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            // add ReserveCount
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::SponseeAcc(alice), Ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::CounterpartySponsor(sponsor), Ter(tesSUCCESS));
            env.close();
        }
        {
            // delete Sponsorship (only with FeeAmount)
            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::CounterpartySponsor(sponsor), Ter(tesSUCCESS));
            env.close();
        }
        {
            // delete Sponsorship (only with ReserveCount)
            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::SponseeAcc(alice), Ter(tesSUCCESS));
            env.close();

            env(sponsor::del(alice), sponsor::CounterpartySponsor(sponsor), Ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testPreFundAndCosign()
    {
        testcase("PreFund and Cosign");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        {
            // both pre-funded and co-signed,pre-funded value is used
            Env env{*this, testableAmendments()};
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100), XRP(1)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            auto const checkSeq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)),
                sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();

            auto sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 99);
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(99));

            env(check::cancel(alice, keylet::check(alice, checkSeq).key), Ter(tesSUCCESS));
            env.close();

            sle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->at(sfRemainingOwnerCount) == 99);  // not paybacked
            BEAST_EXPECT(sle->at(sfFeeAmount) == XRP(99));
        }

        {
            // if pre-funded value is not enough, error
            Env env{*this, testableAmendments()};
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 10, XRP(10), XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            // Fee insufficient
            env(check::create(alice, bob, XRP(1)),
                sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Fee(XRP(11)),
                Ter(terINSUF_FEE_B));
            env.close();

            env(sponsor::set_reserve(sponsor, 0, 0), sponsor::SponseeAcc(alice), Ter(tesSUCCESS));
            env.close();

            // reserve insufficient
            env(check::create(alice, bob, XRP(1)),
                sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Fee(XRP(1)),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testSponsoredFreeTierReserve()
    {
        testcase("Sponsored Free-Tier Reserve");
        using namespace test::jtx;
        Account const alice("alice");
        Account const issuer("issuer");
        Account const sponsor("sponsor");

        // Trust lines and MPTokens normally skip the reserve check when the
        // holder's ownerCount < 2 (the "free-tier" / first-two-items shortcut). When the
        // tx is sponsored, that shortcut must not apply — the sponsor must
        // still cover the reserve.
        Env env{*this, testableAmendments()};
        env.fund(XRP(10000), alice, issuer);
        // Sponsor is funded just below the reserve required to cover a single
        // sponsored item.
        env.fund(reserve(env, 1) - drops(1), sponsor);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        MPTTester mptt(env, issuer, {.fund = false});
        mptt.create();

        // Free-tier trust line cosigned by an undercapitalized sponsor must
        // fail — the holder's free-first-two-items shortcut does not let the
        // sponsor skip the reserve check.
        env(trust(alice, issuer["USD"](100)),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tecNO_LINE_INSUF_RESERVE));
        env.close();

        // Free-tier MPTokenAuthorize must also fail for the same reason.
        env(MPTTester::authorizeJV({.account = alice, .id = mptt.issuanceID()}),
            sponsor::As(sponsor, spfSponsorReserve),
            Sig(sfSponsorSignature, sponsor),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();
    }

    void
    testTransferSponsor()
    {
        testcase("Transfer Sponsor");
        using namespace test::jtx;

        {
            // invalid fields
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);
            env.close();

            env(sponsor::transfer(
                    alice, (tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd) + 1),
                Ter(temINVALID_FLAG));

            // invalid combination of flags
            for (auto flag : {
                     tfSponsorshipCreate | tfSponsorshipReassign,
                     tfSponsorshipCreate | tfSponsorshipEnd,
                     tfSponsorshipReassign | tfSponsorshipEnd,
                     tfSponsorshipCreate | tfSponsorshipReassign | tfSponsorshipEnd,
                 })
                env(sponsor::transfer(alice, flag), Ter(temINVALID_FLAG));

            // invalid tfSponsorshipCreate
            // no sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipCreate), Ter(temINVALID_FLAG));
            // sponsee field present
            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::SponseeAcc(bob),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(temMALFORMED));

            // invalid tfSponsorshipReassign
            // no sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipReassign), Ter(temINVALID_FLAG));
            // sponsee field present
            env(sponsor::transfer(alice, tfSponsorshipReassign),
                sponsor::SponseeAcc(bob),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(temMALFORMED));

            // invalid tfSponsorshipEnd
            // sponsor field present
            env(sponsor::transfer(alice, tfSponsorshipEnd),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(temINVALID_FLAG));
            // account = sponsee
            env(sponsor::transfer(alice, tfSponsorshipEnd),
                sponsor::SponseeAcc(alice),
                Ter(temMALFORMED));
        }

        {
            // Invalid SponsorshipEnd permission (sponsor object/sponsor account)
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            {
                // sponsor object
                env(did::set(alice),
                    did::Uri("uri"),
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor));
                env.close();

                auto const keylet = keylet::did(alice);
                env(sponsor::transfer(bob, tfSponsorshipEnd, keylet.key),
                    sponsor::SponseeAcc(alice),
                    Ter(tecNO_PERMISSION));
            }
            {
                // sponsor object
                env(sponsor::transfer(alice, tfSponsorshipCreate),
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor));
                env.close();

                env(sponsor::transfer(bob, tfSponsorshipEnd),
                    sponsor::SponseeAcc(alice),
                    Ter(tecNO_PERMISSION));
            }
        }

        {
            // sponsor account
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);

            // sfSponsor provided but sfSponsorSignature not provided
            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(temMALFORMED));
            env.close();

            adjustAccountXRPBalance(env, sponsor1, accountReserve(env, 2) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor1, accountReserve(env, 2));

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1));
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
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor2, accountReserve(env, 2));

            env(sponsor::transfer(alice, tfSponsorshipReassign),
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2));
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
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2));
            env.close();

            // dissolve sponsors
            adjustAccountXRPBalance(env, alice, accountReserve(env, 1) - drops(1));

            env(sponsor::transfer(alice, tfSponsorshipEnd), Ter(tecINSUFFICIENT_RESERVE));
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
            env(sponsor::transfer(bob, tfSponsorshipEnd), Ter(tecNO_PERMISSION));
            env.close();
        }
        {
            // dissolve account sponsorship from sponsor
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(env.le(alice)->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 1);

            env(sponsor::transfer(sponsor, tfSponsorshipEnd), sponsor::SponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfSponsor));
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 0);
        }

        {
            // sponsor object (co-signing)
            Env env{*this, testableAmendments()};
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
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(pay(alice, sponsor1, drops(1)));
            env.close();

            // Invalid ObjectID (not found)
            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet::check(alice, 0).key),
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1),
                Ter(tecNO_ENTRY));
            env.close();

            // Invalid Owner
            env(sponsor::transfer(bob, tfSponsorshipCreate, checkId),
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1),
                Ter(tecNO_PERMISSION));
            env.close();

            // Valid Owner
            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::As(sponsor1, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor1));
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
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2),
                Ter(tecINSUFFICIENT_RESERVE));

            env(pay(alice, sponsor2, drops(1)));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2));
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

            env(sponsor::transfer(alice, tfSponsorshipEnd, checkId), Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, alice, reserve(env, 1));

            // object doesn't sponsored
            auto const ticketSeq = env.seq(alice);
            env(ticket::create(alice, 1));
            env.close();
            auto ticketId = keylet::TicketT()(alice, ticketSeq + 1).key;
            BEAST_EXPECT(env.le(keylet::unchecked(ticketId)));
            env(sponsor::transfer(alice, tfSponsorshipEnd, ticketId), Ter(tecNO_PERMISSION));
            env.close();
            env(noop(alice), ticket::Use(ticketSeq + 1));
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
            Env env{*this, testableAmendments()};
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
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(terNO_SPONSORSHIP));
            env.close();

            env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::As(sponsor2, spfSponsorReserve));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(terNO_SPONSORSHIP));
            env.close();
        }
        {
            // sponsor object (pre-funded)
            Env env{*this, testableAmendments()};
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
            env(sponsor::set_fee(sponsor1, 0, XRP(100)), sponsor::SponseeAcc(alice));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::As(sponsor1, spfSponsorReserve),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(sponsor::set_reserve(sponsor1, 0, 100), sponsor::SponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, checkId),
                sponsor::As(sponsor1, spfSponsorReserve));
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
            auto sponsor1Sle = env.le(keylet::sponsorship(sponsor1, alice));
            BEAST_EXPECT(sponsor1Sle->getFieldU32(sfRemainingOwnerCount) == 99);

            // transfer sponsor
            env(sponsor::set_reserve(sponsor2, 0, 100), sponsor::SponseeAcc(alice));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipReassign, checkId),
                sponsor::As(sponsor2, spfSponsorReserve));
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
            sponsor1Sle = env.le(keylet::sponsorship(sponsor1, alice));
            BEAST_EXPECT(sponsor1Sle->getFieldU32(sfRemainingOwnerCount) == 99);
            auto sponsor2Sle = env.le(keylet::sponsorship(sponsor2, alice));
            BEAST_EXPECT(sponsor2Sle->getFieldU32(sfRemainingOwnerCount) == 99);

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
            sponsor2Sle = env.le(keylet::sponsorship(sponsor2, alice));
            BEAST_EXPECT(sponsor2Sle->getFieldU32(sfRemainingOwnerCount) == 99);
        }

        {
            // Dissolve object sponsorship from sponsor(no-ltSponsorship)
            Env env{*this, testableAmendments()};
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
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::unchecked(checkId))->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // not the owner of the object
            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), Ter(tecNO_PERMISSION));
            env.close();

            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), sponsor::SponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(keylet::unchecked(checkId))->isFieldPresent(sfSponsor));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // Dissolve object sponsorship from sponsor (with ltSponsorship)
            Env env{*this, testableAmendments()};
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
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            env(sponsor::set_reserve(sponsor, 0, 100), sponsor::SponseeAcc(alice));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::unchecked(checkId))->getAccountID(sfSponsor) == sponsor.id());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(
                env.le(keylet::sponsorship(sponsor, alice))->getFieldU32(sfRemainingOwnerCount) ==
                100);

            // not the owner of the object
            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), Ter(tecNO_PERMISSION));
            env.close();

            env(sponsor::transfer(sponsor, tfSponsorshipEnd, checkId), sponsor::SponseeAcc(alice));
            env.close();

            BEAST_EXPECT(!env.le(keylet::unchecked(checkId))->isFieldPresent(sfSponsor));
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(
                env.le(keylet::sponsorship(sponsor, alice))->getFieldU32(sfRemainingOwnerCount) ==
                100);
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
                Env env{*this, testableAmendments()};
                env.fund(XRP(10000), alice, bob, sponsor);
                env.close();

                auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
                auto const& user = isIssuerHigh ? lowAcc : highAcc;

                auto const usd = issuer["usd"];
                auto const currency = usd.currency;

                env(trust(user, issuer["usd"](100)));
                env.close();

                auto const trustId = keylet::line(user, issuer, currency);
                BEAST_EXPECT(env.le(trustId));

                // transfer sponsor
                env(sponsor::transfer(user, tfSponsorshipCreate, trustId.key),
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor));
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
            Env env{*this, testableAmendments()};
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
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(tecNO_PERMISSION));
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
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob);
            env.close();

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay Fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    Fee(XRP(2000)),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(terNO_ACCOUNT));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env.fund(XRP(1000), sponsor);
            env.close();

            {
                // Sponsor pays the Fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt),
                    Fee(feeAmt),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance - feeAmt);
            }

            {
                // insufficient balance to pay Fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    Fee(XRP(2000)),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(terINSUF_FEE_B));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            {
                // Fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto const feeAmt = XRP(10);

                env(pay(alice, bob, XRP(20000)),
                    Fee(feeAmt),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(tecUNFUNDED_PAYMENT));
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
                    Fee(env.current()->fees().base),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(terINSUF_FEE_B));
                env.close();

                env(noop(alice),
                    Fee(XRP(10)),
                    sponsor::As(sponsor, spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(terINSUF_FEE_B));
                env.close();
            }
        }

        {
            // pre funded
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            auto const sponsorFeeBalance = [&](Account const& sponsor, Account const& sponsee) {
                return env.le(keylet::sponsorship(sponsor, sponsee))
                    ->getFieldAmount(sfFeeAmount)
                    .xrp();
            };

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay Fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    Fee(XRP(2000)),
                    sponsor::As(sponsor, spfSponsorFee),
                    Ter(terNO_SPONSORSHIP));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env(sponsor::set_fee(sponsor, 0, XRP(100)), sponsor::SponseeAcc(alice));
            env.close();

            {
                // Sponsor pays the Fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt), Fee(feeAmt), sponsor::As(sponsor, spfSponsorFee));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }

            {
                // insufficient balance to pay Fee
                {
                    // > FeeAmount
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        Fee(XRP(90) + drops(1)),
                        sponsor::As(sponsor, spfSponsorFee),
                        Ter(terINSUF_FEE_B));
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
                        Fee(XRP(90)),
                        sponsor::As(sponsor, spfSponsorFee),
                        Ter(tesSUCCESS));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
                    BEAST_EXPECT(env.balance(bob) == bobBalance + XRP(100));
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(
                        !env.le(keylet::sponsorship(sponsor, alice))->isFieldPresent(sfFeeAmount));
                }

                // reset FeeAmount and MaxFee
                env(sponsor::del(sponsor), sponsor::SponseeAcc(alice));
                env.close();
                env(sponsor::set_fee(sponsor, 0, XRP(10), XRP(1)), sponsor::SponseeAcc(alice));
                env.close();

                {
                    // > MaxFee
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        Fee(XRP(1) + drops(1)),
                        sponsor::As(sponsor, spfSponsorFee),
                        Ter(terINSUF_FEE_B));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance);
                    BEAST_EXPECT(env.balance(bob) == bobBalance);
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee);
                }
            }

            {
                // Fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);
                auto const feeAmt = XRP(1);

                env(pay(alice, bob, XRP(20000)),
                    Fee(feeAmt),
                    sponsor::As(sponsor, spfSponsorFee),
                    Ter(tecUNFUNDED_PAYMENT));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }

            // make sfFeeAmount absent if tec error and all Fee is paid
            {
                // reset FeeAmount and MaxFee
                env(sponsor::del(sponsor), sponsor::SponseeAcc(alice));
                env(sponsor::set_fee(sponsor, 0, XRP(10)), sponsor::SponseeAcc(alice));
                env.close();

                BEAST_EXPECT(
                    env.le(keylet::sponsorship(sponsor, alice))->isFieldPresent(sfFeeAmount));
                auto sponsorAvailableFee = sponsorFeeBalance(sponsor, alice);
                env(check::cancel(alice, uint256(1)),
                    Fee(sponsorAvailableFee),
                    sponsor::As(sponsor, spfSponsorFee),
                    Ter(tecNO_ENTRY));
                env.close();
                BEAST_EXPECT(
                    !env.le(keylet::sponsorship(sponsor, alice))->isFieldPresent(sfFeeAmount));
            }
        }

        // MaxFee cap is enforced in reset() for tec-failing transactions.
        // On a closed ledger view (!view.open()), checkFee returns tecINSUFF_FEE when
        // Fee > MaxFee (not terINSUF_FEE_B), triggering reset()
        {
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const carol("sponsor");

            env.fund(XRP(10000), alice, carol);
            env.close();

            // FeeAmount=1000 drops, MaxFee=10 drops
            env(sponsor::set_fee(carol, 0, drops(1000), drops(10)), sponsor::SponseeAcc(alice));
            env.close();

            // Apply directly against the closed ledger view (open_ = false) so that
            // checkFee returns tecINSUFF_FEE and reset() is invoked.
            OpenView overlay(&*env.closed());

            auto jt = env.jt(
                noop(alice),
                Fee(drops(1000)),
                Seq(env.seq(alice)),
                sponsor::As(carol, spfSponsorFee));

            auto const result = xrpl::apply(env.app(), overlay, *jt.stx, TapNone, env.journal);
            BEAST_EXPECT(result.ter == tecINSUFF_FEE);
            BEAST_EXPECT(result.applied);

            // Only MaxFee (10 drops) must be deducted, not the full 1000 drops.
            auto const sle = overlay.read(keylet::sponsorship(carol.id(), alice.id()));
            BEAST_EXPECT(sle);
            BEAST_EXPECT(sle->isFieldPresent(sfFeeAmount));
            BEAST_EXPECT(sle->getFieldAmount(sfFeeAmount) == drops(990));  // 1000 - MaxFee(10)
        }

        // test lsfSponsorshipRequireSignForFee
        {
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::SponseeAcc(alice));
            env.close();

            env(pay(alice, bob, XRP(100)),
                Fee(XRP(10)),
                sponsor::As(sponsor, spfSponsorFee),
                Ter(terNO_SPONSORSHIP));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::sponsorship(sponsor, alice))->getFieldAmount(sfFeeAmount) ==
                XRP(10));

            // clear flag
            env(sponsor::set_fee(sponsor, tfSponsorshipClearRequireSignForFee, XRP(10)),
                sponsor::SponseeAcc(alice));
            env.close();

            // Payment is re-applied
            BEAST_EXPECT(!env.le(keylet::sponsorship(sponsor, alice))->isFieldPresent(sfFeeAmount));
        }

        // RequireSignForFee: co-signing should succeed
        {
            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::SponseeAcc(alice));
            env.close();

            // pre-funded (no sig) should fail
            env(pay(alice, bob, XRP(100)),
                Fee(XRP(1)),
                sponsor::As(sponsor, spfSponsorFee),
                Ter(terNO_SPONSORSHIP));
            env.close();

            // co-signing (with sig) should succeed
            env(pay(alice, bob, XRP(100)),
                Fee(XRP(1)),
                sponsor::As(sponsor, spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::sponsorship(sponsor, alice))->getFieldAmount(sfFeeAmount) == XRP(9));
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
        auto const usd = gw["usd"];

        {
            // Disabled
            Env env{*this, testableAmendments() - featureSponsor};
            env.fund(XRP(10000), alice, sponsor);
            env.close();
            env(pay(alice, bob, XRP(100)), Txflags(tfSponsorCreatedAccount), Ter(temDISABLED));
            env.close();
        }

        Env env{*this, testableAmendments()};
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
                Txflags(tfSponsorCreatedAccount | flag),
                Ter(temINVALID_FLAG));
            env.close();
        }

        // Invalid amount(iou)
        env(pay(alice, bob, usd(100)), Txflags(tfSponsorCreatedAccount), Ter(temBAD_AMOUNT));
        env.close();

        // Sponsored account creation is reserve sponsorship and is only supported for direct XRP
        // payments.
        env(pay(alice, bob, drops(1)),
            Txflags(tfSponsorCreatedAccount),
            Sendmax(usd(2)),
            Ter(temINVALID));
        env.close();

        env(pay(alice, bob, drops(1)),
            Txflags(tfSponsorCreatedAccount),
            Path(~XRP),
            Ter(temINVALID));
        env.close();

        // Account is not sponsored by normal Sponsor specification
        {
            env(pay(alice, bob, drops(baseAccountReserve(*env.current(), 0))),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
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
                Txflags(tfSponsorCreatedAccount),
                Fee(XRP(1)),
                Ter(tecNO_SPONSOR_PERMISSION));
            env.close();

            BEAST_EXPECT(env.balance(sponsor2) == XRP(9999));

            // to non-funded account / insufficient balance for reserve
            env(pay(sponsor2, charlie, XRP(9999) - env.current()->fees().reserve + drops(1)),
                Txflags(tfSponsorCreatedAccount),
                Ter(tecUNFUNDED_PAYMENT));
            env.close();

            // to non-funded account
            auto const sponsor2BalanceBefore = env.balance(sponsor2);
            env(pay(sponsor2, charlie, drops(1)), Txflags(tfSponsorCreatedAccount), Fee(XRP(1)));
            env.close();

            auto const charlieSle = env.le(keylet::account(charlie));
            BEAST_EXPECT(charlieSle->isFieldPresent(sfSponsor));
            BEAST_EXPECT(charlieSle->getAccountID(sfSponsor) == sponsor2.id());
            BEAST_EXPECT(sponsoredOwnerCount(env, charlie) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 1);
            // verify sponsor balance decreased by payment + Fee
            BEAST_EXPECT(env.balance(sponsor2) == sponsor2BalanceBefore - drops(1) - XRP(1));
        }
        {
            // insufficient reserve to sponsor acount

            auto const sendAmount = drops(1);
            // 2 account reserve + send amount
            auto const requireBalance = accountReserve(env, 2) + sendAmount;
            adjustAccountXRPBalance(env, sponsor3, requireBalance - drops(1));
            env(pay(sponsor3, dave, sendAmount),
                Txflags(tfSponsorCreatedAccount),
                Fee(XRP(1)),
                Ter(tecUNFUNDED_PAYMENT));
            env.close();

            adjustAccountXRPBalance(env, sponsor3, requireBalance);
            env(pay(sponsor3, dave, sendAmount),
                Txflags(tfSponsorCreatedAccount),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testRequireFlag()
    {
        using namespace test::jtx;
        {
            testcase("SponsorshipRequireSignForReserve");

            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_reserve(sponsor, tfSponsorshipSetRequireSignForReserve, 10),
                sponsor::SponseeAcc(alice));
            env.close();

            env(check::create(alice, bob, XRP(100)),
                Fee(XRP(10)),
                sponsor::As(sponsor, spfSponsorReserve),
                Ter(terNO_SPONSORSHIP));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // clear flag
            env(sponsor::set_reserve(sponsor, tfSponsorshipClearRequireSignForReserve, 1),
                sponsor::SponseeAcc(alice));
            env.close();

            // CheckCreate is re-applied
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }

        {
            testcase("SponsorshipRequireSignForFee");

            Env env{*this, testableAmendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::SponseeAcc(alice));
            env.close();

            env(check::create(alice, bob, XRP(100)),
                Fee(XRP(10)),
                sponsor::As(sponsor, spfSponsorFee),
                Ter(terNO_SPONSORSHIP));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(
                env.le(keylet::sponsorship(sponsor, alice))->getFieldAmount(sfFeeAmount) ==
                XRP(10));

            // clear flag
            env(sponsor::set_fee(sponsor, tfSponsorshipClearRequireSignForFee, XRP(10)),
                sponsor::SponseeAcc(alice));
            env.close();

            // CheckCreate is re-applied
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(!env.le(keylet::sponsorship(sponsor, alice))->isFieldPresent(sfFeeAmount));
        }
    }

    void
    testSponsorReserveSimple(bool cosigning)
    {
        testcase("SponsorReserveSimple");
        using namespace test::jtx;
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(10000), alice, bob, sponsor);
        env.close();

        // test Sufficient sponsor balance
        if (cosigning)
        {
            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));

            env(check::create(alice, bob, XRP(100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1));

            env(check::create(alice, bob, XRP(100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor),
                Ter(tesSUCCESS));
            env.close();
        }
        else
        {
            env(sponsor::set_reserve(sponsor, 0, 250), sponsor::SponseeAcc(alice));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 2) - drops(1));

            env(check::create(alice, bob, XRP(100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 2));

            env(check::create(alice, bob, XRP(100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Ter(tesSUCCESS));
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

        std::optional<Sig> sponsorSig =
            cosigning ? std::optional<Sig>(Sig(sfSponsorSignature, sponsor)) : std::nullopt;

        auto const sponsorCurrentOwnerCount = ownerCount(env, sponsor) -
            sponsoredOwnerCount(env, sponsor) + sponsoringOwnerCount(env, sponsor);

        auto submit = [&](TER ter) {
            return [&, ter](json::Value const& jv, auto const&... fN) {
                if (sponsorSig)
                {
                    env(jv, fN..., sponsor::As(sponsor, spfSponsorReserve), *sponsorSig, Ter(ter));
                }
                else
                {
                    env(jv, fN..., sponsor::As(sponsor, spfSponsorReserve), Ter(ter));
                }
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
                if (env.le(keylet::sponsorship(sponsor, sponsee)))
                {
                    env(sponsor::del(sponsor), sponsor::SponseeAcc(sponsee));
                    env.close();
                }

                if (sponsorReserveCount - 1 > 0)
                {
                    env(sponsor::set(sponsor, 0, sponsorReserveCount - 1, XRP(1)),
                        sponsor::SponseeAcc(sponsee));
                }
                else
                {
                    // just create sponsor object
                    env(sponsor::set(sponsor, 0, std::nullopt, XRP(1)),
                        sponsor::SponseeAcc(sponsee));
                }
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
                env(sponsor::del(sponsor), sponsor::SponseeAcc(sponsee));
                env(sponsor::set(sponsor, 0, sponsorReserveCount, XRP(1)),
                    sponsor::SponseeAcc(sponsee));
                env.close();
            }
            callback(env, submit(tesSUCCESS));
            env.close();

            if (!cosigning)
            {
                // cleanup sponsorship
                env(sponsor::del(sponsor), sponsor::SponseeAcc(sponsee));
                env.close();
            }
        }

        if (expected)
        {
            (*expected)();
        }
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
    testCheck(bool cosigning)
    {
        testcase("Check");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const usd = gw["usd"];

        {
            Env env{*this, testableAmendments()};
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();

                // transfer sponsor
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve));
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
            Env env{*this, testableAmendments()};
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env.trust(usd(100), alice);
            env.close();
            env(pay(gw, alice, usd(100)));
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
                    submit(check::create(alice, bob, usd(1)));
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
                [&](Env& env, auto const& submit) { submit(check::cash(bob, keylet.key, usd(1))); },
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
    testDelegate(bool cosigning)
    {
        testcase("Delegate");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testableAmendments()};
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve));
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
            Env env{*this, testableAmendments()};
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
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
            Env env{*this, testableAmendments()};
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
                        escrow::kCondition(escrow::kCb1),
                        escrow::kCancelTime(env.now() + 100s));
                });
            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))->getAccountID(sfSponsor) == sponsor.id());

            // transfer sponsor
            if (cosigning)
            {
                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::escrow(alice, seq).key),
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::escrow(alice, seq).key),
                    sponsor::As(sponsor2, spfSponsorReserve));
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
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        Account const gw("gw");
        auto const usd = gw["usd"];
        {
            // IOU Escrow
            Env env{*this, testableAmendments()};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env.trust(usd(1000000), alice);
            env.close();
            env(pay(gw, alice, usd(10000)));
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
                        escrow::create(alice, bob, usd(100)),
                        escrow::kCondition(escrow::kCb1),
                        escrow::kCancelTime(env.now() + 100s));
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
                        escrow::kCondition(escrow::kCb1),
                        escrow::kFulfillment(escrow::kFb1),
                        Fee(baseFee * 150));
                });

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            BEAST_EXPECT(
                env.le(keylet::line(bob, gw, usd.currency))->getAccountID(sfHighSponsor) ==
                sponsor2.id());
        }
        {
            // MPT Escrow
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const mpt = mptGw["MPT"];
            env(pay(gw, alice, mpt(10'000)));
            env.close();

            // create Escrow from alice to bob
            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, mpt(100)),
                escrow::kCondition(escrow::kCb1),
                escrow::kCancelTime(env.now() + 100s));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // finish Escrow
            env(escrow::finish(bob, alice, seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor),
                Fee(XRP(1)));
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // MPTokenIssuanceCreate
            json::Value jv = {};
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, mptIssuanceKeylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve));
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(bob));
                env.close();

                env(sponsor::transfer(bob, tfSponsorshipReassign, mptTokenKeylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve));
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // MPTokenAuthorize
            json::Value jv = {};
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
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
            else
            {
                env(sponsor::set(sponsor, 0, std::nullopt, XRP(1)), sponsor::SponseeAcc(bob));
                env.close();

                env(jv, sponsor::As(sponsor, spfSponsorReserve), Ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }

            env(noop(sponsor), ticket::Use(ticketSeq));
            env.close();

            // pass (free-tier mptoken for the holder, but the sponsor is still
            // charged a reserve increment regardless of the ownerCount < 2 shortcut).
            if (cosigning)
            {
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
                env(jv,
                    sponsor::As(sponsor, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(tesSUCCESS));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor, 0, 1), sponsor::SponseeAcc(bob));
                env.close();
                env(jv, sponsor::As(sponsor, spfSponsorReserve), Ter(tesSUCCESS));
                env.close();
            }
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
            Env env{*this, testableAmendments()};
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
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
                env.close();

                env(sponsor::transfer(alice, tfSponsorshipReassign, chan),
                    sponsor::As(sponsor2, spfSponsorReserve));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            env.close(env.now() + settleDelay);
            // PayChanClaim (delete PayChan)
            env(paychan::claim(bob, chan), Txflags(tfClose));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
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

        Env env{*this, testableAmendments()};
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
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2),
                Ter(tecNO_PERMISSION));
            // invalid signer list owner 2
            // account has signer list and specified signer list exists
            env(signers(bob, 1, {{alice, 1}}));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(bob).key),
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2),
                Ter(tecNO_PERMISSION));
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(alice).key),
                sponsor::As(sponsor2, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor2));
            env.close();
        }
        else
        {
            env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(alice));
            env.close();
            env(sponsor::transfer(alice, tfSponsorshipReassign, keylet::signers(alice).key),
                sponsor::As(sponsor2, spfSponsorReserve));
            env.close();
        }

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

        // Delete
        env(signers(alice, NoneT()));
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, charlie, sponsor, sponsor2);
            env.close();

            auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
            auto const& user = isIssuerHigh ? lowAcc : highAcc;

            auto const usd = issuer["usd"];
            auto const currency = usd.currency;

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
                [&](Env& env, auto const& submit) { submit(trust(user, usd(100))); });

            auto const keylet = keylet::line(user, issuer, currency);

            if (cosigning)
            {
                // invalid owner
                env(sponsor::transfer(charlie, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2),
                    Ter(tecNO_PERMISSION));
                // invalid reserve owner
                env(sponsor::transfer(issuer, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2),
                    Ter(tecNO_PERMISSION));
                env(sponsor::transfer(user, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve),
                    Sig(sfSponsorSignature, sponsor2));
                env.close();
            }
            else
            {
                env(sponsor::set_reserve(sponsor2, 0, 1), sponsor::SponseeAcc(user));
                env.close();
                env(sponsor::transfer(user, tfSponsorshipReassign, keylet.key),
                    sponsor::As(sponsor2, spfSponsorReserve));
                env.close();
            }

            // delete TrustLine
            env(trust(user, usd(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, user) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, user) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            BEAST_EXPECT(!env.le(keylet));
        }

        // update
        for (bool const isIssuerHigh : {false, true})
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
            auto const& user = isIssuerHigh ? lowAcc : highAcc;

            auto const usd = issuer["usd"];
            auto const currency = usd.currency;

            // create TrustLine from issuer
            env(trust(issuer, user["usd"](100)));
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
                [&](Env& env, auto const& submit) { submit(trust(user, usd(100))); });

            auto const line = env.le(keylet::line(user, issuer, currency));
            validateSponsoredTrustline(line, isIssuerHigh, sponsor);

            // update TrustLine from user to clear reserve
            env(trust(user, usd(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, user) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, user) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(env.le(keylet::line(user, issuer, currency)));

            // remove TrustLine from issuer
            env(trust(issuer, user["usd"](0)));
            env.close();
            BEAST_EXPECT(!env.le(keylet::line(user, issuer, currency)));
        }

        // both High and Low sponsored
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // create TrustLines
            env(trust(alice, bob["usd"](100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();
            env(trust(bob, alice["usd"](100)),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            auto sle = env.le(keylet::line(alice, bob, alice["usd"].currency));
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
            env(trust(alice, bob["usd"](0)));
            env.close();
            env(trust(bob, alice["usd"](0)));
            env.close();

            sle = env.le(keylet::line(alice, bob, alice["usd"].currency));
            BEAST_EXPECT(!sle);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // set sponsor
            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            incLgrSeqForAccDel(env, sponsor);

            auto const keylet = keylet::sponsorship(sponsor, alice);
            auto const sponsorObj = env.le(keylet);
            BEAST_EXPECT(sponsorObj);

            // AccountDelete
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob), Fee(requiredFee), Ter(tecHAS_OBLIGATIONS));
            env(acctdelete(sponsor, bob), Fee(requiredFee), Ter(tecHAS_OBLIGATIONS));
        }

        {
            // Delete SponsoredAccount
            Env env{*this, testableAmendments()};
            env.memoize(alice);
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            // create SponsoredAccount
            env(pay(sponsor, alice, XRP(10000)), Txflags(tfSponsorCreatedAccount));
            env.close();

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = non-sponsor
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob), Fee(requiredFee), Ter(tecNO_SPONSOR_PERMISSION));

            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringAccountCount) == 1);

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = sponsor
            env(acctdelete(alice, sponsor), Fee(requiredFee), Ter(tesSUCCESS));

            auto const sponsorSle2 = env.le(keylet::account(sponsor));
            BEAST_EXPECT(!sponsorSle2->isFieldPresent(sfSponsoringAccountCount));
        }

        {
            // Sponsor with sfSponsoringOwnerCount cannot delete (tecHAS_OBLIGATIONS)
            Env env{*this, testableAmendments()};
            Account const gw("gw");
            env.fund(XRP(1000000), alice, bob, sponsor, gw);
            env.close();

            auto const usd = gw["usd"];

            // Create sponsorship allowing reserve sponsoring
            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::SponseeAcc(alice),
                Ter(tesSUCCESS));
            env.close();

            // Create a trust line for alice
            env(trust(alice, usd(1000)));
            env.close();

            // Transfer reserve sponsorship of trust line to sponsor
            auto const trustId = keylet::line(alice, gw, usd.currency);
            BEAST_EXPECT(env.le(trustId));

            env(sponsor::transfer(alice, tfSponsorshipCreate, trustId.key),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            // Verify sfSponsoringOwnerCount is set on sponsor
            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->isFieldPresent(sfSponsoringOwnerCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringOwnerCount) >= 1);

            incLgrSeqForAccDel(env, sponsor);

            // AccountDelete should fail
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(sponsor, bob), Fee(requiredFee), Ter(tecHAS_OBLIGATIONS));
        }

        {
            // Sponsor with sfSponsoringAccountCount cannot delete (tecHAS_OBLIGATIONS)
            Env env{*this, testableAmendments()};
            env.memoize(alice);
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            // Create SponsoredAccount (sets sfSponsoringAccountCount on sponsor)
            env(pay(sponsor, alice, XRP(10000)), Txflags(tfSponsorCreatedAccount));
            env.close();

            // Verify sfSponsoringAccountCount is set on sponsor
            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle->isFieldPresent(sfSponsoringAccountCount));
            BEAST_EXPECT(sponsorSle->getFieldU32(sfSponsoringAccountCount) == 1);

            incLgrSeqForAccDel(env, sponsor);

            // AccountDelete should fail
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(sponsor, bob), Fee(requiredFee), Ter(tecHAS_OBLIGATIONS));
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
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const keylet = keylet::check(alice, seq);

            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet.key),
                sponsor::As(bob, spfSponsorReserve),
                Sig(sfSponsorSignature, bob),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));

            env(delegate::set(alice, carol, {"SponsorshipTransfer"}));
            env.close();

            env(sponsor::transfer(alice, tfSponsorshipCreate, keylet.key),
                sponsor::As(bob, spfSponsorReserve),
                Sig(sfSponsorSignature, bob),
                delegate::As(carol),
                Ter(tesSUCCESS));
            env.close();
        }
        //
        // SponsorshipSet
        //
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::SponseeAcc(bob),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));

            env(delegate::set(alice, carol, {"SponsorshipSet"}));
            env.close();

            env(sponsor::set(alice, 0, 100, XRP(100)),
                sponsor::SponseeAcc(bob),
                delegate::As(carol),
                Ter(tesSUCCESS));
            env.close();
        }

        //
        // Permission SponsorFee
        //
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();
            auto const testFeePermission = [&](TER result) {
                // FeeAmount
                env(sponsor::set(alice, 0, std::nullopt, XRP(100)),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
                // MaxFee
                env(sponsor::set(alice, 0, std::nullopt, std::nullopt, XRP(100)),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
                // SetRequireSignForFee flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForFee),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
                // ClearRequireSignForFee flag
                env(sponsor::set(alice, tfSponsorshipClearRequireSignForFee),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
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
                sponsor::SponseeAcc(bob),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));
        }

        //
        // Permission SponsorReserve
        //
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            auto const testReservePermission = [&](TER result) {
                // ReserveCount
                env(sponsor::set(alice, 0, 100),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
                // SetRequireSignForReserve flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForReserve),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
                // ClearRequireSignForReserve flag
                env(sponsor::set(alice, tfSponsorshipClearRequireSignForReserve),
                    sponsor::SponseeAcc(bob),
                    delegate::As(carol),
                    Ter(result));
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
                sponsor::SponseeAcc(bob),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));
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
        // outer transaction
        //
        {
            // test outer transaction with co-signing sponsor
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::Inner(noop(alice), seq + 1),
                batch::Inner(ticket::create(alice, 1), seq + 2),
                sponsor::As(sponsor, spfSponsorFee),
                Sig(sfSponsorSignature, sponsor),
                Ter(tesSUCCESS));
            env.close();

            // does not affect reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // Fee is paid by sponsor
            BEAST_EXPECT(env.balance(alice) == XRP(1000));
            BEAST_EXPECT(env.balance(sponsor) == XRP(1000 - 1));
        }
        {
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            // spfSponsorReserve on outer Batch is rejected
            for (auto const flags : {spfSponsorReserve | spfSponsorFee, spfSponsorReserve})
            {
                auto const seq = env.seq(alice);
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::Inner(noop(alice), seq + 1),
                    batch::Inner(noop(alice), seq + 2),
                    sponsor::As(sponsor, flags),
                    Sig(sfSponsorSignature, sponsor),
                    Ter(temINVALID_FLAG));
                env.close();
            }
        }
        {
            // test outer transaction with prefunded sponsor
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob);
            env.fund(XRP(1001), sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::Inner(noop(alice), seq + 1),
                batch::Inner(ticket::create(alice, 1), seq + 2),
                sponsor::As(sponsor, spfSponsorFee),
                Ter(tesSUCCESS));
            env.close();

            // does not affect reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // Fee is paid by sponsor object
            BEAST_EXPECT(env.balance(alice) == XRP(1000));
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            auto const sponsorshipSle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sponsorshipSle);
            BEAST_EXPECT(sponsorshipSle->at(sfFeeAmount) == XRP(100 - 1));
            BEAST_EXPECT(sponsorshipSle->at(sfRemainingOwnerCount) == 100);
        }
        //
        // Inner transaction
        //
        {
            // test invalid Inner transaction with co-signing sponsor
            Account const signerAccount("signer");
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob, sponsor, signerAccount);
            env.close();

            env(signers(sponsor, 1, {Signer(signerAccount, 1)}));
            env.close();

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor));
                jt.jv.removeMember(sfTxnSignature.jsonName);

                auto const seq = env.seq(alice);
                // should fail because Inner transaction cannot include SponsorSignature with
                // TxnSignature
                BEAST_EXPECT(jt.jv[sfSponsorSignature.jsonName].isMember(sfTxnSignature.jsonName));
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::Inner(jt.jv, seq + 1),
                    batch::Inner(ticket::create(alice, 1), seq + 2),
                    Ter(temBAD_SIGNATURE));
            }

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                    Msig(sfSponsorSignature, sponsor, signerAccount));
                jt.jv.removeMember(sfTxnSignature.jsonName);

                auto const seq = env.seq(alice);
                // should fail because Inner transaction cannot include SponsorSignature with
                // Signers
                BEAST_EXPECT(jt.jv[sfSponsorSignature.jsonName].isMember(sfSigners.jsonName));
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::Inner(jt.jv, seq + 1),
                    batch::Inner(ticket::create(alice, 1), seq + 2),
                    Ter(temBAD_SIGNER));
            }

            {
                auto jt = env.jtnofill(
                    noop(alice),
                    sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                    Sig(sfSponsorSignature, sponsor));
                jt.jv.removeMember(sfTxnSignature.jsonName);
                jt.jv[sfSponsorSignature.jsonName].removeMember(sfTxnSignature.jsonName);
                jt.jv[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = "";

                auto const seq = env.seq(alice);
                // should fail BatchSigners does have signer for SponsorSignature
                env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                    batch::Inner(jt.jv, seq + 1),
                    batch::Inner(ticket::create(alice, 1), seq + 2),
                    Ter(temBAD_SIGNER));
            }
        }

        {
            // test Inner transaction with prefunded sponsor
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob);
            env.fund(XRP(1001), sponsor);
            env.close();

            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::SponseeAcc(alice),
                Fee(XRP(1)),
                Ter(tesSUCCESS));
            env.close();
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            auto jt = env.jtnofill(
                check::create(alice, bob, XRP(1)),
                sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee));
            // remove txn signature since it is filled by env.jtnofill()
            jt.jv.removeMember(jss::TxnSignature);

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::Inner(noop(alice), seq + 1),
                batch::Inner(jt.jv, seq + 2),
                Ter(tesSUCCESS));
            env.close();

            // affect sponsor reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Fee is paid by outer transaction originator (alice)
            BEAST_EXPECT(env.balance(alice) == XRP(999));
            BEAST_EXPECT(env.balance(sponsor) == XRP(900));

            // reserve count is decreased
            auto const sponsorshipSle = env.le(keylet::sponsorship(sponsor, alice));
            BEAST_EXPECT(sponsorshipSle);
            BEAST_EXPECT(sponsorshipSle->at(sfFeeAmount) == XRP(100));
            BEAST_EXPECT(sponsorshipSle->at(sfRemainingOwnerCount) == 99);
        }

        {
            // test Inner transaction with co-signing sponsor
            Env env{*this, testableAmendments()};
            env.fund(XRP(1000), alice, bob, sponsor);
            env.close();

            auto jt = env.jtnofill(
                check::create(alice, bob, XRP(1)),
                sponsor::As(sponsor, spfSponsorReserve | spfSponsorFee),
                Sig(sfSponsorSignature, sponsor));
            // remove txn signature since it is filled by env.jtnofill()
            jt.jv.removeMember(sfTxnSignature.jsonName);
            jt.jv[sfSponsorSignature.jsonName].removeMember(sfTxnSignature.jsonName);
            jt.jv[sfSponsorSignature.jsonName][sfSigningPubKey.jsonName] = "";

            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, XRP(1), tfAllOrNothing),
                batch::Inner(noop(alice), seq + 1),
                batch::Inner(jt.jv, seq + 2),
                batch::Sig(sponsor),
                Ter(tesSUCCESS));
            env.close();

            // affect sponsor reserve
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // Fee is paid by outer transaction originator (alice)
            BEAST_EXPECT(env.balance(alice) == XRP(999));
            BEAST_EXPECT(env.balance(sponsor) == XRP(1000));
        }
    }

    void
    testSponsorReserve(bool cosigning)
    {
        testRequireFlag();
        testSponsorReserveSimple(cosigning);
        testCheck(cosigning);
        testDelegate(cosigning);
        testDepositPreauth(cosigning);
        testEscrow(cosigning);
        testMPToken(cosigning);
        testPayChan(cosigning);
        testSignerList(cosigning);
        testTrustSet(cosigning);
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
        testSponsoredFreeTierReserve();

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

}  // namespace xrpl::test
