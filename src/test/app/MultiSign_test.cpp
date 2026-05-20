
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/require.h>
#include <test/jtx/rpc.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace xrpl::test {

class MultiSign_test : public beast::unit_test::Suite
{
    // Unfunded accounts to use for phantom signing.
    jtx::Account const bogie_{"bogie", KeyType::Secp256k1};
    jtx::Account const demon_{"demon", KeyType::Ed25519};
    jtx::Account const ghost_{"ghost", KeyType::Secp256k1};
    jtx::Account const haunt_{"haunt", KeyType::Ed25519};
    jtx::Account const jinni_{"jinni", KeyType::Secp256k1};
    jtx::Account const phase_{"phase", KeyType::Ed25519};
    jtx::Account const shade_{"shade", KeyType::Secp256k1};
    jtx::Account const spook_{"spook", KeyType::Ed25519};
    jtx::Account const acc10_{"acc10", KeyType::Ed25519};
    jtx::Account const acc11_{"acc11", KeyType::Ed25519};
    jtx::Account const acc12_{"acc12", KeyType::Ed25519};
    jtx::Account const acc13_{"acc13", KeyType::Ed25519};
    jtx::Account const acc14_{"acc14", KeyType::Ed25519};
    jtx::Account const acc15_{"acc15", KeyType::Ed25519};
    jtx::Account const acc16_{"acc16", KeyType::Ed25519};
    jtx::Account const acc17_{"acc17", KeyType::Ed25519};
    jtx::Account const acc18_{"acc18", KeyType::Ed25519};
    jtx::Account const acc19_{"acc19", KeyType::Ed25519};
    jtx::Account const acc20_{"acc20", KeyType::Ed25519};
    jtx::Account const acc21_{"acc21", KeyType::Ed25519};
    jtx::Account const acc22_{"acc22", KeyType::Ed25519};
    jtx::Account const acc23_{"acc23", KeyType::Ed25519};
    jtx::Account const acc24_{"acc24", KeyType::Ed25519};
    jtx::Account const acc25_{"acc25", KeyType::Ed25519};
    jtx::Account const acc26_{"acc26", KeyType::Ed25519};
    jtx::Account const acc27_{"acc27", KeyType::Ed25519};
    jtx::Account const acc28_{"acc28", KeyType::Ed25519};
    jtx::Account const acc29_{"acc29", KeyType::Ed25519};
    jtx::Account const acc30_{"acc30", KeyType::Ed25519};
    jtx::Account const acc31_{"acc31", KeyType::Ed25519};
    jtx::Account const acc32_{"acc32", KeyType::Ed25519};
    jtx::Account const acc33_{"acc33", KeyType::Ed25519};

public:
    void
    testNoReserve(FeatureBitset features)
    {
        testcase("No Reserve");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Secp256k1};

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        auto const fee = env.current()->fees().base;
        env.fund(XRP(250) - drops(1), alice);
        env.close();
        env.require(Owners(alice, 0));

        {
            // Attach a signer list to alice.  Should fail.
            json::Value const signersList = signers(alice, 1, {{bogie_, 1}});
            env(signersList, Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(Owners(alice, 0));

            // Fund alice enough to set the signer list, then attach signers.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(signersList);
            env.close();
            env.require(Owners(alice, 1));
        }
        {
            // Pay alice enough to almost make the reserve for the biggest
            // possible list.
            env(pay(env.master, alice, fee - drops(1)));

            // Replace with the biggest possible signer list.  Should fail.
            json::Value const bigSigners = signers(
                alice,
                1,
                {{bogie_, 1},
                 {demon_, 1},
                 {ghost_, 1},
                 {haunt_, 1},
                 {jinni_, 1},
                 {phase_, 1},
                 {shade_, 1},
                 {spook_, 1}});
            env(bigSigners, Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(Owners(alice, 1));

            // Fund alice one more drop (plus the fee) and succeed.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(bigSigners);
            env.close();
            env.require(Owners(alice, 1));
        }
        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::kNone));
        env.close();
        env.require(Owners(alice, 0));
    }

    void
    testSignerListSet(FeatureBitset features)
    {
        testcase("SignerListSet");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Add alice as a multisigner for herself.  Should fail.
        env(signers(alice, 1, {{alice, 1}}), Ter(temBAD_SIGNER));

        // Add a signer with a weight of zero.  Should fail.
        env(signers(alice, 1, {{bogie_, 0}}), Ter(temBAD_WEIGHT));

        // Add a signer where the weight is too big.  Should fail since
        // the weight field is only 16 bits.  The jtx framework can't do
        // this kind of test, so it's commented out.
        //      env(signers(alice, 1, { { bogie, 0x10000} }), ter
        //      (temBAD_WEIGHT));

        // Add the same signer twice.  Should fail.
        env(signers(
                alice,
                1,
                {{bogie_, 1},
                 {demon_, 1},
                 {ghost_, 1},
                 {haunt_, 1},
                 {jinni_, 1},
                 {phase_, 1},
                 {demon_, 1},
                 {spook_, 1}}),
            Ter(temBAD_SIGNER));

        // Set a quorum of zero.  Should fail.
        env(signers(alice, 0, {{bogie_, 1}}), Ter(temMALFORMED));

        // Make a signer list where the quorum can't be met.  Should fail.
        env(signers(
                alice,
                9,
                {{bogie_, 1},
                 {demon_, 1},
                 {ghost_, 1},
                 {haunt_, 1},
                 {jinni_, 1},
                 {phase_, 1},
                 {shade_, 1},
                 {spook_, 1}}),
            Ter(temBAD_QUORUM));

        // Make a signer list that's too big.  Should fail.
        Account const spare("spare", KeyType::Secp256k1);
        env(signers(
                alice,
                1,
                std::vector<Signer>{
                    {bogie_, 1}, {demon_, 1}, {ghost_, 1}, {haunt_, 1}, {jinni_, 1}, {phase_, 1},
                    {shade_, 1}, {spook_, 1}, {spare, 1},  {acc10_, 1}, {acc11_, 1}, {acc12_, 1},
                    {acc13_, 1}, {acc14_, 1}, {acc15_, 1}, {acc16_, 1}, {acc17_, 1}, {acc18_, 1},
                    {acc19_, 1}, {acc20_, 1}, {acc21_, 1}, {acc22_, 1}, {acc23_, 1}, {acc24_, 1},
                    {acc25_, 1}, {acc26_, 1}, {acc27_, 1}, {acc28_, 1}, {acc29_, 1}, {acc30_, 1},
                    {acc31_, 1}, {acc32_, 1}, {acc33_, 1},
                }),
            Ter(temMALFORMED));
        // clang-format on
        env.close();
        env.require(Owners(alice, 0));
    }

    void
    testPhantomSigners(FeatureBitset features)
    {
        testcase("Phantom Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie_, 1}, {demon_, 1}}));
        env.close();
        env.require(Owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, demon_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(demon_), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            Msig(demon_, demon_),
            Fee(3 * baseFee),
            Rpc("invalidTransaction", "fails local checks: Duplicate Signers not allowed."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, spook_), Fee(3 * baseFee), Ter(tefBAD_SIGNATURE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie_, 1}, {demon_, 1}}));
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee(2 * baseFee), Ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, demon_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testFee(FeatureBitset features)
    {
        testcase("Fee");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach maximum possible number of signers to alice.
        env(signers(
            alice,
            1,
            {{bogie_, 1},
             {demon_, 1},
             {ghost_, 1},
             {haunt_, 1},
             {jinni_, 1},
             {phase_, 1},
             {shade_, 1},
             {spook_, 1}}));
        env.close();
        env.require(Owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee(2 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee((2 * baseFee) - 1), Ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            Msig(bogie_, demon_, ghost_, haunt_, jinni_, phase_, shade_, spook_),
            Fee(9 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            Msig(bogie_, demon_, ghost_, haunt_, jinni_, phase_, shade_, spook_),
            Fee((9 * baseFee) - 1),
            Ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testMisorderedSigners(FeatureBitset features)
    {
        testcase("Misordered Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // The signatures in a transaction must be submitted in sorted order.
        // Make sure the transaction fails if they are not.
        env(signers(alice, 1, {{bogie_, 1}, {demon_, 1}}));
        env.close();
        env.require(Owners(alice, 1));

        Msig phantoms{bogie_, demon_};
        std::ranges::reverse(phantoms.signers);
        std::uint32_t const aliceSeq = env.seq(alice);
        env(noop(alice),
            phantoms,
            Rpc("invalidTransaction", "fails local checks: Unsorted Signers array."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testMasterSigners(FeatureBitset features)
    {
        testcase("Master Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        Account const becky{"becky", KeyType::Secp256k1};
        Account const cheri{"cheri", KeyType::Ed25519};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // For a different situation, give alice a regular key but don't use it.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env.close();
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Sig(alice));
        env(noop(alice), Sig(alie));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

        // Attach signers to alice
        env(signers(alice, 4, {{becky, 3}, {cheri, 4}}), Sig(alice));
        env.close();
        env.require(Owners(alice, 1));

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(cheri), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // If we don't meet the quorum the transaction should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(becky), Fee(2 * baseFee), Ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Give becky and cheri regular keys.
        Account const beck{"beck", KeyType::Ed25519};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::Ed25519};
        env(regkey(cheri, cher));
        env.close();

        // becky's and cheri's master keys should still work.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(becky, cheri), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testRegularSigners(FeatureBitset features)
    {
        testcase("Regular Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};
        Account const cheri{"cheri", KeyType::Secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}}), Sig(alice));

        // Give everyone regular keys.
        Account const alie{"alie", KeyType::Ed25519};
        env(regkey(alice, alie));
        Account const beck{"beck", KeyType::Secp256k1};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::Ed25519};
        env(regkey(cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset(cheri, asfDisableMaster), Sig(cheri));
        env.close();

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Msig(Reg{cheri, cher}), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // cheri should not be able to multisign using her master key.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(cheri), Fee(2 * baseFee), Ter(tefMASTER_DISABLED));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // becky should be able to multisign using either of her keys.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(becky), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(Reg{becky, beck}), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Both becky and cheri should be able to sign using regular keys.
        aliceSeq = env.seq(alice);
        env(noop(alice), Fee(3 * baseFee), Msig(Reg{becky, beck}, Reg{cheri, cher}));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testRegularSignersUsingSubmitMulti(FeatureBitset features)
    {
        testcase("Regular Signers Using submit_multisigned");

        using namespace jtx;
        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                return cfg;
            }),
            features);
        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};
        Account const cheri{"cheri", KeyType::Secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {cheri, 1}}), Sig(alice));

        // Give everyone regular keys.
        Account const beck{"beck", KeyType::Secp256k1};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::Ed25519};
        env(regkey(cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset(cheri, asfDisableMaster), Sig(cheri));
        env.close();

        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = 0;

        // these represent oft-repeated setup for input json below
        auto setupTx = [&]() -> json::Value {
            json::Value jv;
            jv[jss::tx_json][jss::Account] = alice.human();
            jv[jss::tx_json][jss::TransactionType] = jss::AccountSet;
            jv[jss::tx_json][jss::Fee] = (8 * baseFee).jsonClipped();
            jv[jss::tx_json][jss::Sequence] = env.seq(alice);
            jv[jss::tx_json][jss::SigningPubKey] = "";
            return jv;
        };
        auto cheriSign = [&](json::Value& jv) {
            jv[jss::account] = cheri.human();
            jv[jss::key_type] = "ed25519";
            jv[jss::passphrase] = cher.name();
        };
        auto beckySign = [&](json::Value& jv) {
            jv[jss::account] = becky.human();
            jv[jss::secret] = beck.name();
        };

        {
            // Attempt a multisigned transaction that meets the quorum.
            // using sign_for and submit_multisigned
            aliceSeq = env.seq(alice);
            json::Value jvOne = setupTx();
            cheriSign(jvOne);
            auto jrr = env.rpc("json", "sign_for", to_string(jvOne))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            json::Value jvTwo;
            jvTwo[jss::tx_json] = jrr[jss::tx_json];
            beckySign(jvTwo);
            jrr = env.rpc("json", "sign_for", to_string(jvTwo))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jvSubmit;
            jvSubmit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jvSubmit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        {
            // failure case -- SigningPubKey not empty
            aliceSeq = env.seq(alice);
            json::Value jvOne = setupTx();
            jvOne[jss::tx_json][jss::SigningPubKey] = strHex(alice.pk().slice());
            cheriSign(jvOne);
            auto jrr = env.rpc("json", "sign_for", to_string(jvOne))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "When multi-signing 'tx_json.SigningPubKey' must be empty.");
        }

        {
            // failure case - bad fee
            aliceSeq = env.seq(alice);
            json::Value jvOne = setupTx();
            jvOne[jss::tx_json][jss::Fee] = -1;
            cheriSign(jvOne);
            auto jrr = env.rpc("json", "sign_for", to_string(jvOne))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            json::Value jvTwo;
            jvTwo[jss::tx_json] = jrr[jss::tx_json];
            beckySign(jvTwo);
            jrr = env.rpc("json", "sign_for", to_string(jvTwo))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jvSubmit;
            jvSubmit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jvSubmit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid Fee field.  Fees must be greater than zero.");
        }

        {
            // failure case - bad fee v2
            aliceSeq = env.seq(alice);
            json::Value jvOne = setupTx();
            jvOne[jss::tx_json][jss::Fee] = alice["USD"](10).value().getFullText();
            cheriSign(jvOne);
            auto jrr = env.rpc("json", "sign_for", to_string(jvOne))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            json::Value jvTwo;
            jvTwo[jss::tx_json] = jrr[jss::tx_json];
            beckySign(jvTwo);
            jrr = env.rpc("json", "sign_for", to_string(jvTwo))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jvSubmit;
            jvSubmit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jvSubmit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "internal");
            BEAST_EXPECT(jrr[jss::error_message] == "Internal error.");
        }

        {
            // cheri should not be able to multisign using her master key.
            aliceSeq = env.seq(alice);
            json::Value jv = setupTx();
            jv[jss::account] = cheri.human();
            jv[jss::secret] = cheri.name();
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "masterDisabled");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        {
            // Unlike cheri, becky should also be able to sign using her master
            // key
            aliceSeq = env.seq(alice);
            json::Value jvOne = setupTx();
            cheriSign(jvOne);
            auto jrr = env.rpc("json", "sign_for", to_string(jvOne))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            json::Value jvTwo;
            jvTwo[jss::tx_json] = jrr[jss::tx_json];
            jvTwo[jss::account] = becky.human();
            jvTwo[jss::key_type] = "ed25519";
            jvTwo[jss::passphrase] = becky.name();
            jrr = env.rpc("json", "sign_for", to_string(jvTwo))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jvSubmit;
            jvSubmit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jvSubmit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        {
            // check for bad or bogus accounts in the tx
            json::Value jv = setupTx();
            jv[jss::tx_json][jss::Account] = "DEADBEEF";
            cheriSign(jv);
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "srcActMalformed");

            Account const jimmy{"jimmy"};
            jv[jss::tx_json][jss::Account] = jimmy.human();
            jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "srcActNotFound");
        }

        {
            aliceSeq = env.seq(alice);
            json::Value jv = setupTx();
            jv[jss::tx_json][sfSigners.fieldName] = json::Value{json::ValueType::Array};
            beckySign(jv);
            auto jrr = env.rpc("json", "submit_multisigned", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "tx_json.Signers array may not be empty.");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }
    }

    void
    testHeterogeneousSigners(FeatureBitset features)
    {
        testcase("Heterogeneous Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};
        Account const cheri{"cheri", KeyType::Secp256k1};
        Account const daria{"daria", KeyType::Ed25519};
        env.fund(XRP(1000), alice, becky, cheri, daria);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), Sig(alice));

        // becky is master only without a regular key.

        // cheri has a regular key, but leaves the master key enabled.
        Account const cher{"cher", KeyType::Secp256k1};
        env(regkey(cheri, cher));

        // daria has a regular key and disables her master key.
        Account const dari{"dari", KeyType::Ed25519};
        env(regkey(daria, dari));
        env(fset(daria, asfDisableMaster), Sig(daria));
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}, {daria, 1}, {jinni_, 1}}), Sig(alie));
        env.close();
        env.require(Owners(alice, 1));

        // Each type of signer should succeed individually.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Msig(becky), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(cheri), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(Reg{cheri, cher}), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(Reg{daria, dari}), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(jinni_), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        //  Should also work if all signers sign.
        aliceSeq = env.seq(alice);
        env(noop(alice), Fee(5 * baseFee), Msig(becky, Reg{cheri, cher}, Reg{daria, dari}, jinni_));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Require all signers to sign.
        env(signers(
                alice,
                0x3FFFC,
                {{becky, 0xFFFF}, {cheri, 0xFFFF}, {daria, 0xFFFF}, {jinni_, 0xFFFF}}),
            Sig(alie));
        env.close();
        env.require(Owners(alice, 1));

        aliceSeq = env.seq(alice);
        env(noop(alice), Fee(9 * baseFee), Msig(becky, Reg{cheri, cher}, Reg{daria, dari}, jinni_));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Try cheri with both key types.
        aliceSeq = env.seq(alice);
        env(noop(alice), Fee(5 * baseFee), Msig(becky, cheri, Reg{daria, dari}, jinni_));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Makes sure the maximum allowed number of signers works.
        env(signers(
                alice,
                0x7FFF8,
                {{becky, 0xFFFF},
                 {cheri, 0xFFFF},
                 {daria, 0xFFFF},
                 {haunt_, 0xFFFF},
                 {jinni_, 0xFFFF},
                 {phase_, 0xFFFF},
                 {shade_, 0xFFFF},
                 {spook_, 0xFFFF}}),
            Sig(alie));
        env.close();
        env.require(Owners(alice, 1));

        aliceSeq = env.seq(alice);
        env(noop(alice),
            Fee(9 * baseFee),
            Msig(
                becky, Reg{cheri, cher}, Reg{daria, dari}, haunt_, jinni_, phase_, shade_, spook_));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // One signer short should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            Msig(becky, cheri, haunt_, jinni_, phase_, shade_, spook_),
            Fee(8 * baseFee),
            Ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::kNone), Sig(alie));
        env.close();
        env.require(Owners(alice, 0));
    }

    // We want to always leave an account signable.  Make sure the that we
    // disallow removing the last way a transaction may be signed.
    void
    testKeyDisable(FeatureBitset features)
    {
        testcase("Key Disable");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // There are three negative tests we need to make:
        //  M0. A lone master key cannot be disabled.
        //  R0. A lone regular key cannot be removed.
        //  L0. A lone signer list cannot be removed.
        //
        // Additionally, there are 6 positive tests we need to make:
        //  M1. The master key can be disabled if there's a regular key.
        //  M2. The master key can be disabled if there's a signer list.
        //
        //  R1. The regular key can be removed if there's a signer list.
        //  R2. The regular key can be removed if the master key is enabled.
        //
        //  L1. The signer list can be removed if the master key is enabled.
        //  L2. The signer list can be removed if there's a regular key.

        // Master key tests.
        // M0: A lone master key cannot be disabled.
        env(fset(alice, asfDisableMaster), Sig(alice), Ter(tecNO_ALTERNATIVE_KEY));

        // Add a regular key.
        Account const alie{"alie", KeyType::Ed25519};
        env(regkey(alice, alie));

        // M1: The master key can be disabled if there's a regular key.
        env(fset(alice, asfDisableMaster), Sig(alice));

        // R0: A lone regular key cannot be removed.
        env(regkey(alice, kDisabled), Sig(alie), Ter(tecNO_ALTERNATIVE_KEY));

        // Add a signer list.
        env(signers(alice, 1, {{bogie_, 1}}), Sig(alie));

        // R1: The regular key can be removed if there's a signer list.
        env(regkey(alice, kDisabled), Sig(alie));

        // L0: A lone signer list cannot be removed.
        auto const baseFee = env.current()->fees().base;
        env(signers(alice, jtx::kNone), Msig(bogie_), Fee(2 * baseFee), Ter(tecNO_ALTERNATIVE_KEY));

        // Enable the master key.
        env(fclear(alice, asfDisableMaster), Msig(bogie_), Fee(2 * baseFee));

        // L1: The signer list can be removed if the master key is enabled.
        env(signers(alice, jtx::kNone), Msig(bogie_), Fee(2 * baseFee));

        // Add a signer list.
        env(signers(alice, 1, {{bogie_, 1}}), Sig(alice));

        // M2: The master key can be disabled if there's a signer list.
        env(fset(alice, asfDisableMaster), Sig(alice));

        // Add a regular key.
        env(regkey(alice, alie), Msig(bogie_), Fee(2 * baseFee));

        // L2: The signer list can be removed if there's a regular key.
        env(signers(alice, jtx::kNone), Sig(alie));

        // Enable the master key.
        env(fclear(alice, asfDisableMaster), Sig(alie));

        // R2: The regular key can be removed if the master key is enabled.
        env(regkey(alice, kDisabled), Sig(alie));
    }

    // Verify that the first regular key can be made for free using the
    // master key, but not when multisigning.
    void
    testRegKey(FeatureBitset features)
    {
        testcase("Regular Key");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Secp256k1};
        env.fund(XRP(1000), alice);
        env.close();

        // Give alice a regular key with a zero fee.  Should succeed.  Once.
        Account const alie{"alie", KeyType::Ed25519};
        env(regkey(alice, alie), Sig(alice), Fee(0));

        // Try it again and creating the regular key for free should fail.
        Account const liss{"liss", KeyType::Secp256k1};
        env(regkey(alice, liss), Sig(alice), Fee(0), Ter(telINSUF_FEE_P));

        // But paying to create a regular key should succeed.
        env(regkey(alice, liss), Sig(alice));

        // In contrast, trying to multisign for a regular key with a zero
        // fee should always fail.  Even the first time.
        Account const becky{"becky", KeyType::Ed25519};
        env.fund(XRP(1000), becky);
        env.close();

        env(signers(becky, 1, {{alice, 1}}), Sig(becky));
        env(regkey(becky, alie), Msig(alice), Fee(0), Ter(telINSUF_FEE_P));

        // Using the master key to sign for a regular key for free should
        // still work.
        env(regkey(becky, alie), Sig(becky), Fee(0));
    }

    // See if every kind of transaction can be successfully multi-signed.
    void
    testTxTypes(FeatureBitset features)
    {
        testcase("Transaction Types");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Secp256k1};
        Account const becky{"becky", KeyType::Ed25519};
        Account const zelda{"zelda", KeyType::Secp256k1};
        Account const gw{"gw"};
        auto const usd = gw["USD"];
        env.fund(XRP(1000), alice, becky, zelda, gw);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), Sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie_, 1}}), Sig(alie));
        env.close();
        env.require(Owners(alice, 1));

        // Multisign a ttPAYMENT.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(pay(alice, env.master, XRP(1)), Msig(becky, bogie_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttACCOUNT_SET.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(becky, bogie_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttREGULAR_KEY_SET.
        aliceSeq = env.seq(alice);
        Account const ace{"ace", KeyType::Secp256k1};
        env(regkey(alice, ace), Msig(becky, bogie_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttTRUST_SET
        env(trust("alice", usd(100)),
            Msig(becky, bogie_),
            Fee(3 * baseFee),
            Require(lines("alice", 1)));
        env.close();
        env.require(Owners(alice, 2));

        // Multisign a ttOFFER_CREATE transaction.
        env(pay(gw, alice, usd(50)));
        env.close();
        env.require(Balance(alice, usd(50)));
        env.require(Balance(gw, alice["USD"](-50)));

        std::uint32_t const offerSeq = env.seq(alice);
        env(offer(alice, XRP(50), usd(50)), Msig(becky, bogie_), Fee(3 * baseFee));
        env.close();
        env.require(Owners(alice, 3));

        // Now multisign a ttOFFER_CANCEL canceling the offer we just created.
        {
            aliceSeq = env.seq(alice);
            env(offerCancel(alice, offerSeq), Seq(aliceSeq), Msig(becky, bogie_), Fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env.require(Owners(alice, 2));
        }

        // Multisign a ttSIGNER_LIST_SET.
        env(signers(alice, 3, {{becky, 1}, {bogie_, 1}, {demon_, 1}}),
            Msig(becky, bogie_),
            Fee(3 * baseFee));
        env.close();
        env.require(Owners(alice, 2));
    }

    void
    testBadSignatureText(FeatureBitset features)
    {
        testcase("Bad Signature Text");

        // Verify that the text returned for signature failures is correct.
        using namespace jtx;

        Env env{*this, features};

        // lambda that submits an STTx and returns the resulting JSON.
        auto submitSTTx = [&env](STTx const& stx) {
            json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(stx.getSerializer().slice());
            return env.rpc("json", "submit", to_string(jvResult));
        };

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();
        env(signers(alice, 1, {{bogie_, 1}, {demon_, 1}}), Sig(alice));

        auto const baseFee = env.current()->fees().base;
        {
            // Single-sign, but leave an empty SigningPubKey.
            JTx const tx = env.jt(noop(alice), Sig(alice));
            STTx local = *(tx.stx);
            local.setFieldVL(sfSigningPubKey, Blob());  // Empty SigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Empty SigningPubKey.");
        }
        {
            // Single-sign, but invalidate the signature.
            JTx const tx = env.jt(noop(alice), Sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto badSig = local.getFieldVL(sfTxnSignature);
            badSig[20] ^= 0xAA;
            local.setFieldVL(sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid signature.");
        }
        {
            // Single-sign, but invalidate the sequence number.
            JTx const tx = env.jt(noop(alice), Sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto seq = local.getFieldU32(sfSequence);
            local.setFieldU32(sfSequence, seq + 1);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid signature.");
        }
        {
            // Multisign, but leave a nonempty sfSigningPubKey.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_));
            STTx local = *(tx.stx);
            local[sfSigningPubKey] = alice.pk();  // Insert sfSigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Both multi- and single-sign with an empty SigningPubKey.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_));
            STTx local = *(tx.stx);
            local.sign(alice.pk(), alice.sk());
            local.setFieldVL(sfSigningPubKey, Blob());  // Empty SigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Multisign but invalidate one of the signatures.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto& signer = local.peekFieldArray(sfSigners).back();
            auto badSig = signer.getFieldVL(sfTxnSignature);
            badSig[20] ^= 0xAA;
            signer.setFieldVL(sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception].asString().find(
                    "Invalid signature on account r") != std::string::npos);
        }
        {
            // Multisign with an empty signers array should fail.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_));
            STTx local = *(tx.stx);
            local.peekFieldArray(sfSigners).clear();  // Empty Signers array.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid Signers array size.");
        }
        {
            JTx const tx = env.jt(
                noop(alice),
                Fee(2 * baseFee),

                Msig(
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_,
                    bogie_));
            STTx const local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid Signers array size.");
        }
        {
            // The account owner may not multisign for themselves.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(alice));
            STTx const local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid multisigner.");
        }
        {
            // No duplicate multisignatures allowed.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_, bogie_));
            STTx const local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Duplicate Signers not allowed.");
        }
        {
            // Multisignatures must be submitted in sorted order.
            JTx const tx = env.jt(noop(alice), Fee(2 * baseFee), Msig(bogie_, demon_));
            STTx local = *(tx.stx);
            // Unsort the Signers array.
            auto& signers = local.peekFieldArray(sfSigners);
            std::ranges::reverse(signers);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Unsorted Signers array.");
        }
    }

    void
    testNoMultiSigners(FeatureBitset features)
    {
        testcase("No Multisigners");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        Account const becky{"becky", KeyType::Secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        auto const baseFee = env.current()->fees().base;
        env(noop(alice), Msig(becky, demon_), Fee(3 * baseFee), Ter(tefNOT_MULTI_SIGNING));
    }

    void
    testMultisigningMultisigner(FeatureBitset features)
    {
        testcase("Multisigning multisigner");

        // Set up a signer list where one of the signers has both the
        // master disabled and no regular key (because that signer is
        // exclusively multisigning).  That signer should no longer be
        // able to successfully sign the signer list.

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        Account const becky{"becky", KeyType::Secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        // alice sets up a signer list with becky as a signer.
        env(signers(alice, 1, {{becky, 1}}));
        env.close();

        // becky sets up her signer list.
        env(signers(becky, 1, {{bogie_, 1}, {demon_, 1}}));
        env.close();

        // Because becky has not (yet) disabled her master key, she can
        // multisign a transaction for alice.
        auto const baseFee = env.current()->fees().base;
        env(noop(alice), Msig(becky), Fee(2 * baseFee));
        env.close();

        // Now becky disables her master key.
        env(fset(becky, asfDisableMaster));
        env.close();

        // Since becky's master key is disabled she can no longer
        // multisign for alice.
        env(noop(alice), Msig(becky), Fee(2 * baseFee), Ter(tefMASTER_DISABLED));
        env.close();

        // Becky cannot 2-level multisign for alice.  2-level multisigning
        // is not supported.
        env(noop(alice), Msig(Reg{becky, bogie_}), Fee(2 * baseFee), Ter(tefBAD_SIGNATURE));
        env.close();

        // Verify that becky cannot sign with a regular key that she has
        // not yet enabled.
        Account const beck{"beck", KeyType::Ed25519};
        env(noop(alice), Msig(Reg{becky, beck}), Fee(2 * baseFee), Ter(tefBAD_SIGNATURE));
        env.close();

        // Once becky gives herself the regular key, she can sign for alice
        // using that regular key.
        env(regkey(becky, beck), Msig(demon_), Fee(2 * baseFee));
        env.close();

        env(noop(alice), Msig(Reg{becky, beck}), Fee(2 * baseFee));
        env.close();

        // The presence of becky's regular key does not influence whether she
        // can 2-level multisign; it still won't work.
        env(noop(alice), Msig(Reg{becky, demon_}), Fee(2 * baseFee), Ter(tefBAD_SIGNATURE));
        env.close();
    }

    void
    testSignForHash(FeatureBitset features)
    {
        testcase("sign_for Hash");

        // Make sure that the "hash" field returned by the "sign_for" RPC
        // command matches the hash returned when that command is sent
        // through "submit_multisigned".  Make sure that hash also locates
        // the transaction in the ledger.
        using namespace jtx;
        Account const alice{"alice", KeyType::Ed25519};

        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                return cfg;
            }),
            features);
        env.fund(XRP(1000), alice);
        env.close();

        env(signers(alice, 2, {{bogie_, 1}, {ghost_, 1}}));
        env.close();

        // Use sign_for to sign a transaction where alice pays 10 XRP to
        // masterpassphrase.
        auto const baseFee = env.current()->fees().base;
        json::Value jvSig1;
        jvSig1[jss::account] = bogie_.human();
        jvSig1[jss::secret] = bogie_.name();
        jvSig1[jss::tx_json][jss::Account] = alice.human();
        jvSig1[jss::tx_json][jss::Amount] = 10000000;
        jvSig1[jss::tx_json][jss::Destination] = env.master.human();
        jvSig1[jss::tx_json][jss::Fee] = (3 * baseFee).jsonClipped();
        jvSig1[jss::tx_json][jss::Sequence] = env.seq(alice);
        jvSig1[jss::tx_json][jss::TransactionType] = jss::Payment;

        json::Value jvSig2 = env.rpc("json", "sign_for", to_string(jvSig1));
        BEAST_EXPECT(jvSig2[jss::result][jss::status].asString() == "success");

        // Save the hash with one signature for use later.
        std::string const hash1 = jvSig2[jss::result][jss::tx_json][jss::hash].asString();

        // Add the next signature and sign again.
        jvSig2[jss::result][jss::account] = ghost_.human();
        jvSig2[jss::result][jss::secret] = ghost_.name();
        json::Value jvSubmit = env.rpc("json", "sign_for", to_string(jvSig2[jss::result]));
        BEAST_EXPECT(jvSubmit[jss::result][jss::status].asString() == "success");

        // Save the hash with two signatures for use later.
        std::string const hash2 = jvSubmit[jss::result][jss::tx_json][jss::hash].asString();
        BEAST_EXPECT(hash1 != hash2);

        // Submit the result of the two signatures.
        json::Value jvResult =
            env.rpc("json", "submit_multisigned", to_string(jvSubmit[jss::result]));
        BEAST_EXPECT(jvResult[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(jvResult[jss::result][jss::engine_result].asString() == "tesSUCCESS");

        // The hash from the submit should be the same as the hash from the
        // second signing.
        BEAST_EXPECT(hash2 == jvResult[jss::result][jss::tx_json][jss::hash].asString());
        env.close();

        // The transaction we just submitted should now be available and
        // validated.
        json::Value jvTx = env.rpc("tx", hash2);
        BEAST_EXPECT(jvTx[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(jvTx[jss::result][jss::validated].asString() == "true");
        BEAST_EXPECT(
            jvTx[jss::result][jss::meta][sfTransactionResult.jsonName].asString() == "tesSUCCESS");
    }

    void
    testSignersWithTickets(FeatureBitset features)
    {
        testcase("Signers With Tickets");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(2000), alice);
        env.close();

        // Create a few tickets that alice can use up.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 20));
        env.close();
        std::uint32_t const aliceSeq = env.seq(alice);

        // Attach phantom signers to alice using a ticket.
        env(signers(alice, 1, {{bogie_, 1}, {demon_, 1}}), ticket::Use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        auto const baseFee = env.current()->fees().base;
        env(noop(alice), Msig(bogie_, demon_), Fee(3 * baseFee), ticket::Use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Should also be able to remove the signer list using a ticket.
        env(signers(alice, jtx::kNone), ticket::Use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testSignersWithTags(FeatureBitset features)
    {
        testcase("Signers With Tags");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();
        uint8_t tag1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03,
                          0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                          0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        uint8_t tag2[] = "hello world some ascii 32b long";  // including 1 byte for NUL

        uint256 bogieTag = xrpl::BaseUInt<256>::fromVoid(tag1);
        uint256 demonTag = xrpl::BaseUInt<256>::fromVoid(tag2);

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie_, 1, bogieTag}, {demon_, 1, demonTag}}));
        env.close();
        env.require(Owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, demon_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(demon_), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            Msig(demon_, demon_),
            Fee(3 * baseFee),
            Rpc("invalidTransaction", "fails local checks: Duplicate Signers not allowed."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, spook_), Fee(3 * baseFee), Ter(tefBAD_SIGNATURE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie_, 1}, {demon_, 1}}));
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_), Fee(2 * baseFee), Ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq(alice);
        env(noop(alice), Msig(bogie_, demon_), Fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testSignerListSetFlags(FeatureBitset features)
    {
        using namespace test::jtx;

        Env env{*this, features};
        Account const alice{"alice"};

        env.fund(XRP(1000), alice);
        env.close();

        bool const enabled = features[fixInvalidTxFlags];
        testcase(std::string("SignerListSet flag, fix ") + (enabled ? "enabled" : "disabled"));

        Ter const expected(enabled ? TER(temINVALID_FLAG) : TER(tesSUCCESS));
        env(signers(alice, 2, {{bogie_, 1}, {ghost_, 1}}), expected, Txflags(tfPassive));
        env.close();
    }

    void
    testSignerListObject(FeatureBitset features)
    {
        testcase("SignerList Object");

        // Verify that the SignerList object is created correctly.
        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach phantom signers to alice.
        env(signers(alice, 1, {{bogie_, 1}, {demon_, 1}}));
        env.close();

        // Verify that the SignerList object was created correctly.
        auto const& sle = env.le(keylet::signers(alice.id()));
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldArray(sfSignerEntries).size() == 2);
        if (features[fixIncludeKeyletFields])
        {
            BEAST_EXPECT((*sle)[sfOwner] == alice.id());
        }
        else
        {
            BEAST_EXPECT(!sle->isFieldPresent(sfOwner));
        }
    }

    void
    testAll(FeatureBitset features)
    {
        testNoReserve(features);
        testSignerListSet(features);
        testPhantomSigners(features);
        testFee(features);
        testMisorderedSigners(features);
        testMasterSigners(features);
        testRegularSigners(features);
        testRegularSignersUsingSubmitMulti(features);
        testHeterogeneousSigners(features);
        testKeyDisable(features);
        testRegKey(features);
        testTxTypes(features);
        testBadSignatureText(features);
        testNoMultiSigners(features);
        testMultisigningMultisigner(features);
        testSignForHash(features);
        testSignersWithTickets(features);
        testSignersWithTags(features);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const all = testableAmendments();

        testAll(all);

        testSignerListSetFlags(all - fixInvalidTxFlags);
        testSignerListSetFlags(all);

        testSignerListObject(all - fixIncludeKeyletFields);
        testSignerListObject(all);
    }
};

BEAST_DEFINE_TESTSUITE(MultiSign, app, xrpl);

}  // namespace xrpl::test
