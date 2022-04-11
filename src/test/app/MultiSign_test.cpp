//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
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

#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {
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
    test_noReserve(FeatureBitset features)
    {
        testcase("No Reserve");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};

        // The reserve required for a signer list changes with the passage
        // of featureMultiSignReserve.  Make the required adjustments.
        bool const reserve1{features[featureMultiSignReserve]};

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        auto const fee = env.current()->fees().base;
        auto const smallSignersReserve = reserve1 ? XRP(250) : XRP(350);
        env.fund(smallSignersReserve - drops(1), alice);
        env.close();
        env.require(owners(alice, 0));

        {
            // Attach a signer list to alice.  Should fail.
            Json::Value smallSigners = signers(alice, 1, {{bogie, 1}});
            env(smallSigners, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(owners(alice, 0));

            // Fund alice enough to set the signer list, then attach signers.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(smallSigners);
            env.close();
            env.require(owners(alice, reserve1 ? 1 : 3));
        }
        {
            // Pay alice enough to almost make the reserve for the biggest
            // possible list.
            auto const addReserveBigSigners = reserve1 ? XRP(0) : XRP(350);
            env(pay(env.master, alice, addReserveBigSigners + fee - drops(1)));

            // Replace with the biggest possible signer list.  Should fail.
            Json::Value bigSigners = signers(
                alice,
                1,
                {{bogie, 1},
                 {demon, 1},
                 {ghost, 1},
                 {haunt, 1},
                 {jinni, 1},
                 {phase, 1},
                 {shade, 1},
                 {spook, 1}});
            env(bigSigners, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require(owners(alice, reserve1 ? 1 : 3));

            // Fund alice one more drop (plus the fee) and succeed.
            env(pay(env.master, alice, fee + drops(1)));
            env.close();
            env(bigSigners);
            env.close();
            env.require(owners(alice, reserve1 ? 1 : 10));
        }
        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none));
        env.close();
        env.require(owners(alice, 0));
    }

    void
    test_signerListSet(FeatureBitset features)
    {
        testcase("SignerListSet");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);

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
                {{bogie, 1},
                 {demon, 1},
                 {ghost, 1},
                 {haunt, 1},
                 {jinni, 1},
                 {phase, 1},
                 {demon, 1},
                 {spook, 1}}),
            ter(temBAD_SIGNER));

        // Set a quorum of zero.  Should fail.
        env(signers(alice, 0, {{bogie, 1}}), ter(temMALFORMED));

        // Make a signer list where the quorum can't be met.  Should fail.
        env(signers(
                alice,
                9,
                {{bogie, 1},
                 {demon, 1},
                 {ghost, 1},
                 {haunt, 1},
                 {jinni, 1},
                 {phase, 1},
                 {shade, 1},
                 {spook, 1}}),
            ter(temBAD_QUORUM));

        // clang-format off
        // Make a signer list that's too big.  Should fail. (Even with
        // ExpandedSignerList)
        Account const spare("spare", KeyType::secp256k1);
        env(signers(
                alice,
                1,
                features[featureExpandedSignerList]
                    ? std::vector<signer>{{bogie, 1}, {demon, 1}, {ghost, 1},
                                          {haunt, 1}, {jinni, 1}, {phase, 1},
                                          {shade, 1}, {spook, 1}, {spare, 1},
                                          {acc10, 1}, {acc11, 1}, {acc12, 1},
                                          {acc13, 1}, {acc14, 1}, {acc15, 1},
                                          {acc16, 1}, {acc17, 1}, {acc18, 1},
                                          {acc19, 1}, {acc20, 1}, {acc21, 1},
                                          {acc22, 1}, {acc23, 1}, {acc24, 1},
                                          {acc25, 1}, {acc26, 1}, {acc27, 1},
                                          {acc28, 1}, {acc29, 1}, {acc30, 1},
                                          {acc31, 1}, {acc32, 1}, {acc33, 1}}
                    : std::vector<signer>{{bogie, 1}, {demon, 1}, {ghost, 1},
                                          {haunt, 1}, {jinni, 1}, {phase, 1},
                                          {shade, 1}, {spook, 1}, {spare, 1}}),
            ter(temMALFORMED));
        // clang-format on
        env.close();
        env.require(owners(alice, 0));
    }

    void
    test_phantomSigners(FeatureBitset features)
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
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 4));

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
        env(noop(alice), msig(demon, demon), fee(3 * baseFee), ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(bogie, spook),
            fee(3 * baseFee),
            ter(tefBAD_SIGNATURE));
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
    test_fee(FeatureBitset features)
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
            {{bogie, 1},
             {demon, 1},
             {ghost, 1},
             {haunt, 1},
             {jinni, 1},
             {phase, 1},
             {shade, 1},
             {spook, 1}}));
        env.close();
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 10));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(bogie),
            fee((2 * baseFee) - 1),
            ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(bogie, demon, ghost, haunt, jinni, phase, shade, spook),
            fee(9 * baseFee));
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
    test_misorderedSigners(FeatureBitset features)
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
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 4));

        msig phantoms{bogie, demon};
        std::reverse(phantoms.signers.begin(), phantoms.signers.end());
        std::uint32_t const aliceSeq = env.seq(alice);
        env(noop(alice), phantoms, ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    test_masterSigners(FeatureBitset features)
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
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 4));

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
    test_regularSigners(FeatureBitset features)
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
        env(noop(alice), msig(msig::Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // cheri should not be able to multisign using her master key.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(cheri),
            fee(2 * baseFee),
            ter(tefMASTER_DISABLED));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // becky should be able to multisign using either of her keys.
        aliceSeq = env.seq(alice);
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(msig::Reg{becky, beck}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Both becky and cheri should be able to sign using regular keys.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(3 * baseFee),
            msig(msig::Reg{becky, beck}, msig::Reg{cheri, cher}));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    test_regularSignersUsingSubmitMulti(FeatureBitset features)
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
            auto jrr =
                env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
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
            jrr = env.rpc(
                "json",
                "submit_multisigned",
                to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        }

        {
            // failure case -- SigningPubKey not empty
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::SigningPubKey] =
                strHex(alice.pk().slice());
            cheri_sign(jv_one);
            auto jrr =
                env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "When multi-signing 'tx_json.SigningPubKey' must be empty.");
        }

        {
            // failure case - bad fee
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::Fee] = -1;
            cheri_sign(jv_one);
            auto jrr =
                env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
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
            jrr = env.rpc(
                "json",
                "submit_multisigned",
                to_string(jv_submit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid Fee field.  Fees must be greater than zero.");
        }

        {
            // failure case - bad fee v2
            aliceSeq = env.seq(alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::Fee] =
                alice["USD"](10).value().getFullText();
            cheri_sign(jv_one);
            auto jrr =
                env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
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
            jrr = env.rpc(
                "json",
                "submit_multisigned",
                to_string(jv_submit))[jss::result];
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
            auto jrr =
                env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
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
            jrr = env.rpc(
                "json",
                "submit_multisigned",
                to_string(jv_submit))[jss::result];
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
            jv[jss::tx_json][sfSigners.fieldName] =
                Json::Value{Json::arrayValue};
            becky_sign(jv);
            auto jrr = env.rpc(
                "json", "submit_multisigned", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "tx_json.Signers array may not be empty.");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }
    }

    void
    test_heterogeneousSigners(FeatureBitset features)
    {
        testcase("Heterogenious Signers");

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
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}, {daria, 1}, {jinni, 1}}),
            sig(alie));
        env.close();
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 6));

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
        env(noop(alice), msig(msig::Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(msig::Reg{daria, dari}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq(alice);
        env(noop(alice), msig(jinni), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        //  Should also work if all signers sign.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(5 * baseFee),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Require all signers to sign.
        env(signers(
                alice,
                0x3FFFC,
                {{becky, 0xFFFF},
                 {cheri, 0xFFFF},
                 {daria, 0xFFFF},
                 {jinni, 0xFFFF}}),
            sig(alie));
        env.close();
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 6));

        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(9 * baseFee),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Try cheri with both key types.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(5 * baseFee),
            msig(becky, cheri, msig::Reg{daria, dari}, jinni));
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
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 10));

        aliceSeq = env.seq(alice);
        env(noop(alice),
            fee(9 * baseFee),
            msig(
                becky,
                msig::Reg{cheri, cher},
                msig::Reg{daria, dari},
                haunt,
                jinni,
                phase,
                shade,
                spook));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // One signer short should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(becky, cheri, haunt, jinni, phase, shade, spook),
            fee(8 * baseFee),
            ter(tefBAD_QUORUM));
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
    test_keyDisable(FeatureBitset features)
    {
        testcase("Key Disable");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);

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
        env(fset(alice, asfDisableMaster),
            sig(alice),
            ter(tecNO_ALTERNATIVE_KEY));

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
        env(signers(alice, jtx::none),
            msig(bogie),
            fee(2 * baseFee),
            ter(tecNO_ALTERNATIVE_KEY));

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
    test_regKey(FeatureBitset features)
    {
        testcase("Regular Key");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::secp256k1};
        env.fund(XRP(1000), alice);

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

        env(signers(becky, 1, {{alice, 1}}), sig(becky));
        env(regkey(becky, alie), msig(alice), fee(0), ter(telINSUF_FEE_P));

        // Using the master key to sign for a regular key for free should
        // still work.
        env(regkey(becky, alie), sig(becky), fee(0));
    }

    // See if every kind of transaction can be successfully multi-signed.
    void
    test_txTypes(FeatureBitset features)
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
        int const signerListOwners{features[featureMultiSignReserve] ? 1 : 4};
        env.require(owners(alice, signerListOwners + 0));

        // Multisign a ttPAYMENT.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq(alice);
        env(pay(alice, env.master, XRP(1)),
            msig(becky, bogie),
            fee(3 * baseFee));
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
        env(trust("alice", USD(100)),
            msig(becky, bogie),
            fee(3 * baseFee),
            require(lines("alice", 1)));
        env.close();
        env.require(owners(alice, signerListOwners + 1));

        // Multisign a ttOFFER_CREATE transaction.
        env(pay(gw, alice, USD(50)));
        env.close();
        env.require(balance(alice, USD(50)));
        env.require(balance(gw, alice["USD"](-50)));

        std::uint32_t const offerSeq = env.seq(alice);
        env(offer(alice, XRP(50), USD(50)),
            msig(becky, bogie),
            fee(3 * baseFee));
        env.close();
        env.require(owners(alice, signerListOwners + 2));

        // Now multisign a ttOFFER_CANCEL canceling the offer we just created.
        {
            aliceSeq = env.seq(alice);
            env(offer_cancel(alice, offerSeq),
                seq(aliceSeq),
                msig(becky, bogie),
                fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env.require(owners(alice, signerListOwners + 1));
        }

        // Multisign a ttSIGNER_LIST_SET.
        env(signers(alice, 3, {{becky, 1}, {bogie, 1}, {demon, 1}}),
            msig(becky, bogie),
            fee(3 * baseFee));
        env.close();
        env.require(owners(alice, features[featureMultiSignReserve] ? 2 : 6));
    }

    void
    test_badSignatureText(FeatureBitset features)
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
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}), sig(alice));

        auto const baseFee = env.current()->fees().base;
        {
            // Single-sign, but leave an empty SigningPubKey.
            JTx tx = env.jt(noop(alice), sig(alice));
            STTx local = *(tx.stx);
            local.setFieldVL(sfSigningPubKey, Blob());  // Empty SigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Empty SigningPubKey.");
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
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid signature.");
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
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid signature.");
        }
        {
            // Multisign, but leave a nonempty sfSigningPubKey.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local[sfSigningPubKey] = alice.pk();  // Insert sfSigningPubKey
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Both multi- and single-sign with an empty SigningPubKey.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
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
                info[jss::result][jss::error_exception].asString().find(
                    "Invalid signature on account r") != std::string::npos);
        }
        {
            // Multisign with an empty signers array should fail.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local.peekFieldArray(sfSigners).clear();  // Empty Signers array.
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid Signers array size.");
        }
        {
            // Multisign 9 (!ExpandedSignerList) | 33 (ExpandedSignerList) times
            // should fail.
            JTx tx = env.jt(
                noop(alice),
                fee(2 * baseFee),

                features[featureExpandedSignerList] ? msig(
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
                                                          bogie)
                                                    : msig(
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
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid Signers array size.");
        }
        {
            // The account owner may not multisign for themselves.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(alice));
            STTx local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid multisigner.");
        }
        {
            // No duplicate multisignatures allowed.
            JTx tx = env.jt(noop(alice), fee(2 * baseFee), msig(bogie, bogie));
            STTx local = *(tx.stx);
            auto const info = submitSTTx(local);
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Duplicate Signers not allowed.");
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
            BEAST_EXPECT(
                info[jss::result][jss::error_exception] ==
                "fails local checks: Unsorted Signers array.");
        }
    }

    void
    test_noMultiSigners(FeatureBitset features)
    {
        testcase("No Multisigners");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        Account const becky{"becky", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        auto const baseFee = env.current()->fees().base;
        env(noop(alice),
            msig(becky, demon),
            fee(3 * baseFee),
            ter(tefNOT_MULTI_SIGNING));
    }

    void
    test_multisigningMultisigner(FeatureBitset features)
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
        env(noop(alice),
            msig(becky),
            fee(2 * baseFee),
            ter(tefMASTER_DISABLED));
        env.close();

        // Becky cannot 2-level multisign for alice.  2-level multisigning
        // is not supported.
        env(noop(alice),
            msig(msig::Reg{becky, bogie}),
            fee(2 * baseFee),
            ter(tefBAD_SIGNATURE));
        env.close();

        // Verify that becky cannot sign with a regular key that she has
        // not yet enabled.
        Account const beck{"beck", KeyType::ed25519};
        env(noop(alice),
            msig(msig::Reg{becky, beck}),
            fee(2 * baseFee),
            ter(tefBAD_SIGNATURE));
        env.close();

        // Once becky gives herself the regular key, she can sign for alice
        // using that regular key.
        env(regkey(becky, beck), msig(demon), fee(2 * baseFee));
        env.close();

        env(noop(alice), msig(msig::Reg{becky, beck}), fee(2 * baseFee));
        env.close();

        // The presence of becky's regular key does not influence whether she
        // can 2-level multisign; it still won't work.
        env(noop(alice),
            msig(msig::Reg{becky, demon}),
            fee(2 * baseFee),
            ter(tefBAD_SIGNATURE));
        env.close();
    }

    void
    test_signForHash(FeatureBitset features)
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
        std::string const hash1 =
            jvSig2[jss::result][jss::tx_json][jss::hash].asString();

        // Add the next signature and sign again.
        jvSig2[jss::result][jss::account] = ghost.human();
        jvSig2[jss::result][jss::secret] = ghost.name();
        Json::Value jvSubmit =
            env.rpc("json", "sign_for", to_string(jvSig2[jss::result]));
        BEAST_EXPECT(
            jvSubmit[jss::result][jss::status].asString() == "success");

        // Save the hash with two signatures for use later.
        std::string const hash2 =
            jvSubmit[jss::result][jss::tx_json][jss::hash].asString();
        BEAST_EXPECT(hash1 != hash2);

        // Submit the result of the two signatures.
        Json::Value jvResult = env.rpc(
            "json", "submit_multisigned", to_string(jvSubmit[jss::result]));
        BEAST_EXPECT(
            jvResult[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(
            jvResult[jss::result][jss::engine_result].asString() ==
            "tesSUCCESS");

        // The hash from the submit should be the same as the hash from the
        // second signing.
        BEAST_EXPECT(
            hash2 == jvResult[jss::result][jss::tx_json][jss::hash].asString());
        env.close();

        // The transaction we just submitted should now be available and
        // validated.
        Json::Value jvTx = env.rpc("tx", hash2);
        BEAST_EXPECT(jvTx[jss::result][jss::status].asString() == "success");
        BEAST_EXPECT(jvTx[jss::result][jss::validated].asString() == "true");
        BEAST_EXPECT(
            jvTx[jss::result][jss::meta][sfTransactionResult.jsonName]
                .asString() == "tesSUCCESS");
    }

    void
    test_amendmentTransition()
    {
        testcase("Amendment Transition");

        // The OwnerCount associated with a SignerList changes once the
        // featureMultiSignReserve amendment goes live.  Create a couple
        // of signer lists before and after the amendment goes live and
        // verify that the OwnerCount is managed properly for all of them.
        using namespace jtx;
        Account const alice{"alice", KeyType::secp256k1};
        Account const becky{"becky", KeyType::ed25519};
        Account const cheri{"cheri", KeyType::secp256k1};
        Account const daria{"daria", KeyType::ed25519};

        Env env{*this, supported_amendments() - featureMultiSignReserve};
        env.fund(XRP(1000), alice, becky, cheri, daria);
        env.close();

        // Give alice and becky signer lists before the amendment goes live.
        env(signers(alice, 1, {{bogie, 1}}));
        env(signers(
            becky,
            1,
            {{bogie, 1},
             {demon, 1},
             {ghost, 1},
             {haunt, 1},
             {jinni, 1},
             {phase, 1},
             {shade, 1},
             {spook, 1}}));
        env.close();

        env.require(owners(alice, 3));
        env.require(owners(becky, 10));

        // Enable the amendment.
        env.enableFeature(featureMultiSignReserve);
        env.close();

        // Give cheri and daria signer lists after the amendment goes live.
        env(signers(cheri, 1, {{bogie, 1}}));
        env(signers(
            daria,
            1,
            {{bogie, 1},
             {demon, 1},
             {ghost, 1},
             {haunt, 1},
             {jinni, 1},
             {phase, 1},
             {shade, 1},
             {spook, 1}}));
        env.close();

        env.require(owners(alice, 3));
        env.require(owners(becky, 10));
        env.require(owners(cheri, 1));
        env.require(owners(daria, 1));

        // Delete becky's signer list; her OwnerCount should drop to zero.
        // Replace alice's signer list; her OwnerCount should drop to one.
        env(signers(becky, jtx::none));
        env(signers(
            alice,
            1,
            {{bogie, 1},
             {demon, 1},
             {ghost, 1},
             {haunt, 1},
             {jinni, 1},
             {phase, 1},
             {shade, 1},
             {spook, 1}}));
        env.close();

        env.require(owners(alice, 1));
        env.require(owners(becky, 0));
        env.require(owners(cheri, 1));
        env.require(owners(daria, 1));

        // Delete the three remaining signer lists.  Everybody's OwnerCount
        // should now be zero.
        env(signers(alice, jtx::none));
        env(signers(cheri, jtx::none));
        env(signers(daria, jtx::none));
        env.close();

        env.require(owners(alice, 0));
        env.require(owners(becky, 0));
        env.require(owners(cheri, 0));
        env.require(owners(daria, 0));
    }

    void
    test_signersWithTickets(FeatureBitset features)
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
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}),
            ticket::use(aliceTicketSeq++));
        env.close();
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        auto const baseFee = env.current()->fees().base;
        env(noop(alice),
            msig(bogie, demon),
            fee(3 * baseFee),
            ticket::use(aliceTicketSeq++));
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
    test_signersWithTags(FeatureBitset features)
    {
        if (!features[featureExpandedSignerList])
            return;

        testcase("Signers With Tags");

        using namespace jtx;
        Env env{*this, features};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();
        uint8_t tag1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        uint8_t tag2[] =
            "hello world some ascii 32b long";  // including 1 byte for NUL

        uint256 bogie_tag = ripple::base_uint<256>::fromVoid(tag1);
        uint256 demon_tag = ripple::base_uint<256>::fromVoid(tag2);

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie, 1, bogie_tag}, {demon, 1, demon_tag}}));
        env.close();
        env.require(owners(alice, features[featureMultiSignReserve] ? 1 : 4));

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
        env(noop(alice), msig(demon, demon), fee(3 * baseFee), ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq(alice);
        env(noop(alice),
            msig(bogie, spook),
            fee(3 * baseFee),
            ter(tefBAD_SIGNATURE));
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
    testAll(FeatureBitset features)
    {
        test_noReserve(features);
        test_signerListSet(features);
        test_phantomSigners(features);
        test_fee(features);
        test_misorderedSigners(features);
        test_masterSigners(features);
        test_regularSigners(features);
        test_regularSignersUsingSubmitMulti(features);
        test_heterogeneousSigners(features);
        test_keyDisable(features);
        test_regKey(features);
        test_txTypes(features);
        test_badSignatureText(features);
        test_noMultiSigners(features);
        test_multisigningMultisigner(features);
        test_signForHash(features);
        test_signersWithTickets(features);
        test_signersWithTags(features);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const all = supported_amendments();

        // The reserve required on a signer list changes based on
        // featureMultiSignReserve.  Limits on the number of signers
        // changes based on featureExpandedSignerList.  Test both with and
        // without.
        testAll(all - featureMultiSignReserve - featureExpandedSignerList);
        testAll(all - featureExpandedSignerList);
        testAll(all);
        test_amendmentTransition();
    }
};

BEAST_DEFINE_TESTSUITE(MultiSign, app, ripple);

}  // namespace test
}  // namespace ripple
