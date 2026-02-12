#include <test/jtx.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {

class MultiSign_test : public beast::unit_test::suite
{
    // Unfunded accounts to use for phantom signing.
    jtx::Account const bogie{"bogie", KeyType::secp256k1};
    jtx::Account const demon{"demon", KeyType::ed25519};
    jtx::Account const ghost{"ghost", KeyType::secp256k1};
    jtx::Account const haunt{"haunt", KeyType::ed25519};
    jtx::Account const jinni{"jinni", KeyType::secp256k1};
    jtx::Account const phase{"phase", KeyType::ed25519};
    jtx::Account const shade{"shade", KeyType::secp256k1};
    jtx::Account const spook{"spook", KeyType::ed25519};
    jtx::Account const acc10{"acc10", KeyType::ed25519};
    jtx::Account const acc11{"acc11", KeyType::ed25519};
    jtx::Account const acc12{"acc12", KeyType::ed25519};
    jtx::Account const acc13{"acc13", KeyType::ed25519};
    jtx::Account const acc14{"acc14", KeyType::ed25519};
    jtx::Account const acc15{"acc15", KeyType::ed25519};
    jtx::Account const acc16{"acc16", KeyType::ed25519};
    jtx::Account const acc17{"acc17", KeyType::ed25519};
    jtx::Account const acc18{"acc18", KeyType::ed25519};
    jtx::Account const acc19{"acc19", KeyType::ed25519};
    jtx::Account const acc20{"acc20", KeyType::ed25519};
    jtx::Account const acc21{"acc21", KeyType::ed25519};
    jtx::Account const acc22{"acc22", KeyType::ed25519};
    jtx::Account const acc23{"acc23", KeyType::ed25519};
    jtx::Account const acc24{"acc24", KeyType::ed25519};
    jtx::Account const acc25{"acc25", KeyType::ed25519};
    jtx::Account const acc26{"acc26", KeyType::ed25519};
    jtx::Account const acc27{"acc27", KeyType::ed25519};
    jtx::Account const acc28{"acc28", KeyType::ed25519};
    jtx::Account const acc29{"acc29", KeyType::ed25519};
    jtx::Account const acc30{"acc30", KeyType::ed25519};
    jtx::Account const acc31{"acc31", KeyType::ed25519};
    jtx::Account const acc32{"acc32", KeyType::ed25519};
    jtx::Account const acc33{"acc33", KeyType::ed25519};

public:
    void
    testNoReserve(FeatureBitset features)
    {
        testcase("No Reserve");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        auto const fee = env.current()->fees().base;
        env.fund(XRP(250) - drops(1), alice);
        env.close();
        env.require(owners(alice, 0));

        {
            // Attach a signer list to alice.  Should fail.
            Json::Value signersList = signers(alice, 1, {{bogie, 1}});
            env(signersList, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(owners(alice, 0));

            // Fund alice enough to set the signer list, then attach signers.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(signersList);
            env.close();
            env.require(owners(alice, 1));
        }
        {
            // Pay alice enough to almost make the reserve for the biggest
            // possible list.
            env(pay(env.master, alice, fee - drops(1)));

            // Replace with the biggest possible signer list.  Should fail.
            Json::Value bigSigners = signers(
                alice,
                1,
                {{bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1}, {jinni, 1}, {phase, 1}, {shade, 1}, {spook, 1}});
            env(bigSigners, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(owners(alice, 1));

            // Fund alice one more drop (plus the fee) and succeed.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(bigSigners);
            env.close();
            env.require(owners(alice, 1));
        }
        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none));
        env.close();
        env.require(owners(alice, 0));
    }

    void
    testSignerListSet(FeatureBitset features)
    {
        testcase("SignerListSet");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Add alice as a multisigner for herself.  Should fail.
        env(signers(alice, 1, {{alice, 1}}), ter(temBAD_SIGNER));

        // Add a signer with a weight of zero.  Should fail.
        env(signers(alice, 1, {{bogie, 0}}), ter(temBAD_WEIGHT));

        // Add a signer where the weight is too big.  Should fail since
        // the weight field is only 16 bits.  The jtx framework can't do
        // this kind of test, so it's commented out.
        //      env(signers(alice, 1, { { bogie, 0x10000} }), ter
        //      (temBAD_WEIGHT));

        // Add the same signer twice.  Should fail.
        env(signers(
                alice,
                1,
                {{bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1}, {jinni, 1}, {phase, 1}, {demon, 1}, {spook, 1}}),
            ter(temBAD_SIGNER));

        // Set a quorum of zero.  Should fail.
        env(signers(alice, 0, {{bogie, 1}}), ter(temMALFORMED));

        // Make a signer list where the quorum can't be met.  Should fail.
        env(signers(
                alice,
                9,
                {{bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1}, {jinni, 1}, {phase, 1}, {shade, 1}, {spook, 1}}),
            ter(temBAD_QUORUM));

        // clang-format off
        // Make a signer list that's too big.  Should fail.
        Account const spare("spare", KeyType::secp256k1);
        env(signers(
                alice,
                1,
                std::vector<signer>{{bogie, 1}, {demon, 1}, {ghost, 1},
                                          {haunt, 1}, {jinni, 1}, {phase, 1},
                                          {shade, 1}, {spook, 1}, {spare, 1},
                                          {acc10, 1}, {acc11, 1}, {acc12, 1},
                                          {acc13, 1}, {acc14, 1}, {acc15, 1},
                                          {acc16, 1}, {acc17, 1}, {acc18, 1},
                                          {acc19, 1}, {acc20, 1}, {acc21, 1},
                                          {acc22, 1}, {acc23, 1}, {acc24, 1},
                                          {acc25, 1}, {acc26, 1}, {acc27, 1},
                                          {acc28, 1}, {acc29, 1}, {acc30, 1},
                                          {acc31, 1}, {acc32, 1}, {acc33, 1}}),
            ter(temMALFORMED));
        // clang-format on
        env.close();
        env.require(owners(alice, 0));
    }

    void
    testPhantomSigners(FeatureBitset features)
    {
        testcase("Phantom Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        env.close();
        env.require(owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(demon), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(demon, demon),
            fee(3 * baseFee),
            rpc("invalidTransaction", "fails local checks: Duplicate Signers not allowed."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, spook), fee(3 * baseFee), ter(tefBAD_SIGNATURE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}));
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testFee(FeatureBitset features)
    {
        testcase("Fee");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach maximum possible number of signers to alice.
        env(signers(
            alice,
            1,
            {{bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1}, {jinni, 1}, {phase, 1}, {shade, 1}, {spook, 1}}));
        env.close();
        env.require(owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee((2 * baseFee) - 1), ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, demon, ghost, haunt, jinni, phase, shade, spook), fee(9 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(bogie, demon, ghost, haunt, jinni, phase, shade, spook),
            fee((9 * baseFee) - 1),
            ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testMisorderedSigners(FeatureBitset features)
    {
        testcase("Misordered Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // The signatures in a transaction must be submitted in sorted order.
        // Make sure the transaction fails if they are not.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        env.close();
        env.require(owners(alice, 1));

        msig phantoms{bogie, demon};
        std::reverse(phantoms.signers.begin(), phantoms.signers.end());
        std::uint32_t const aliceSeq = env.seq(alice);
        env(noop(alice), phantoms, rpc("invalidTransaction", "fails local checks: Unsorted Signers array."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testMasterSigners(FeatureBitset features)
    {
        testcase("Master Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        Account const becky{"becky", KeyType::secp256k1};
        Account const cheri{"cheri", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // For a different situation, give alice a regular key but don't use it.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env.close();
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), sig(alice));
        env(noop(alice), sig(alie));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

        // Attach signers to alice
        env(signers(alice, 4, {{becky, 3}, {cheri, 4}}), sig(alice));
        env.close();
        env.require(owners(alice, 1));

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // If we don't meet the quorum the transaction should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky), fee(2 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Give becky and cheri regular keys.
        Account const beck{"beck", KeyType::ed25519};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::ed25519};
        env(regkey(cheri, cher));
        env.close();

        // becky's and cheri's master keys should still work.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky, cheri), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    testRegularSigners(FeatureBitset features)
    {
        testcase("Regular Signers");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}}), sig(alice));

        // Give everyone regular keys.
        Account const alie{"alie", KeyType::ed25519};
        env(regkey(alice, alie));
        Account const beck{"beck", KeyType::secp256k1};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::ed25519};
        env(regkey(cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset(cheri, asfDisableMaster), sig(cheri));
        env.close();

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // cheri should not be able to multisign using her master key.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee), ter(tefMASTER_DISABLED));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // becky should be able to multisign using either of her keys.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(Reg{becky, beck}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Both becky and cheri should be able to sign using regular keys.
        aliceSeq = env.seq(alice);
        env(noop(alice), fee(3 * baseFee), msig(Reg{becky, beck}, Reg{cheri, cher}));
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
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {cheri, 1}}), sig(alice));

        // Give everyone regular keys.
        Account const beck{"beck", KeyType::secp256k1};
        env(regkey(becky, beck));
        Account const cher{"cher", KeyType::ed25519};
        env(regkey(cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset(cheri, asfDisableMaster), sig(cheri));
        env.close();

        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq;

        // these represent oft-repeated setup for input json below
        auto setup_tx = [&]() -> Json::Value {
            Json::Value jv;
            jv[jss::tx_json][jss::Account] = alice.human();
            jv[jss::tx_json][jss::TransactionType] = jss::AccountSet;
            jv[jss::tx_json][jss::Fee] = (8 * baseFee).jsonClipped();
            jv[jss::tx_json][jss::Sequence] = env.seq(alice);
            jv[jss::tx_json][jss::SigningPubKey] = "";
            return jv;
        };
        auto cheri_sign = [&](Json::Value& jv) {
            jv[jss::account] = cheri.human();
            jv[jss::key_type] = "ed25519";
            jv[jss::passphrase] = cher.name();
        };
        auto becky_sign = [&](Json::Value& jv) {
            jv[jss::account] = becky.human();
            jv[jss::secret] = beck.name();
        };

        {
            // Attempt a multisigned transaction that meets the quorum.
            // using sign_for and submit_multisigned
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            Json::Value jv_two;
            jv_two[jss::tx_json] = jrr[jss::tx_json];
            becky_sign(jv_two);
            jrr = env.rpc("json", "sign_for", to_string(jv_two))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            Json::Value jv_submit;
            jv_submit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        {
            // failure case -- SigningPubKey not empty
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::SigningPubKey] = strHex(alice.pk().slice());
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "When multi-signing 'tx_json.SigningPubKey' must be empty.");
        }

        {
            // failure case - bad fee
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::Fee] = -1;
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            Json::Value jv_two;
            jv_two[jss::tx_json] = jrr[jss::tx_json];
            becky_sign(jv_two);
            jrr = env.rpc("json", "sign_for", to_string(jv_two))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            Json::Value jv_submit;
            jv_submit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid Fee field.  Fees must be greater than zero.");
        }

        {
            // failure case - bad fee v2
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::Fee] = alice["USD"](10).value().getFullText();
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            Json::Value jv_two;
            jv_two[jss::tx_json] = jrr[jss::tx_json];
            becky_sign(jv_two);
            jrr = env.rpc("json", "sign_for", to_string(jv_two))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            Json::Value jv_submit;
            jv_submit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "internal");
            BEAST_EXPECT(jrr[jss::error_message] == "Internal error.");
        }

        {
            // cheri should not be able to multisign using her master key.
            aliceSeq = env.seq(alice);
            Json::Value jv = setup_tx();
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
            Json::Value jv_one = setup_tx();
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            Json::Value jv_two;
            jv_two[jss::tx_json] = jrr[jss::tx_json];
            jv_two[jss::account] = becky.human();
            jv_two[jss::key_type] = "ed25519";
            jv_two[jss::passphrase] = becky.name();
            jrr = env.rpc("json", "sign_for", to_string(jv_two))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            Json::Value jv_submit;
            jv_submit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        {
            // check for bad or bogus accounts in the tx
            Json::Value jv = setup_tx();
            jv[jss::tx_json][jss::Account] = "DEADBEEF";
            cheri_sign(jv);
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
            Json::Value jv = setup_tx();
            jv[jss::tx_json][sfSigners.fieldName] = Json::Value{Json::arrayValue};
            becky_sign(jv);
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
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        Account const daria{"daria", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri, daria);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // becky is master only without a regular key.

        // cheri has a regular key, but leaves the master key enabled.
        Account const cher{"cher", KeyType::secp256k1};
        env(regkey(cheri, cher));

        // daria has a regular key and disables her master key.
        Account const dari{"dari", KeyType::ed25519};
        env(regkey(daria, dari));
        env(fset(daria, asfDisableMaster), sig(daria));
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}, {daria, 1}, {jinni, 1}}), sig(alie));
        env.close();
        env.require(owners(alice, 1));

        // Each type of signer should succeed individually.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(Reg{daria, dari}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(jinni), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        //  Should also work if all signers sign.
        aliceSeq = env.seq(alice);
        env(noop(alice), fee(5 * baseFee), msig(becky, Reg{cheri, cher}, Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Require all signers to sign.
        env(signers(alice, 0x3FFFC, {{becky, 0xFFFF}, {cheri, 0xFFFF}, {daria, 0xFFFF}, {jinni, 0xFFFF}}), sig(alie));
        env.close();
        env.require(owners(alice, 1));

        aliceSeq = env.seq(alice);
        env(noop(alice), fee(9 * baseFee), msig(becky, Reg{cheri, cher}, Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Try cheri with both key types.
        aliceSeq = env.seq(alice);
        env(noop(alice), fee(5 * baseFee), msig(becky, cheri, Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Makes sure the maximum allowed number of signers works.
        env(signers(
                alice,
                0x7FFF8,
                {{becky, 0xFFFF},
                 {cheri, 0xFFFF},
                 {daria, 0xFFFF},
                 {haunt, 0xFFFF},
                 {jinni, 0xFFFF},
                 {phase, 0xFFFF},
                 {shade, 0xFFFF},
                 {spook, 0xFFFF}}),
            sig(alie));
        env.close();
        env.require(owners(alice, 1));

        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(9 * baseFee),
            msig(becky, Reg{cheri, cher}, Reg{daria, dari}, haunt, jinni, phase, shade, spook));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // One signer short should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky, cheri, haunt, jinni, phase, shade, spook), fee(8 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none), sig(alie));
        env.close();
        env.require(owners(alice, 0));
    }

    // We want to always leave an account signable.  Make sure the that we
    // disallow removing the last way a transaction may be signed.
    void
    testKeyDisable(FeatureBitset features)
    {
        testcase("Key Disable");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
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
        env(fset(alice, asfDisableMaster), sig(alice), ter(tecNO_ALTERNATIVE_KEY));

        // Add a regular key.
        Account const alie{"alie", KeyType::ed25519};
        env(regkey(alice, alie));

        // M1: The master key can be disabled if there's a regular key.
        env(fset(alice, asfDisableMaster), sig(alice));

        // R0: A lone regular key cannot be removed.
        env(regkey(alice, disabled), sig(alie), ter(tecNO_ALTERNATIVE_KEY));

        // Add a signer list.
        env(signers(alice, 1, {{bogie, 1}}), sig(alie));

        // R1: The regular key can be removed if there's a signer list.
        env(regkey(alice, disabled), sig(alie));

        // L0: A lone signer list cannot be removed.
        auto const baseFee = env.current()->fees().base;
        env(signers(alice, jtx::none), msig(bogie), fee(2 * baseFee), ter(tecNO_ALTERNATIVE_KEY));

        // Enable the master key.
        env(fclear(alice, asfDisableMaster), msig(bogie), fee(2 * baseFee));

        // L1: The signer list can be removed if the master key is enabled.
        env(signers(alice, jtx::none), msig(bogie), fee(2 * baseFee));

        // Add a signer list.
        env(signers(alice, 1, {{bogie, 1}}), sig(alice));

        // M2: The master key can be disabled if there's a signer list.
        env(fset(alice, asfDisableMaster), sig(alice));

        // Add a regular key.
        env(regkey(alice, alie), msig(bogie), fee(2 * baseFee));

        // L2: The signer list can be removed if there's a regular key.
        env(signers(alice, jtx::none), sig(alie));

        // Enable the master key.
        env(fclear(alice, asfDisableMaster), sig(alie));

        // R2: The regular key can be removed if the master key is enabled.
        env(regkey(alice, disabled), sig(alie));
    }

    // Verify that the first regular key can be made for free using the
    // master key, but not when multisigning.
    void
    testRegKey(FeatureBitset features)
    {
        testcase("Regular Key");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};
        env.fund(XRP(1000), alice);
        env.close();

        // Give alice a regular key with a zero fee.  Should succeed.  Once.
        Account const alie{"alie", KeyType::ed25519};
        env(regkey(alice, alie), sig(alice), fee(0));

        // Try it again and creating the regular key for free should fail.
        Account const liss{"liss", KeyType::secp256k1};
        env(regkey(alice, liss), sig(alice), fee(0), ter(telINSUF_FEE_P));

        // But paying to create a regular key should succeed.
        env(regkey(alice, liss), sig(alice));

        // In contrast, trying to multisign for a regular key with a zero
        // fee should always fail.  Even the first time.
        Account const becky{"becky", KeyType::ed25519};
        env.fund(XRP(1000), becky);
        env.close();

        env(signers(becky, 1, {{alice, 1}}), sig(becky));
        env(regkey(becky, alie), msig(alice), fee(0), ter(telINSUF_FEE_P));

        // Using the master key to sign for a regular key for free should
        // still work.
        env(regkey(becky, alie), sig(becky), fee(0));
    }

    // See if every kind of transaction can be successfully multi-signed.
    void
    testTxTypes(FeatureBitset features)
    {
        testcase("Transaction Types");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const zelda{"zelda", KeyType::secp256k1};
        Account const gw{"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, becky, zelda, gw);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie));
        env(fset(alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig(alie));
        env.close();
        env.require(owners(alice, 1));

        // Multisign a ttPAYMENT.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(pay(alice, env.master, XRP(1)), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttACCOUNT_SET.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttREGULAR_KEY_SET.
        aliceSeq = env.seq(alice);
        Account const ace{"ace", KeyType::secp256k1};
        env(regkey(alice, ace), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttTRUST_SET
        env(trust("alice", USD(100)), msig(becky, bogie), fee(3 * baseFee), require(lines("alice", 1)));
        env.close();
        env.require(owners(alice, 2));

        // Multisign a ttOFFER_CREATE transaction.
        env(pay(gw, alice, USD(50)));
        env.close();
        env.require(balance(alice, USD(50)));
        env.require(balance(gw, alice["USD"](-50)));

        std::uint32_t const offerSeq = env.seq(alice);
        env(offer(alice, XRP(50), USD(50)), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        env.require(owners(alice, 3));

        // Now multisign a ttOFFER_CANCEL canceling the offer we just created.
        {
            aliceSeq = env.seq(alice);
            env(offer_cancel(alice, offerSeq), seq(aliceSeq), msig(becky, bogie), fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env.require(owners(alice, 2));
        }

        // Multisign a ttSIGNER_LIST_SET.
        env(signers(alice, 3, {{becky, 1}, {bogie, 1}, {demon, 1}}), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        env.require(owners(alice, 2));
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
            Json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(stx.getSerializer().slice());
            return env.rpc("json", "submit", to_string(jvResult));
        };

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}), sig(alice));

        auto const baseFee = env.current()->fees().base;
        {
            // Single-sign, but leave an empty SigningPubKey.
            JTx tx = env.jt(noop(alice), sig(alice));
            STTx local = *(tx.stx);
            local.setFieldVL(sfSigningPubKey, Blob());  // Empty SigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Empty SigningPubKey.");
        }
        {
            // Single-sign, but invalidate the signature.
            JTx tx = env.jt(noop(alice), sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto badSig = local.getFieldVL(sfTxnSignature);
            badSig[20] ^= 0xAA;
            local.setFieldVL(sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Invalid signature.");
        }
        {
            // Single-sign, but invalidate the sequence number.
            JTx tx = env.jt(noop(alice), sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto seq = local.getFieldU32(sfSequence);
            local.setFieldU32(sfSequence, seq + 1);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Invalid signature.");
        }
        {
            // Multisign, but leave a nonempty sfSigningPubKey.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local[sfSigningPubKey] = alice.pk();  // Insert sfSigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] == "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Both multi- and single-sign with an empty SigningPubKey.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local.sign(alice.pk(), alice.sk());
            local.setFieldVL(sfSigningPubKey, Blob());  // Empty SigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] == "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Multisign but invalidate one of the signatures.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto& signer = local.peekFieldArray(sfSigners).back();
            auto badSig = signer.getFieldVL(sfTxnSignature);
            badSig[20] ^= 0xAA;
            signer.setFieldVL(sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception].asString().find("Invalid signature on account r") !=
                std::string::npos);
        }
        {
            // Multisign with an empty signers array should fail.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local.peekFieldArray(sfSigners).clear();  // Empty Signers array.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Invalid Signers array size.");
        }
        {
            JTx tx = env.jt(
                noop(alice),
                fee(2 * baseFee),

                msig(
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie,
                    bogie));
            STTx local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Invalid Signers array size.");
        }
        {
            // The account owner may not multisign for themselves.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(alice));
            STTx local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Invalid multisigner.");
        }
        {
            // No duplicate multisignatures allowed.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie, bogie));
            STTx local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] == "fails local checks: Duplicate Signers not allowed.");
        }
        {
            // Multisignatures must be submitted in sorted order.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie, demon));
            STTx local = *(tx.stx);
            // Unsort the Signers array.
            auto& signers = local.peekFieldArray(sfSigners);
            std::reverse(signers.begin(), signers.end());
            // Signature should fail.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Unsorted Signers array.");
        }

        if (features[featureNestedMultiSign])
        {
            Account const becky{"becky", KeyType::secp256k1};
            {
                // Nested multisign with an empty nested Signers array should
                // fail.
                JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(demon))}));
                STTx local = *(tx.stx);
                auto& nested = local.peekFieldArray(sfSigners).back().peekFieldArray(sfSigners);
                nested.clear();
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception] == "fails local checks: Invalid Signers array size.");
            }
            {
                // Nested multisign with too many nested signers should fail.
                JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(demon))}));
                STTx local = *(tx.stx);
                auto& nested = local.peekFieldArray(sfSigners).back().peekFieldArray(sfSigners);
                while (nested.size() <= STTx::maxMultiSigners)
                    nested.push_back(nested.back());
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception] == "fails local checks: Invalid Signers array size.");
            }
        }
    }

    void
    testNoMultiSigners(FeatureBitset features)
    {
        testcase("No Multisigners");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        Account const becky{"becky", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        auto const baseFee = env.current()->fees().base;
        env(noop(alice), msig(becky, demon), fee(3 * baseFee), ter(tefNOT_MULTI_SIGNING));
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
        Account const alice{"alice", KeyType::ed25519};
        Account const becky{"becky", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        // alice sets up a signer list with becky as a signer.
        env(signers(alice, 1, {{becky, 1}}));
        env.close();

        // becky sets up her signer list.
        env(signers(becky, 1, {{bogie, 1}, {demon, 1}}));
        env.close();

        // Because becky has not (yet) disabled her master key, she can
        // multisign a transaction for alice.
        auto const baseFee = env.current()->fees().base;
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();

        // Now becky disables her master key.
        env(fset(becky, asfDisableMaster));
        env.close();

        // Since becky's master key is disabled she can no longer
        // multisign for alice.
        env(noop(alice), msig(becky), fee(2 * baseFee), ter(tefMASTER_DISABLED));
        env.close();

        // Becky cannot 2-level multisign for alice.  2-level multisigning
        // is not supported.
        env(noop(alice), msig(Reg{becky, bogie}), fee(2 * baseFee), ter(tefBAD_SIGNATURE));
        env.close();

        // Verify that becky cannot sign with a regular key that she has
        // not yet enabled.
        Account const beck{"beck", KeyType::ed25519};
        env(noop(alice), msig(Reg{becky, beck}), fee(2 * baseFee), ter(tefBAD_SIGNATURE));
        env.close();

        // Once becky gives herself the regular key, she can sign for alice
        // using that regular key.
        env(regkey(becky, beck), msig(demon), fee(2 * baseFee));
        env.close();

        env(noop(alice), msig(Reg{becky, beck}), fee(2 * baseFee));
        env.close();

        // The presence of becky's regular key does not influence whether she
        // can 2-level multisign; it still won't work.
        env(noop(alice), msig(Reg{becky, demon}), fee(2 * baseFee), ter(tefBAD_SIGNATURE));
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
        Account const alice{"alice", KeyType::ed25519};

        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                return cfg;
            }),
            features);
        env.fund(XRP(1000), alice);
        env.close();

        env(signers(alice, 2, {{bogie, 1}, {ghost, 1}}));
        env.close();

        // Use sign_for to sign a transaction where alice pays 10 XRP to
        // masterpassphrase.
        auto const baseFee = env.current()->fees().base;
        Json::Value jvSig1;
        jvSig1[jss::account] = bogie.human();
        jvSig1[jss::secret] = bogie.name();
        jvSig1[jss::tx_json][jss::Account] = alice.human();
        jvSig1[jss::tx_json][jss::Amount] = 10000000;
        jvSig1[jss::tx_json][jss::Destination] = env.master.human();
        jvSig1[jss::tx_json][jss::Fee] = (3 * baseFee).jsonClipped();
        jvSig1[jss::tx_json][jss::Sequence] = env.seq(alice);
        jvSig1[jss::tx_json][jss::TransactionType] = jss::Payment;

        Json::Value jvSig2 = env.rpc("json", "sign_for", to_string(jvSig1));
        BEAST_EXPECT(jvSig2[jss::result][jss::status].asString() == "success");

        // Save the hash with one signature for use later.
        std::string const hash1 = jvSig2[jss::result][jss::tx_json][jss::hash].asString();

        // Add the next signature and sign again.
        jvSig2[jss::result][jss::account] = ghost.human();
        jvSig2[jss::result][jss::secret] = ghost.name();
        Json::Value jvSubmit = env.rpc("json", "sign_for", to_string(jvSig2[jss::result]));
        BEAST_EXPECT(jvSubmit[jss::result][jss::status].asString() == "success");

        // Save the hash with two signatures for use later.
        std::string const hash2 = jvSubmit[jss::result][jss::tx_json][jss::hash].asString();
        BEAST_EXPECT(hash1 != hash2);

        // Submit the result of the two signatures.
        Json::Value jvResult = env.rpc("json", "submit_multisigned", to_string(jvSubmit[jss::result]));
        BEAST_EXPECT(jvResult[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(jvResult[jss::result][jss::engine_result].asString() == "tesSUCCESS");

        // The hash from the submit should be the same as the hash from the
        // second signing.
        BEAST_EXPECT(hash2 == jvResult[jss::result][jss::tx_json][jss::hash].asString());
        env.close();

        // The transaction we just submitted should now be available and
        // validated.
        Json::Value jvTx = env.rpc("tx", hash2);
        BEAST_EXPECT(jvTx[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(jvTx[jss::result][jss::validated].asString() == "true");
        BEAST_EXPECT(jvTx[jss::result][jss::meta][sfTransactionResult.jsonName].asString() == "tesSUCCESS");
    }

    void
    testSignersWithTickets(FeatureBitset features)
    {
        testcase("Signers With Tickets");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(2000), alice);
        env.close();

        // Create a few tickets that alice can use up.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 20));
        env.close();
        std::uint32_t const aliceSeq = env.seq(alice);

        // Attach phantom signers to alice using a ticket.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}), ticket::use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        auto const baseFee = env.current()->fees().base;
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee), ticket::use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Should also be able to remove the signer list using a ticket.
        env(signers(alice, jtx::none), ticket::use(aliceTicketSeq++));
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
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();
        uint8_t tag1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03,
                          0x04, 0x05, 0x06, 0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                          0x07, 0x08, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        uint8_t tag2[] = "hello world some ascii 32b long";  // including 1 byte for NUL

        uint256 bogie_tag = xrpl::base_uint<256>::fromVoid(tag1);
        uint256 demon_tag = xrpl::base_uint<256>::fromVoid(tag2);

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie, 1, bogie_tag}, {demon, 1, demon_tag}}));
        env.close();
        env.require(owners(alice, 1));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(demon), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(demon, demon),
            fee(3 * baseFee),
            rpc("invalidTransaction", "fails local checks: Duplicate Signers not allowed."));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, spook), fee(3 * baseFee), ter(tefBAD_SIGNATURE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}));
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
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

        ter const expected(enabled ? TER(temINVALID_FLAG) : TER(tesSUCCESS));
        env(signers(alice, 2, {{bogie, 1}, {ghost, 1}}), expected, txflags(tfPassive));
        env.close();
    }

    void
    testSignerListObject(FeatureBitset features)
    {
        testcase("SignerList Object");

        // Verify that the SignerList object is created correctly.
        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach phantom signers to alice.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
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
    testNestedMultiSign(FeatureBitset features)
    {
        testcase("Nested MultiSign");

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define LINE_TO_HEX_STRING                                                \
    []() -> std::string {                                                 \
        const char* line = TOSTRING(__LINE__);                            \
        int len = 0;                                                      \
        while (line[len])                                                 \
            len++;                                                        \
        std::string result;                                               \
        if (len % 2 == 1)                                                 \
        {                                                                 \
            result += (char)(0x00 * 16 + (line[0] - '0'));                \
            line++;                                                       \
        }                                                                 \
        for (int i = 0; line[i]; i += 2)                                  \
        {                                                                 \
            result += (char)((line[i] - '0') * 16 + (line[i + 1] - '0')); \
        }                                                                 \
        return result;                                                    \
    }()

#define M(m) memo(m, "", "")
#define L() memo(LINE_TO_HEX_STRING, "", "")

        using namespace jtx;
        Env env{*this, envconfig(), features};
        // Env env{*this, envconfig(), features, nullptr,
        // beast::severities::kTrace};

        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        Account const daria{"daria", KeyType::ed25519};
        Account const edgar{"edgar", KeyType::secp256k1};
        Account const fiona{"fiona", KeyType::ed25519};
        Account const grace{"grace", KeyType::secp256k1};
        Account const henry{"henry", KeyType::ed25519};
        Account const f1{"f1", KeyType::ed25519};
        Account const f2{"f2", KeyType::ed25519};
        Account const f3{"f3", KeyType::ed25519};
        env.fund(
            XRP(1000),
            alice,
            becky,
            cheri,
            daria,
            edgar,
            fiona,
            grace,
            henry,
            f1,
            f2,
            f3,
            phase,
            jinni,
            acc10,
            acc11,
            acc12);
        env.close();

        auto const baseFee = env.current()->fees().base;

        if (!features[featureNestedMultiSign])
        {
            // When feature is disabled, nested signing should fail
            env(signers(f1, 1, {{f2, 1}}));
            env(signers(f2, 1, {{f3, 1}}));
            env.close();

            std::uint32_t f1Seq = env.seq(f1);
            // Nested signers are rejected at the signature-check level in
            // STTx::checkMultiSign (via multiSignHelper) before the
            // transaction reaches preflight/preclaim.  The Env framework
            // maps RPC-level rejection to telENV_RPC_FAILED.
            env(noop(f1), msig({msigner(f2, msigner(f3))}), L(), fee(3 * baseFee), ter(telENV_RPC_FAILED));
            env.close();
            BEAST_EXPECT(env.seq(f1) == f1Seq);
            return;
        }

        // Test Case 1: Basic 2-level nested signing with quorum
        {
            // Set up signer lists with quorum requirements
            env(signers(becky, 2, {{bogie, 1}, {demon, 1}, {ghost, 1}}));
            env(signers(cheri, 3, {{haunt, 2}, {jinni, 2}}));
            env.close();

            // Alice requires quorum of 3 with weighted signers
            env(signers(alice, 3, {{becky, 2}, {cheri, 2}, {daria, 1}}));
            env.close();

            // Test 1a: becky alone (weight 2) doesn't meet alice's quorum
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(bogie), msigner(demon))}),
                L(),
                fee(4 * baseFee),
                ter(tefBAD_QUORUM));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Test 1b: becky (2) + daria (1) meets quorum of 3
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(bogie), msigner(demon)), msigner(daria)}),
                L(),
                fee(5 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Test 1c: cheri's nested signers must meet her quorum
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig(
                    {msigner(
                         becky,
                         msigner(bogie),
                         msigner(demon)),               // becky has a satisfied quorum
                     msigner(cheri, msigner(haunt))}),  // but cheri does not
                                                        // (needs jinni too)
                L(),
                fee(5 * baseFee),
                ter(tefBAD_QUORUM));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Test 1d: cheri with both signers meets her quorum
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(cheri, msigner(haunt), msigner(jinni)), msigner(daria)}),
                L(),
                fee(5 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 2: 3-level maximum depth with quorum at each level
        {
            // Level 2: phase needs direct signatures (no deeper nesting)
            env(signers(phase, 2, {{acc10, 1}, {acc11, 1}, {acc12, 1}}));

            // Level 1: jinni needs weighted signatures
            env(signers(jinni, 3, {{phase, 2}, {shade, 2}, {spook, 1}}));

            // Level 0: edgar needs 2 from weighted signers
            env(signers(edgar, 2, {{jinni, 1}, {bogie, 1}, {demon, 1}}));

            // Alice now requires edgar with weight 3
            env(signers(alice, 3, {{edgar, 3}, {fiona, 2}}));
            env.close();

            // Test 2a: 3-level signing with phase signing directly (not through
            // nested signers)
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({
                    msigner(
                        edgar,
                        msigner(
                            jinni,
                            msigner(phase),  // phase signs directly at level 3
                            msigner(shade))  // jinni quorum: 2+2 = 4 >= 3 ✓
                        )                    // edgar quorum: 1+0 = 1 < 2 ✗
                }),
                L(),
                fee(4 * baseFee),
                ter(tefBAD_QUORUM));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Test 2b: Edgar needs to meet his quorum too
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({
                    msigner(
                        edgar,
                        msigner(
                            jinni,
                            msigner(phase),  // phase signs directly
                            msigner(shade)),
                        msigner(bogie))  // edgar quorum: 1+1 = 2 ✓
                }),
                L(),
                fee(5 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Test 2c: Use phase's signers (making it effectively 3-level from
            // alice)
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(
                    edgar,
                    msigner(jinni, msigner(phase, msigner(acc10), msigner(acc11)), msigner(spook)),
                    msigner(bogie))}),
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 3: Mixed levels - some direct, some nested at different
        // depths (max 3)
        {
            // Set up mixed-level signing for alice
            // grace has direct signers
            env(signers(grace, 2, {{bogie, 1}, {demon, 1}}));

            // henry has 2-level signers (henry -> becky -> bogie/demon)
            env(signers(henry, 1, {{becky, 1}, {cheri, 1}}));

            // edgar can be signed for by bogie
            env(signers(edgar, 1, {{bogie, 1}, {shade, 1}}));

            // Alice has mix of direct and nested signers at different weights
            env(signers(
                alice,
                5,
                {
                    {daria, 1},  // direct signer
                    {edgar, 2},  // has 2-level signers
                    {fiona, 1},  // direct signer
                    {grace, 2},  // has direct signers
                    {henry, 2}   // has 2-level signers
                }));
            env.close();

            // Test 3a: Mix of all levels meeting quorum exactly
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({
                    msigner(daria),                                 // weight 1, direct
                    msigner(edgar, msigner(bogie)),                 // weight 2, 2-level
                    msigner(grace, msigner(bogie), msigner(demon))  // weight 2,
                                                                    // 2-level
                }),
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Test 3b: 3-level signing through henry
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig(
                    {msigner(fiona),                  // weight 1, direct
                     msigner(grace, msigner(bogie)),  // weight 2, 2-level (partial)
                     msigner(
                         henry,  // weight 2, 3-level
                         msigner(becky, msigner(bogie), msigner(demon)))}),
                L(),
                fee(6 * baseFee),
                ter(tefBAD_QUORUM));  // grace didn't meet quorum
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Test 3c: Correct version with all quorums met
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig(
                    {msigner(fiona),                                  // weight 1
                     msigner(edgar, msigner(bogie), msigner(shade)),  // weight 2
                     msigner(
                         henry,  // weight 2
                         msigner(becky, msigner(bogie), msigner(demon)))}),
                L(),
                fee(8 * baseFee));  // Total weight: 1+2+2 = 5 ✓
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 4: Complex scenario with maximum signers at mixed depths
        // (max 3)
        {
            // Create a signing tree that uses close to maximum signers
            // and tests weight accumulation across all levels

            // Set up for alice: needs 15 out of possible 20 weight
            env(signers(
                alice,
                15,
                {
                    {becky, 3},  // will use 2-level
                    {cheri, 3},  // will use 2-level
                    {daria, 3},  // will use direct
                    {edgar, 3},  // will use 2-level
                    {fiona, 3},  // will use direct
                    {grace, 3},  // will use direct
                    {henry, 2}   // will use 2-level
                }));
            env.close();

            // Complex multi-level transaction just meeting quorum
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({
                    msigner(
                        becky,  // weight 3, 2-level
                        msigner(demon),
                        msigner(ghost)),
                    msigner(
                        cheri,  // weight 3, 2-level
                        msigner(haunt),
                        msigner(jinni)),
                    msigner(daria),  // weight 3, direct
                    msigner(
                        edgar,  // weight 3, 2-level
                        msigner(bogie)),
                    msigner(grace)  // weight 3, direct
                }),
                L(),
                fee(10 * baseFee));  // Total weight: 3+3+3+3+3 = 15 ✓
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Test 4b: Test with henry using 3-level depth (maximum)
            // First set up henry's chain properly
            env(signers(henry, 1, {{jinni, 1}}));
            env(signers(jinni, 2, {{acc10, 1}, {acc11, 1}}));
            env.close();

            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig(
                    {msigner(
                         becky,            // weight 3
                         msigner(demon)),  // becky quorum not met!
                     msigner(
                         cheri,  // weight 3
                         msigner(haunt),
                         msigner(jinni)),
                     msigner(daria),  // weight 3
                     msigner(
                         henry,  // weight 2, 3-level depth
                         msigner(jinni, msigner(acc10), msigner(acc11))),
                     msigner(
                         edgar,  // weight 3
                         msigner(bogie),
                         msigner(shade))}),
                L(),
                fee(10 * baseFee),
                ter(tefBAD_QUORUM));  // becky's quorum not met
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test Case 5: Edge case - single signer with maximum nesting (depth 3)
        {
            // Alice needs just one signer, but that signer uses depth up to 3
            env(signers(alice, 1, {{becky, 1}}));
            env.close();

            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(demon), msigner(ghost))}), L(), fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Now with 3-level depth (maximum allowed)
            // Structure: alice -> becky -> cheri -> jinni (jinni signs
            // directly)
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 1, {{jinni, 1}}));
            // Note: We do NOT add signers to jinni to keep max depth at 3
            env.close();

            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(jinni)))}),  // jinni signs directly (depth 3)
                L(),
                fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 6: Simple cycle detection (A -> B -> A)
        {
            testcase("Cycle Detection - Simple");

            // Reset signer lists for clean state
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env.close();

            // becky's signer list includes alice
            // alice's signer list includes becky
            // This creates: alice -> becky -> alice (cycle)
            env(signers(alice, 1, {{becky, 1}, {bogie, 1}}));
            env(signers(becky, 1, {{alice, 1}, {demon, 1}}));
            env.close();

            // Without cycle relaxation this would fail because:
            // - alice needs becky (weight 1)
            // - becky needs alice, but alice is ancestor -> cycle
            // - becky's effective quorum relaxes since alice is unavailable
            // - demon can satisfy becky's relaxed quorum
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(demon))}), L(), fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Test that direct signer still works normally
            aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(bogie)}), L(), fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 6b: Unauthorized cyclic nested signer must be rejected.
        // Validates auth check happens before cycle skipping.
        {
            testcase("Cycle Detection - Unauthorized Cyclic Signer Rejected");

            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            // alice can be signed by becky.
            env(signers(alice, 1, {{becky, 1}}));
            // becky can be signed by cheri.
            env(signers(becky, 1, {{cheri, 1}}));
            // cheri can only be signed by demon. becky is NOT authorized here.
            env(signers(cheri, 1, {{demon, 1}}));
            env.close();

            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(becky), msigner(demon)))}),
                L(),
                fee(4 * baseFee),
                ter(tefBAD_SIGNATURE));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test Case 6c: Authorized cyclic nested signer is ignored and valid
        // non-cyclic path may still satisfy quorum.
        {
            testcase("Cycle Detection - Authorized Cyclic Signer Ignored");

            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            // becky is cyclic but authorized; demon is the usable signer.
            env(signers(cheri, 1, {{becky, 1}, {demon, 1}}));
            env.close();

            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(becky), msigner(demon)))}),
                L(),
                fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 7: The specific lockout scenario
        // onyx:{jade, nova:{ruby:{jade, nova}, jade}}
        // All have quorum 2, only jade can actually sign
        {
            testcase("Cycle Detection - Complex Lockout");

            Account const onyx{"onyx", KeyType::secp256k1};
            Account const nova{"nova", KeyType::ed25519};
            Account const ruby{"ruby", KeyType::secp256k1};
            Account const jade{"jade", KeyType::ed25519};  // phantom signer

            env.fund(XRP(1000), onyx, nova, ruby);
            env.close();

            // Set up signer lists FIRST (before disabling master keys)
            // ruby: {jade, nova} with quorum 2
            env(signers(ruby, 2, {{jade, 1}, {nova, 1}}));
            // nova: {ruby, jade} with quorum 2
            env(signers(nova, 2, {{jade, 1}, {ruby, 1}}));
            // onyx: {jade, nova} with quorum 2
            env(signers(onyx, 2, {{jade, 1}, {nova, 1}}));
            env.close();

            // NOW disable master keys (signer lists provide alternative)
            env(fset(onyx, asfDisableMaster), sig(onyx));
            env(fset(nova, asfDisableMaster), sig(nova));
            env(fset(ruby, asfDisableMaster), sig(ruby));
            env.close();

            // The signing tree for onyx:
            // onyx (quorum 2) -> jade (weight 1) + nova (weight 1)
            //   nova (quorum 2) -> jade (weight 1) + ruby (weight 1)
            //     ruby (quorum 2) -> jade (weight 1) + nova (weight 1, CYCLE!)
            //
            // Without cycle detection: ruby needs nova, but nova is ancestor ->
            // stuck With cycle detection:
            //   - At ruby level: nova is cyclic, cyclicWeight=1, totalWeight=2
            //   - maxAchievable = 2-1 = 1 < quorum(2), so effectiveQuorum -> 1
            //   - jade alone can satisfy ruby's relaxed quorum
            //   - ruby satisfied -> nova gets ruby's weight
            //   - nova: jade(1) + ruby(1) = 2 >= quorum(2) ✓
            //   - onyx: jade(1) + nova(1) = 2 >= quorum(2) ✓

            std::uint32_t onyxSeq = env.seq(onyx);
            env(noop(onyx),
                msig(
                    {msigner(jade),
                     msigner(nova, msigner(jade), msigner(ruby, msigner(jade)))}),  // nova is cyclic,
                                                                                    // skipped at ruby level
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(onyx) == onyxSeq + 1);
        }

        // Test Case 8: Cycle where all signers are cyclic (effectiveQuorum ==
        // 0)
        {
            testcase("Cycle Detection - Total Lockout");

            Account const alpha{"alpha", KeyType::secp256k1};
            Account const beta{"beta", KeyType::ed25519};
            Account const gamma{"gamma", KeyType::secp256k1};

            env.fund(XRP(1000), alpha, beta, gamma);
            env.close();

            // Set up pure cycle signer lists FIRST
            env(signers(alpha, 1, {{beta, 1}}));
            env(signers(beta, 1, {{gamma, 1}}));
            env(signers(gamma, 1, {{alpha, 1}}));
            env.close();

            // NOW disable master keys
            env(fset(alpha, asfDisableMaster), sig(alpha));
            env(fset(beta, asfDisableMaster), sig(beta));
            env(fset(gamma, asfDisableMaster), sig(gamma));
            env.close();

            // This is a true lockout - no valid signing path exists.
            // gamma appears as a leaf signer but has master disabled ->
            // tefMASTER_DISABLED (The cycle detection would return
            // tefBAD_QUORUM if gamma were nested, but there's no way to
            // construct such a transaction since gamma's only signer is alpha,
            // which is what we're trying to sign for)
            std::uint32_t alphaSeq = env.seq(alpha);
            env(noop(alpha),
                msig({msigner(beta, msigner(gamma))}),  // gamma can't sign - master disabled
                L(),
                fee(4 * baseFee),
                ter(tefMASTER_DISABLED));
            env.close();
            BEAST_EXPECT(env.seq(alpha) == alphaSeq);
        }

        // Test Case 9: Cycle at depth 3 (near max depth)
        {
            testcase("Cycle Detection - Deep Cycle");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env(signers(daria, jtx::none));
            env.close();

            // Structure: alice -> becky -> cheri -> daria -> alice (cycle at
            // depth 4)
            env(signers(alice, 1, {{becky, 1}, {bogie, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env(signers(daria, 1, {{alice, 1}, {demon, 1}}));
            env.close();

            // At depth 4, daria needs alice but alice is ancestor
            // daria's quorum relaxes, demon can satisfy
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(daria, msigner(demon))))}),
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 10: Multiple independent cycles in same tree
        {
            testcase("Cycle Detection - Multiple Cycles");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            // alice -> {becky, cheri}
            // becky -> {alice, bogie}  (cycle back to alice)
            // cheri -> {alice, demon}  (another cycle back to alice)
            env(signers(alice, 2, {{becky, 1}, {cheri, 1}}));
            env(signers(becky, 2, {{alice, 1}, {bogie, 1}}));
            env(signers(cheri, 2, {{alice, 1}, {demon, 1}}));
            env.close();

            // Both becky and cheri have cycles back to alice
            // Both need their quorums relaxed
            // bogie satisfies becky, demon satisfies cheri
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(bogie)), msigner(cheri, msigner(demon))}),
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 11: Cycle with sufficient non-cyclic weight (no relaxation
        // needed)
        {
            testcase("Cycle Detection - No Relaxation Needed");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env.close();

            // becky has alice in signer list but also has enough other signers
            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 2, {{alice, 1}, {bogie, 1}, {demon, 1}}));
            env.close();

            // becky quorum is 2, alice is cyclic (weight 1)
            // totalWeight = 3, cyclicWeight = 1, maxAchievable = 2 >= quorum
            // No relaxation needed, bogie + demon satisfy quorum normally
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(bogie), msigner(demon))}), L(), fee(5 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Should fail if only one non-cyclic signer provided
            aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(bogie))}), L(), fee(4 * baseFee), ter(tefBAD_QUORUM));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test Case 12: Partial cycle - one branch cyclic, one not
        {
            testcase("Cycle Detection - Partial Cycle");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            // alice -> {becky, cheri}
            // becky -> {alice, bogie}  (cyclic)
            // cheri -> {daria}  (not cyclic)
            env(signers(alice, 2, {{becky, 1}, {cheri, 1}}));
            env(signers(becky, 1, {{alice, 1}, {bogie, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env.close();

            // becky's branch has cycle, cheri's doesn't
            // Both contribute to alice's quorum
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig(
                    {msigner(becky, msigner(bogie)),    // relaxed quorum
                     msigner(cheri, msigner(daria))}),  // normal quorum
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 13: Diamond pattern with cycle
        {
            testcase("Cycle Detection - Diamond Pattern");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env(signers(daria, jtx::none));
            env.close();

            // alice -> {becky, cheri}
            // becky -> {daria}
            // cheri -> {daria}
            // daria -> {alice, bogie}  (cycle through both paths)
            env(signers(alice, 2, {{becky, 1}, {cheri, 1}}));
            env(signers(becky, 1, {{daria, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env(signers(daria, 1, {{alice, 1}, {bogie, 1}}));
            env.close();

            // Both paths converge at daria, which cycles back to alice
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(daria, msigner(bogie))), msigner(cheri, msigner(daria, msigner(bogie)))}),
                L(),
                fee(7 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        // Test Case 14: Cycle requiring maximum quorum relaxation
        {
            testcase("Cycle Detection - Maximum Relaxation");

            Account const omega{"omega", KeyType::secp256k1};
            Account const sigma{"sigma", KeyType::ed25519};

            env.fund(XRP(1000), omega, sigma);
            env.close();

            // Reset alice and becky signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env.close();

            // Set up signer lists FIRST
            env(signers(sigma, 1, {{omega, 1}, {bogie, 1}}));
            env(signers(omega, 3, {{sigma, 2}, {alice, 1}, {becky, 1}}));
            env(signers(alice, 1, {{omega, 1}, {demon, 1}}));
            env(signers(becky, 1, {{omega, 1}, {ghost, 1}}));
            env.close();

            // NOW disable master keys
            env(fset(omega, asfDisableMaster), sig(omega));
            env(fset(sigma, asfDisableMaster), sig(sigma));
            env.close();

            // From omega's perspective when signing for omega:
            // - sigma: needs omega (cyclic), so relaxes to bogie only
            // - alice: needs omega (cyclic), so relaxes to demon only
            // - becky: needs omega (cyclic), so relaxes to ghost only
            // All signers need relaxation but can be satisfied
            std::uint32_t omegaSeq = env.seq(omega);
            env(noop(omega),
                msig({msigner(alice, msigner(demon)), msigner(becky, msigner(ghost)), msigner(sigma, msigner(bogie))}),
                L(),
                fee(7 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(omega) == omegaSeq + 1);
        }

        // Test Case 15: Cycle at exact max depth boundary
        {
            testcase("Cycle Detection - Max Depth Boundary");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env(signers(daria, jtx::none));
            env(signers(edgar, jtx::none));
            env.close();

            // Depth 4 is max: alice(1) -> becky(2) -> cheri(3) -> daria(4)
            // daria cycles back but we're at max depth
            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env(signers(daria, 1, {{alice, 1}, {bogie, 1}}));
            env.close();

            // This should work - cycle detected and relaxed at depth 4
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(daria, msigner(bogie))))}),
                L(),
                fee(6 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Now try to exceed depth (add edgar at depth 5)
            env(signers(daria, 1, {{edgar, 1}}));
            env(signers(edgar, 1, {{bogie, 1}}));
            env.close();

            // Transaction structure is rejected at preflight for exceeding
            // nesting limits
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(daria, msigner(edgar, msigner(bogie)))))}),
                L(),
                fee(7 * baseFee),
                ter(temMALFORMED));  // Rejected at preflight for excessive
                                     // nesting
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test Case 16: Fee calculation with nested signers
        // Verify that fees are based on total LEAF signers, not nested entries
        // Note: Not using L() macro here to get clean fee calculations
        {
            testcase("Nested Fee Calculation");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            // Setup: alice -> becky -> {bogie, demon}
            // This means 2 leaf signers even though there's 1 nested entry
            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 2, {{bogie, 1}, {demon, 1}}));
            env.close();

            // Fee = baseFee + (2 leaf signers * baseFee) = 3 * baseFee
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(bogie), msigner(demon))}), fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Fee too low: 2 * baseFee is not enough for 2 leaf signers
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(bogie), msigner(demon))}),
                fee((3 * baseFee) - 1),
                ter(telINSUF_FEE_P));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Deeper nesting: alice -> becky -> cheri -> {bogie, demon, ghost}
            // 3 leaf signers
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 3, {{bogie, 1}, {demon, 1}, {ghost, 1}}));
            env.close();

            // Fee = baseFee + (3 leaf signers * baseFee) = 4 * baseFee
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(bogie), msigner(demon), msigner(ghost)))}),
                fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Fee too low: 3 * baseFee not enough for 3 leaf signers
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(bogie), msigner(demon), msigner(ghost)))}),
                fee((4 * baseFee) - 1),
                ter(telINSUF_FEE_P));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test Case 17: Mixed flat and nested signers fee calculation
        {
            testcase("Mixed Flat and Nested Fee Calculation");

            // Reset signer lists
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env.close();

            // alice -> {becky (nested -> bogie, demon), daria (flat)}
            // Total leaf signers: 3 (bogie, demon, daria)
            env(signers(alice, 2, {{becky, 1}, {daria, 1}}));
            env(signers(becky, 2, {{bogie, 1}, {demon, 1}}));
            env.close();

            // Fee = baseFee + (3 leaf signers * baseFee) = 4 * baseFee
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(bogie), msigner(demon)), msigner(daria)}), fee(4 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Fee too low
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(bogie), msigner(demon)), msigner(daria)}),
                fee((4 * baseFee) - 1),
                ter(telINSUF_FEE_P));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }
    }

    void
    testNestedMultiSignEdgeCases(FeatureBitset features)
    {
        using namespace jtx;

        if (!features[featureNestedMultiSign])
            return;

        // Three validation layers for nested multi-sign, in order:
        //
        // 1. STTx::checkSign → multiSignHelper (local signature &
        //    structure checks: depth, sort order, leaf/nested shape,
        //    cryptographic validity).
        //    Tests: 1, 2, 3, 5, 6, 8, 11, 12.
        //
        // 2. Transactor::checkMultiSign → validateSigners (ledger-
        //    level checks: signer list lookup, phantom/regular key,
        //    cycle detection, quorum).
        //    Tests: 4 (fee/countSigners), 7, 9.
        //
        // 3. transactionSubmitMultiSigned → validateSignersRecursive
        //    (RPC-level JSON shape validation, depth limit).
        //    Test: 10.
        //
        // Many Transactor paths are unreachable because multiSignHelper
        // catches the same structural errors first.

        Env env{*this, envconfig(), features};

        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        Account const daria{"daria", KeyType::ed25519};
        Account const edgar{"edgar", KeyType::secp256k1};
        Account const fiona{"fiona", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri, daria, edgar, fiona);
        env.close();

        auto const baseFee = env.current()->fees().base;

        // lambda that submits an STTx and returns the resulting JSON.
        auto submitSTTx = [&env](STTx const& stx) {
            Json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(stx.getSerializer().slice());
            return env.rpc("json", "submit", to_string(jvResult));
        };

        // Test 1: Depth overflow in multiSignHelper's checkSignersArray.
        // Build nested signer tree at depth 5 via the env() path.
        // The depth-exceeded check triggers temMALFORMED at preflight.
        {
            testcase("Nested Depth Overflow in STTx");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env(signers(daria, 1, {{edgar, 1}}));
            env(signers(edgar, 1, {{bogie, 1}}));
            env.close();

            // Depth 5: alice(1)->becky(2)->cheri(3)->daria(4)->edgar(5)->bogie
            // This exceeds max depth of 4.  Also verify via local submit path.
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(daria, msigner(edgar, msigner(bogie)))))}),
                fee(3 * baseFee),
                ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            // Also exercise multiSignHelper's depth check
            // directly via checkSign.
            //
            // env.jt() can't build depth-5 directly (returns null
            // stx), so start from a valid depth-2 STTx and manually
            // add nesting levels to create depth 5.
            //
            // Structure after modification:
            //   outerSigners (depth 1): [{becky: nested}]
            //     beckySigners (depth 2): [{cheri: nested *}]
            //       cheriChildren (depth 3): [{daria: nested}]
            //         dariaChildren (depth 4): [{edgar: nested}]
            //           edgarChildren (depth 5): -> EXCEEDS MAX
            //
            // * cheri converted from leaf to nested via
            //   makeFieldAbsent(SPK/TxnSig) + setFieldArray(Signers)
            //
            // multiSignHelper enters checkSignersArray at depth 1
            // and recurses: 1->2->3->4->5. At depth 5 the check
            // `depth > maxDepth(4)` fires, returning
            // "Multi-signing depth limit exceeded."
            {
                JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(cheri))}));
                BEAST_EXPECT(tx.stx);
                if (tx.stx)
                {
                    STTx local = *(tx.stx);
                    auto& outerSigners = local.peekFieldArray(sfSigners);
                    auto& beckySigners = outerSigners.back().peekFieldArray(sfSigners);

                    // Convert cheri from leaf to nested:
                    //   remove SPK + TxnSig, add Signers array.
                    auto& cheriSigner = beckySigners.back();
                    cheriSigner.makeFieldAbsent(sfSigningPubKey);
                    cheriSigner.makeFieldAbsent(sfTxnSignature);

                    // Verify cheri is now a nested signer.
                    BEAST_EXPECT(!isLeafSigner(cheriSigner));

                    // Build 3 nested levels: daria->edgar->fiona
                    STObject fionaSigner(sfSigner);
                    fionaSigner.setAccountID(sfAccount, fiona.id());
                    fionaSigner.setFieldVL(sfSigningPubKey, Blob(33, 0x02));
                    fionaSigner.setFieldVL(sfTxnSignature, Blob(64, 0xAA));
                    fionaSigner.applyTemplateFromSField(sfSigner);
                    BEAST_EXPECT(isLeafSigner(fionaSigner));

                    STObject edgarSigner(sfSigner);
                    edgarSigner.setAccountID(sfAccount, edgar.id());
                    STArray edgarChildren;
                    edgarChildren.push_back(fionaSigner);
                    edgarSigner.setFieldArray(sfSigners, edgarChildren);
                    edgarSigner.applyTemplateFromSField(sfSigner);
                    BEAST_EXPECT(isNestedSigner(edgarSigner));

                    STObject dariaSigner(sfSigner);
                    dariaSigner.setAccountID(sfAccount, daria.id());
                    STArray dariaChildren;
                    dariaChildren.push_back(edgarSigner);
                    dariaSigner.setFieldArray(sfSigners, dariaChildren);
                    dariaSigner.applyTemplateFromSField(sfSigner);
                    BEAST_EXPECT(isNestedSigner(dariaSigner));

                    // Attach daria under cheri.
                    STArray cheriChildren;
                    cheriChildren.push_back(dariaSigner);
                    cheriSigner.setFieldArray(sfSigners, cheriChildren);
                    BEAST_EXPECT(isNestedSigner(cheriSigner));

                    // Call checkSign directly to verify the depth
                    // error without any serialization concerns.
                    auto const rules = env.current()->rules();
                    auto const result = local.checkSign(rules);
                    BEAST_EXPECT(!result);
                    BEAST_EXPECT(result.error().find("depth limit exceeded") != std::string::npos);

                    // Note: blob submit is not tested here. The
                    // manually-built structure fails somewhere in
                    // the serialize/deserialize roundtrip (possibly
                    // template reapplication during STTx
                    // construction from SerialIter). The direct
                    // checkSign call above already covers the
                    // depth-exceeded path in checkSignersArray.
                }
            }
        }

        // Test 2: Invalid signature via corrupted SigningPubKey in
        // multiSignHelper's leaf verification (the catch/validSig path).
        // Corrupt a nested signer's SigningPubKey to malformed size/content.
        {
            testcase("Nested Corrupt SigningPubKey Exception");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(bogie))}));
            BEAST_EXPECT(tx.stx);
            if (tx.stx)
            {
                STTx local = *(tx.stx);

                // Corrupt the nested signer's SigningPubKey to an invalid
                // size that fails the publicKeyType check and validSig
                // stays false.
                auto& outerSigners = local.peekFieldArray(sfSigners);
                auto& nestedSigners = outerSigners.back().peekFieldArray(sfSigners);
                // Set to 1 byte - too short for any valid key type.
                nestedSigners.back().setFieldVL(sfSigningPubKey, Blob(1, 0xFF));
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception].asString().find("Invalid signature on account r") !=
                    std::string::npos);
            }
        }

        // Test 3: Malformed signer entry shape in multiSignHelper.
        // Construct nested signer entry with both Signers and
        // TxnSignature/SigningPubKey — fails isLeafSigner and
        // isNestedSigner, hitting the "Malformed signer entry" path.
        {
            testcase("Nested Malformed Signer Entry Shape");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(bogie))}));
            BEAST_EXPECT(tx.stx);
            if (tx.stx)
            {
                STTx local = *(tx.stx);

                // Add both signature fields AND Signers array to the
                // nested signer, making it neither leaf nor nested.
                auto& outerSigners = local.peekFieldArray(sfSigners);
                auto& nestedSigners = outerSigners.back().peekFieldArray(sfSigners);
                auto& leafSigner = nestedSigners.back();
                // It's currently a leaf signer. Add a Signers array too.
                leafSigner.setFieldArray(sfSigners, STArray{});
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception].asString().find("Malformed signer entry for account r") !=
                    std::string::npos);
            }
        }

        // Test 4: Fee calculation via calculateBaseFee / countSigners.
        // Verify that fee is correct for a max-depth (4) nested tx,
        // and that an insufficient fee is rejected.
        {
            testcase("Nested Fee Depth Guard");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            env(signers(cheri, 1, {{daria, 1}}));
            env(signers(daria, 1, {{bogie, 1}}));
            env.close();

            // Depth 4 (max): alice->becky->cheri->daria->bogie (1 leaf)
            // Fee should be baseFee + 1*baseFee = 2*baseFee
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(cheri, msigner(daria, msigner(bogie))))}), fee(2 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

            // Fee too low should fail
            aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(daria, msigner(bogie))))}),
                fee((2 * baseFee) - 1),
                ter(telINSUF_FEE_P));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test 5: Unsorted nested signers.
        // Reversing the nested signer order triggers the sort check
        // in multiSignHelper's checkSignersArray.
        {
            testcase("Nested Unsorted Signers in STTx");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}, {demon, 1}}));
            env.close();

            // Build a valid nested tx, then unsort the nested signers
            JTx tx = env.jt(noop(alice), fee(4 * baseFee), msig({msigner(becky, msigner(bogie), msigner(demon))}));
            BEAST_EXPECT(tx.stx);
            if (tx.stx)
            {
                STTx local = *(tx.stx);

                // Reverse the nested signers to make them unsorted
                auto& outerSigners = local.peekFieldArray(sfSigners);
                auto& nestedSigners = outerSigners.back().peekFieldArray(sfSigners);
                std::reverse(nestedSigners.begin(), nestedSigners.end());

                // The local STTx check (multiSignHelper) catches unsorted
                // arrays, so we get a local-check failure.
                auto const info = submitSTTx(local);
                BEAST_EXPECT(info[jss::result][jss::error_exception] == "fails local checks: Unsorted Signers array.");
            }
        }

        // Test 6: Unknown pubkey type in nested leaf.
        // Set nested signer SigningPubKey to invalid type prefix;
        // multiSignHelper's leaf verification rejects it.
        {
            testcase("Nested Unknown PubKey Type");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(bogie))}));
            BEAST_EXPECT(tx.stx);
            if (tx.stx)
            {
                STTx local = *(tx.stx);

                // Replace the nested signer's SigningPubKey with a
                // valid-length but unknown type prefix (0x05 is not
                // secp256k1 or ed25519)
                auto& outerSigners = local.peekFieldArray(sfSigners);
                auto& nestedSigners = outerSigners.back().peekFieldArray(sfSigners);
                Blob invalidPK(33, 0x00);
                invalidPK[0] = 0x05;  // invalid type prefix
                nestedSigners.back().setFieldVL(sfSigningPubKey, invalidPK);

                // The local STTx check catches this first (invalid
                // signature).
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception].asString().find("Invalid signature on account r") !=
                    std::string::npos);
            }

            // For the Transactor path, we need the tx to pass local checks
            // but fail at Transactor level. A signer account with
            // a mismatched pubkey will hit this via the regular key path.
            // This is covered by test 7 below.
        }

        // Test 7: Non-phantom signer without account root.
        // Use signer in nested tree that doesn't exist in ledger but
        // whose pubkey hashes to a different account (non-phantom
        // path in Transactor::checkMultiSign's validateSigners).
        {
            testcase("Nested Non-Phantom Without Account Root");

            // bogie is a phantom (no account root). Use a Reg with a
            // different signing key to make pubkey hash mismatch.
            // This makes signingAcctIDFromPubKey != txSignerAcctID (bogie),
            // so it's not phantom. Then sleTxSignerRoot is null ->
            // tefBAD_SIGNATURE.
            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            // Sign with demon's key for bogie's account.
            // msigner(bogie, demon) creates leaf Reg(acct=bogie, sig=demon).
            // STTx check passes (signature is cryptographically valid).
            // Transactor: calcAccountID(demon.pk()) != bogie → non-phantom
            // path, but bogie has no account root → tefBAD_SIGNATURE.
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice), msig({msigner(becky, msigner(bogie, demon))}), fee(3 * baseFee), ter(tefBAD_SIGNATURE));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test 8: Malformed signer entry in multiSignHelper.
        // Craft signer entry with Account + SigningPubKey but no
        // TxnSignature and no Signers — fails both isLeafSigner
        // (needs TxnSignature) and isNestedSigner (needs Signers),
        // hitting the "Malformed signer entry" path.
        {
            testcase("Nested Malformed Signer Missing TxnSig");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            JTx tx = env.jt(noop(alice), fee(3 * baseFee), msig({msigner(becky, msigner(bogie))}));
            BEAST_EXPECT(tx.stx);
            if (tx.stx)
            {
                STTx local = *(tx.stx);

                // Remove TxnSignature from the nested leaf signer,
                // leaving Account + SigningPubKey (2 present fields).
                // This is neither a leaf (needs 3: Account+SPK+TxnSig)
                // nor nested (needs Account+Signers).
                auto& outerSigners = local.peekFieldArray(sfSigners);
                auto& nestedSigners = outerSigners.back().peekFieldArray(sfSigners);
                auto& leafSigner = nestedSigners.back();
                leafSigner.makeFieldAbsent(sfTxnSignature);
                auto const info = submitSTTx(local);
                BEAST_EXPECT(
                    info[jss::result][jss::error_exception].asString().find("Malformed signer entry for account r") !=
                    std::string::npos);
            }
        }

        // Test 9: effectiveQuorum == 0 path in
        // Transactor::checkMultiSign's validateSigners.
        // Build scenario where ALL signers at a level are cyclic
        // with no non-cyclic alternative.
        {
            testcase("Nested effectiveQuorum Zero");

            // Structure: alice -> becky -> cheri -> {becky}
            // cheri's ONLY signer is becky, who is already an ancestor
            // in the signing chain. At cheri's level:
            //   ancestors = {alice, becky, cheri}
            //   cheri's signer list = {becky: weight 1}
            //   becky is in ancestors → cyclicWeight = 1
            //   totalWeight = 1, maxAchievable = 0 < quorum(1)
            //   effectiveQuorum = 0 → tefBAD_QUORUM
            //
            // Note: we can't use alice as the cyclic signer because
            // STTx rejects txnAccountID as a multisigner at any depth.
            env(signers(alice, jtx::none));
            env(signers(becky, jtx::none));
            env(signers(cheri, jtx::none));
            env.close();

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{cheri, 1}}));
            // cheri's only signer is becky (which will be cyclic)
            env(signers(cheri, 1, {{becky, 1}}));
            env.close();

            // Transaction: alice -> becky(nested) -> cheri(nested) ->
            // becky(leaf)
            // STTx check passes (becky != alice, signature valid)
            // Transactor: at cheri's level, becky is ancestor → skipped
            // All signers cyclic → effectiveQuorum = 0 → tefBAD_QUORUM
            std::uint32_t aliceSeq = env.seq(alice);
            env(noop(alice),
                msig({msigner(becky, msigner(cheri, msigner(becky)))}),
                fee(3 * baseFee),
                ter(tefBAD_QUORUM));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Test 10: RPC recursive validator negatives.
        // Exercise validateSignersRecursive in
        // transactionSubmitMultiSigned.
        {
            testcase("Nested RPC Validator Negatives");

            env(signers(alice, 1, {{becky, 1}}));
            env(signers(becky, 1, {{bogie, 1}}));
            env.close();

            // 10a: isValidSignerEntry rejects a signer with both
            // leaf fields (SPK/TxnSig) AND a nested Signers array.
            {
                Json::Value jv;
                jv[jss::tx_json][jss::Account] = alice.human();
                jv[jss::tx_json][jss::TransactionType] = jss::AccountSet;
                jv[jss::tx_json][jss::Fee] = (3 * baseFee).jsonClipped();
                jv[jss::tx_json][jss::Sequence] = env.seq(alice);
                jv[jss::tx_json][jss::SigningPubKey] = "";

                // Build a signer with both SPK/TxnSig AND Signers
                auto& signersList = jv[jss::tx_json][sfSigners.getJsonName()];
                auto& signer0 = signersList[0u][sfSigner.getJsonName()];
                signer0[jss::Account] = becky.human();
                signer0[jss::SigningPubKey] = strHex(becky.pk().slice());
                signer0[sfTxnSignature.getJsonName()] = "DEADBEEF";
                // Also add a nested Signers array
                auto& nestedSigners = signer0[sfSigners.getJsonName()];
                auto& nested0 = nestedSigners[0u][sfSigner.getJsonName()];
                nested0[jss::Account] = bogie.human();
                nested0[jss::SigningPubKey] = strHex(bogie.pk().slice());
                nested0[sfTxnSignature.getJsonName()] = "DEADBEEF";

                auto jrr = env.rpc("json", "submit_multisigned", to_string(jv));
                BEAST_EXPECT(jrr[jss::result][jss::status] == "error");
                BEAST_EXPECT(
                    jrr[jss::result][jss::error_message] == "Signers array may only contain valid Signer entries.");
            }

            // 10b: depth check in validateSignersRecursive rejects
            // a depth-5 nested tree (exceeds nestedMultiSignMaxDepth).
            {
                Json::Value jv;
                jv[jss::tx_json][jss::Account] = alice.human();
                jv[jss::tx_json][jss::TransactionType] = jss::AccountSet;
                jv[jss::tx_json][jss::Fee] = (3 * baseFee).jsonClipped();
                jv[jss::tx_json][jss::Sequence] = env.seq(alice);
                jv[jss::tx_json][jss::SigningPubKey] = "";

                // Build depth-5 nested structure
                // Level 1: becky (nested)
                auto& signersList = jv[jss::tx_json][sfSigners.getJsonName()];
                auto* current = &signersList[0u][sfSigner.getJsonName()];
                (*current)[jss::Account] = becky.human();

                // Levels 2-4: cheri, daria, edgar (all nested)
                Account const accts[] = {cheri, daria, edgar};
                for (auto const& acct : accts)
                {
                    auto& nested = (*current)[sfSigners.getJsonName()];
                    current = &nested[0u][sfSigner.getJsonName()];
                    (*current)[jss::Account] = acct.human();
                }

                // Level 5: fiona (nested - exceeds max depth 4)
                {
                    auto& nested = (*current)[sfSigners.getJsonName()];
                    current = &nested[0u][sfSigner.getJsonName()];
                    (*current)[jss::Account] = fiona.human();

                    // Level 6: bogie as leaf at bottom
                    auto& deepNested = (*current)[sfSigners.getJsonName()];
                    auto& leaf = deepNested[0u][sfSigner.getJsonName()];
                    leaf[jss::Account] = bogie.human();
                    leaf[jss::SigningPubKey] = strHex(bogie.pk().slice());
                    leaf[sfTxnSignature.getJsonName()] = "DEADBEEF";
                }

                auto jrr = env.rpc("json", "submit_multisigned", to_string(jv));
                BEAST_EXPECT(jrr[jss::result][jss::status] == "error");
                BEAST_EXPECT(
                    jrr[jss::result][jss::error_message] == "Signers array may only contain valid Signer entries.");
            }

            // 10c: isValidSignerEntry rejects a nested child with
            // Account + TxnSignature but no SPK and no Signers.
            {
                Json::Value jv;
                jv[jss::tx_json][jss::Account] = alice.human();
                jv[jss::tx_json][jss::TransactionType] = jss::AccountSet;
                jv[jss::tx_json][jss::Fee] = (3 * baseFee).jsonClipped();
                jv[jss::tx_json][jss::Sequence] = env.seq(alice);
                jv[jss::tx_json][jss::SigningPubKey] = "";

                // Valid outer nested signer
                auto& signersList = jv[jss::tx_json][sfSigners.getJsonName()];
                auto& signer0 = signersList[0u][sfSigner.getJsonName()];
                signer0[jss::Account] = becky.human();

                // Invalid nested child: has Account + TxnSignature but
                // no SigningPubKey (neither leaf nor nested)
                auto& nestedSigners = signer0[sfSigners.getJsonName()];
                auto& nested0 = nestedSigners[0u][sfSigner.getJsonName()];
                nested0[jss::Account] = bogie.human();
                nested0[sfTxnSignature.getJsonName()] = "DEADBEEF";
                // Intentionally omit SigningPubKey and Signers

                auto jrr = env.rpc("json", "submit_multisigned", to_string(jv));
                BEAST_EXPECT(jrr[jss::result][jss::status] == "error");
                BEAST_EXPECT(
                    jrr[jss::result][jss::error_message] == "Signers array may only contain valid Signer entries.");
            }
        }

        // Tests 11–12: Leaf cap enforcement
        // (nestedMultiSignMaxLeafSigners = 64).
        // Both tests share 65 funded leaf accounts.
        {
            std::vector<Account> leaves;
            leaves.reserve(65);
            for (int i = 0; i < 65; ++i)
                leaves.emplace_back("leaf" + std::to_string(i), KeyType::secp256k1);
            for (auto const& leaf : leaves)
                env.fund(XRP(1000), leaf);
            env.close();

            // Test 11: Leaf cap exceeded (65 > 64).
            // 3-way nested tree: becky(22) + cheri(22) + daria(21)
            // = 65 leaf signers.  multiSignHelper's leaf counter
            // rejects with "Too many leaf signers." at preflight.
            {
                testcase("Nested Leaf Cap Exceeded");

                // alice -> becky, cheri, daria (quorum 3)
                env(signers(alice, 3, {{becky, 1}, {cheri, 1}, {daria, 1}}));

                // becky -> leaves 0..21 (22 signers)
                {
                    std::vector<signer> list;
                    for (int i = 0; i < 22; ++i)
                        list.emplace_back(leaves[i], 1);
                    env(signers(becky, 1, list));
                }

                // cheri -> leaves 22..43 (22 signers)
                {
                    std::vector<signer> list;
                    for (int i = 22; i < 44; ++i)
                        list.emplace_back(leaves[i], 1);
                    env(signers(cheri, 1, list));
                }

                // daria -> leaves 44..64 (21 signers)
                {
                    std::vector<signer> list;
                    for (int i = 44; i < 65; ++i)
                        list.emplace_back(leaves[i], 1);
                    env(signers(daria, 1, list));
                }
                env.close();

                // Build nested msig: 22 + 22 + 21 = 65 leaf signers
                std::vector<std::shared_ptr<Reg>> beckyChildren;
                for (int i = 0; i < 22; ++i)
                    beckyChildren.push_back(msigner(leaves[i]));
                auto beckyReg = std::make_shared<Reg>(becky, std::move(beckyChildren));

                std::vector<std::shared_ptr<Reg>> cheriChildren;
                for (int i = 22; i < 44; ++i)
                    cheriChildren.push_back(msigner(leaves[i]));
                auto cheriReg = std::make_shared<Reg>(cheri, std::move(cheriChildren));

                std::vector<std::shared_ptr<Reg>> dariaChildren;
                for (int i = 44; i < 65; ++i)
                    dariaChildren.push_back(msigner(leaves[i]));
                auto dariaReg = std::make_shared<Reg>(daria, std::move(dariaChildren));

                // Fee = baseFee * (1 + 65) = 66 * baseFee
                std::uint32_t aliceSeq = env.seq(alice);
                env(noop(alice), msig({beckyReg, cheriReg, dariaReg}), fee(66 * baseFee), ter(temMALFORMED));
                env.close();
                BEAST_EXPECT(env.seq(alice) == aliceSeq);
            }

            // Test 12: At leaf cap succeeds (exactly 64).
            // 2-way nested tree: becky(32) + cheri(32) = 64.
            // Each nested signer is within the per-array cap of 32.
            {
                testcase("Nested Leaf Cap At Limit Succeeds");

                // Reconfigure: alice -> becky, cheri (quorum 2)
                env(signers(alice, 2, {{becky, 1}, {cheri, 1}}));

                // becky -> leaves 0..31 (32 signers)
                {
                    std::vector<signer> list;
                    for (int i = 0; i < 32; ++i)
                        list.emplace_back(leaves[i], 1);
                    env(signers(becky, 1, list));
                }

                // cheri -> leaves 32..63 (32 signers)
                {
                    std::vector<signer> list;
                    for (int i = 32; i < 64; ++i)
                        list.emplace_back(leaves[i], 1);
                    env(signers(cheri, 1, list));
                }
                env.close();

                // Build nested msig: 32 + 32 = 64 leaf signers
                std::vector<std::shared_ptr<Reg>> beckyLeaves;
                for (int i = 0; i < 32; ++i)
                    beckyLeaves.push_back(msigner(leaves[i]));
                auto beckyReg2 = std::make_shared<Reg>(becky, std::move(beckyLeaves));

                std::vector<std::shared_ptr<Reg>> cheriLeaves;
                for (int i = 32; i < 64; ++i)
                    cheriLeaves.push_back(msigner(leaves[i]));
                auto cheriReg2 = std::make_shared<Reg>(cheri, std::move(cheriLeaves));

                // Fee = baseFee * (1 + 64) = 65 * baseFee
                env(noop(alice), msig({beckyReg2, cheriReg2}), fee(65 * baseFee), ter(tesSUCCESS));
                env.close();
            }
        }
    }

    void
    test_countPresentFields()
    {
        testcase("countPresentFields vs getCount");

        // Construct Signer STObjects with template applied.
        // The sfSigner template has 4 fields:
        //   Account (required), SigningPubKey (opt), TxnSignature (opt),
        //   Signers (opt)
        // After template application, getCount() returns 4 (all template
        // slots), but countPresentFields() should return only the populated
        // ones.
        //
        // Note: Account must be set before applyTemplateFromSField because
        // the template marks it as required.

        // Leaf signer: set Account + SigningPubKey + TxnSignature
        {
            STObject signer(sfSigner);
            signer.setAccountID(sfAccount, bogie.id());
            signer.setFieldVL(sfSigningPubKey, Blob(33, 0x02));
            signer.setFieldVL(sfTxnSignature, Blob(64, 0xAA));
            signer.applyTemplateFromSField(sfSigner);

            // getCount() includes all template slots (should be 4)
            BEAST_EXPECT(signer.getCount() != 3);
            BEAST_EXPECT(signer.getCount() == 4);

            // countPresentFields() counts only populated fields (should be 3)
            BEAST_EXPECT(countPresentFields(signer) == 3);

            // Helpers should recognize this as a valid leaf signer
            BEAST_EXPECT(isLeafSigner(signer));
            BEAST_EXPECT(!isNestedSigner(signer));
            BEAST_EXPECT(isValidSignerEntry(signer));
        }

        // Nested signer: set Account + Signers
        {
            STObject signer(sfSigner);
            signer.setAccountID(sfAccount, demon.id());
            signer.setFieldArray(sfSigners, STArray{});
            signer.applyTemplateFromSField(sfSigner);

            BEAST_EXPECT(signer.getCount() != 2);
            BEAST_EXPECT(signer.getCount() == 4);

            BEAST_EXPECT(countPresentFields(signer) == 2);

            BEAST_EXPECT(!isLeafSigner(signer));
            BEAST_EXPECT(isNestedSigner(signer));
            BEAST_EXPECT(isValidSignerEntry(signer));
        }

        // Invalid: all 4 fields set (both leaf and nested fields)
        {
            STObject signer(sfSigner);
            signer.setAccountID(sfAccount, ghost.id());
            signer.setFieldVL(sfSigningPubKey, Blob(33, 0x02));
            signer.setFieldVL(sfTxnSignature, Blob(64, 0xAA));
            signer.setFieldArray(sfSigners, STArray{});
            signer.applyTemplateFromSField(sfSigner);

            BEAST_EXPECT(countPresentFields(signer) == 4);

            // Both helpers reject (mutually exclusive field sets)
            BEAST_EXPECT(!isLeafSigner(signer));
            BEAST_EXPECT(!isNestedSigner(signer));
            BEAST_EXPECT(!isValidSignerEntry(signer));
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
        testNestedMultiSign(features);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const all = testable_amendments();

        testAll(all - featureNestedMultiSign);
        testAll(all);

        testNestedMultiSignEdgeCases(all - featureNestedMultiSign);
        testNestedMultiSignEdgeCases(all);

        testSignerListSetFlags(all - fixInvalidTxFlags);
        testSignerListSetFlags(all);

        test_countPresentFields();

        testSignerListObject(all - fixIncludeKeyletFields);
        testSignerListObject(all);
    }
};

BEAST_DEFINE_TESTSUITE(MultiSign, app, xrpl);

}  // namespace test
}  // namespace xrpl
