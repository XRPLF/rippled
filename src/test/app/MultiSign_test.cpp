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

#include <BeastConfig.h>
#include <ripple/protocol/JsonFields.h>     // jss:: definitions
#include <ripple/protocol/Feature.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

class MultiSign_test : public beast::unit_test::suite
{
    // Unfunded accounts to use for phantom signing.
    jtx::Account const bogie {"bogie", KeyType::secp256k1};
    jtx::Account const demon {"demon", KeyType::ed25519};
    jtx::Account const ghost {"ghost", KeyType::secp256k1};
    jtx::Account const haunt {"haunt", KeyType::ed25519};
    jtx::Account const jinni {"jinni", KeyType::secp256k1};
    jtx::Account const phase {"phase", KeyType::ed25519};
    jtx::Account const shade {"shade", KeyType::secp256k1};
    jtx::Account const spook {"spook", KeyType::ed25519};

public:
    void test_noReserve()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};

        // Pay alice enough to meet the initial reserve, but not enough to
        // meet the reserve for a SignerListSet.
        env.fund(XRP(200), alice);
        env.close();
        env.require (owners (alice, 0));

        {
            // Attach a signer list to alice.  Should fail.
            Json::Value smallSigners = signers(alice, 1, { { bogie, 1 } });
            env(smallSigners, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require (owners (alice, 0));

            // Fund alice enough to set the signer list, then attach signers.
            env(pay(env.master, alice, XRP(151)));
            env.close();
            env(smallSigners);
            env.close();
            env.require (owners (alice, 3));
        }
        {
            // Replace with the biggest possible signer list.  Should fail.
            Json::Value bigSigners = signers(alice, 1, {
                { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
                { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 }});
            env(bigSigners, ter(tecINSUFFICIENT_RESERVE));
            env.close();
            env.require (owners (alice, 3));

            // Fund alice and succeed.
            env(pay(env.master, alice, XRP(350)));
            env.close();
            env(bigSigners);
            env.close();
            env.require (owners (alice, 10));
        }
        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none));
        env.close();
        env.require (owners (alice, 0));
    }

    void test_signerListSet()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);

        // Add alice as a multisigner for herself.  Should fail.
        env(signers(alice, 1, { { alice, 1} }), ter (temBAD_SIGNER));

        // Add a signer with a weight of zero.  Should fail.
        env(signers(alice, 1, { { bogie, 0} }), ter (temBAD_WEIGHT));

        // Add a signer where the weight is too big.  Should fail since
        // the weight field is only 16 bits.  The jtx framework can't do
        // this kind of test, so it's commented out.
//      env(signers(alice, 1, { { bogie, 0x10000} }), ter (temBAD_WEIGHT));

        // Add the same signer twice.  Should fail.
        env(signers(alice, 1, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { demon, 1 }, { spook, 1 }}),
            ter(temBAD_SIGNER));

        // Set a quorum of zero.  Should fail.
        env(signers(alice, 0, { { bogie, 1} }), ter (temMALFORMED));

        // Make a signer list where the quorum can't be met.  Should fail.
        env(signers(alice, 9, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 }}),
            ter(temBAD_QUORUM));

        // Make a signer list that's too big.  Should fail.
        Account const spare ("spare", KeyType::secp256k1);
        env(signers(alice, 1, {
            { bogie, 1 }, { demon, 1 }, { ghost, 1 }, { haunt, 1 },
            { jinni, 1 }, { phase, 1 }, { shade, 1 }, { spook, 1 },
            { spare, 1 }}),
            ter(temMALFORMED));

        env.close();
        env.require (owners (alice, 0));
    }

    void test_phantomSigners()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach phantom signers to alice and use them for a transaction.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        env.close();
        env.require (owners (alice, 4));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Either signer alone should work.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(demon), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Duplicate signers should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(demon, demon), fee(3 * baseFee), ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // A non-signer should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(bogie, spook), fee(3 * baseFee), ter(tefBAD_SIGNATURE));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Don't meet the quorum.  Should fail.
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}));
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Meet the quorum.  Should succeed.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie, demon), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void
    test_enablement()
    {
        using namespace jtx;
        Env env(*this, FeatureBitset{});
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // NOTE: These six tests will fail when multisign is default enabled.
        env(signers(alice, 1, {{bogie, 1}}), ter(temDISABLED));
        env.close();
        env.require (owners (alice, 0));

        std::uint32_t aliceSeq = env.seq (alice);
        auto const baseFee = env.current()->fees().base;
        env(noop(alice), msig(bogie), fee(2 * baseFee), ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env(signers(alice, 1, {{bogie, 1}, {demon,1}}), ter(temDISABLED));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        {
            Json::Value jvParams;
            jvParams[jss::account] = alice.human();
            auto const jsmr = env.rpc("json", "submit_multisigned", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jsmr[jss::error]         == "notEnabled");
            BEAST_EXPECT(jsmr[jss::status]        == "error");
            BEAST_EXPECT(jsmr[jss::error_message] == "Not enabled in configuration.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::account] = alice.human();
            auto const jsmr = env.rpc("json", "sign_for", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jsmr[jss::error]         == "notEnabled");
            BEAST_EXPECT(jsmr[jss::status]        == "error");
            BEAST_EXPECT(jsmr[jss::error_message] == "Not enabled in configuration.");
        }
    }

    void test_fee ()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // Attach maximum possible number of signers to alice.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}, {ghost, 1}, {haunt, 1},
            {jinni, 1}, {phase, 1}, {shade, 1}, {spook, 1}}));
        env.close();
        env.require (owners (alice, 10));

        // This should work.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(bogie), fee(2 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(bogie), fee((2 * baseFee) - 1), ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // This should work.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(bogie, demon, ghost, haunt, jinni, phase, shade, spook),
            fee(9 * baseFee));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // This should fail because the fee is too small.
        aliceSeq = env.seq (alice);
        env(noop(alice),
            msig(bogie, demon, ghost, haunt, jinni, phase, shade, spook),
            fee((9 * baseFee) - 1),
            ter(telINSUF_FEE_P));
        env.close();

        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void test_misorderedSigners()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // The signatures in a transaction must be submitted in sorted order.
        // Make sure the transaction fails if they are not.
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}));
        env.close();
        env.require (owners(alice, 4));

        msig phantoms {bogie, demon};
        std::reverse (phantoms.signers.begin(), phantoms.signers.end());
        std::uint32_t const aliceSeq = env.seq (alice);
        env(noop(alice), phantoms, ter(temINVALID));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void test_masterSigners()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
        Account const becky {"becky", KeyType::secp256k1};
        Account const cheri {"cheri", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // For a different situation, give alice a regular key but don't use it.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        env.close();
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), sig(alice));
        env(noop(alice), sig(alie));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 2);

        //Attach signers to alice
        env(signers(alice, 4, {{becky, 3}, {cheri, 4}}), sig (alice));
        env.close();
        env.require (owners (alice, 4));

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // If we don't meet the quorum the transaction should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky), fee(2 * baseFee), ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Give becky and cheri regular keys.
        Account const beck {"beck", KeyType::ed25519};
        env(regkey (becky, beck));
        Account const cher {"cher", KeyType::ed25519};
        env(regkey (cheri, cher));
        env.close();

        // becky's and cheri's master keys should still work.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, cheri), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void test_regularSigners()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const cheri {"cheri", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1, {{becky, 1}, {cheri, 1}}), sig (alice));

        // Give everyone regular keys.
        Account const alie {"alie", KeyType::ed25519};
        env(regkey (alice, alie));
        Account const beck {"beck", KeyType::secp256k1};
        env(regkey (becky, beck));
        Account const cher {"cher", KeyType::ed25519};
        env(regkey (cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset (cheri, asfDisableMaster), sig(cheri));
        env.close();

        // Attempt a multisigned transaction that meets the quorum.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // cheri should not be able to multisign using her master key.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee), ter(tefMASTER_DISABLED));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // becky should be able to multisign using either of her keys.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{becky, beck}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Both becky and cheri should be able to sign using regular keys.
        aliceSeq = env.seq (alice);
        env(noop(alice), fee(3 * baseFee),
            msig(msig::Reg{becky, beck}, msig::Reg{cheri, cher}));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
    }

    void test_regularSignersUsingSubmitMulti()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const cheri {"cheri", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky, cheri);
        env.close();

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {cheri, 1}}), sig (alice));

        // Give everyone regular keys.
        Account const beck {"beck", KeyType::secp256k1};
        env(regkey (becky, beck));
        Account const cher {"cher", KeyType::ed25519};
        env(regkey (cheri, cher));
        env.close();

        // Disable cheri's master key to mix things up.
        env(fset (cheri, asfDisableMaster), sig(cheri));
        env.close();

        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq;

        // these represent oft-repeated setup for input json below
        auto setup_tx = [&]() -> Json::Value {
            Json::Value jv;
            jv[jss::tx_json][jss::Account]         = alice.human();
            jv[jss::tx_json][jss::TransactionType] = "AccountSet";
            jv[jss::tx_json][jss::Fee]             = static_cast<uint32_t>(8 * baseFee);
            jv[jss::tx_json][jss::Sequence]        = env.seq(alice);
            jv[jss::tx_json][jss::SigningPubKey]   = "";
            return jv;
        };
        auto cheri_sign = [&](Json::Value& jv) {
            jv[jss::account]       = cheri.human();
            jv[jss::key_type]      = "ed25519";
            jv[jss::passphrase]    = cher.name();
        };
        auto becky_sign = [&](Json::Value& jv) {
            jv[jss::account] = becky.human();
            jv[jss::secret]  = beck.name();
        };

        {
            // Attempt a multisigned transaction that meets the quorum.
            // using sign_for and submit_multisigned
            aliceSeq = env.seq (alice);
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
            //failure case -- SigningPubKey not empty
            aliceSeq = env.seq (alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::SigningPubKey]   = strHex(alice.pk().slice());
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error]  == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message]  == "When multi-signing 'tx_json.SigningPubKey' must be empty.");
        }

        {
            //failure case - bad fee
            aliceSeq = env.seq (alice);
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
            BEAST_EXPECT(jrr[jss::error]  == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message]  == "Invalid Fee field.  Fees must be greater than zero.");
        }

        {
            //failure case - bad fee v2
            aliceSeq = env.seq (alice);
            Json::Value jv_one = setup_tx();
            jv_one[jss::tx_json][jss::Fee]  = alice["USD"](10).value().getFullText();
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
            BEAST_EXPECT(jrr[jss::error]  == "internal");
            BEAST_EXPECT(jrr[jss::error_message]  == "Internal error.");
        }

        {
            // cheri should not be able to multisign using her master key.
            aliceSeq = env.seq (alice);
            Json::Value jv = setup_tx();
            jv[jss::account]                       = cheri.human();
            jv[jss::secret]                        = cheri.name();
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error]  == "masterDisabled");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        {
            // Unlike cheri, becky should also be able to sign using her master key
            aliceSeq = env.seq (alice);
            Json::Value jv_one = setup_tx();
            cheri_sign(jv_one);
            auto jrr = env.rpc("json", "sign_for", to_string(jv_one))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            // for the second sign_for, use the returned tx_json with
            // first signer info
            Json::Value jv_two;
            jv_two[jss::tx_json]    = jrr[jss::tx_json];
            jv_two[jss::account]    = becky.human();
            jv_two[jss::key_type]   = "ed25519";
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
            jv[jss::tx_json][jss::Account]         = "DEADBEEF";
            cheri_sign(jv);
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error]  == "srcActMalformed");

            Account const jimmy {"jimmy"};
            jv[jss::tx_json][jss::Account]         = jimmy.human();
            jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error]  == "srcActNotFound");
        }

        {
            aliceSeq = env.seq (alice);
            Json::Value jv = setup_tx();
            jv[jss::tx_json][sfSigners.fieldName]  = Json::Value{Json::arrayValue};
            becky_sign(jv);
            auto jrr = env.rpc("json", "submit_multisigned", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(jrr[jss::error]  == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message]  == "tx_json.Signers array may not be empty.");
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }
    }

    void test_heterogeneousSigners()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const cheri {"cheri", KeyType::secp256k1};
        Account const daria {"daria", KeyType::ed25519};
        env.fund(XRP(1000), alice, becky, cheri, daria);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        env(fset (alice, asfDisableMaster), sig(alice));

        // becky is master only without a regular key.

        // cheri has a regular key, but leaves the master key enabled.
        Account const cher {"cher", KeyType::secp256k1};
        env(regkey (cheri, cher));

        // daria has a regular key and disables her master key.
        Account const dari {"dari", KeyType::ed25519};
        env(regkey (daria, dari));
        env(fset (daria, asfDisableMaster), sig(daria));
        env.close();

        // Attach signers to alice.
        env(signers(alice, 1,
            {{becky, 1}, {cheri, 1}, {daria, 1}, {jinni, 1}}), sig (alie));
        env.close();
        env.require (owners (alice, 6));

        // Each type of signer should succeed individually.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(cheri), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{cheri, cher}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(msig::Reg{daria, dari}), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        aliceSeq = env.seq (alice);
        env(noop(alice), msig(jinni), fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        //  Should also work if all signers sign.
        aliceSeq = env.seq (alice);
        env(noop(alice), fee(5 * baseFee),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Require all signers to sign.
        env(signers(alice, 0x3FFFC, {{becky, 0xFFFF},
            {cheri, 0xFFFF}, {daria, 0xFFFF}, {jinni, 0xFFFF}}), sig (alie));
        env.close();
        env.require (owners (alice, 6));

        aliceSeq = env.seq (alice);
        env(noop(alice), fee(9 * baseFee),
            msig(becky, msig::Reg{cheri, cher}, msig::Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Try cheri with both key types.
        aliceSeq = env.seq (alice);
        env(noop(alice), fee(5 * baseFee),
            msig(becky, cheri, msig::Reg{daria, dari}, jinni));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Makes sure the maximum allowed number of signers works.
        env(signers(alice, 0x7FFF8, {{becky, 0xFFFF}, {cheri, 0xFFFF},
            {daria, 0xFFFF}, {haunt, 0xFFFF}, {jinni, 0xFFFF},
            {phase, 0xFFFF}, {shade, 0xFFFF}, {spook, 0xFFFF}}), sig (alie));
        env.close();
        env.require (owners (alice, 10));

        aliceSeq = env.seq (alice);
        env(noop(alice), fee(9 * baseFee), msig(becky, msig::Reg{cheri, cher},
            msig::Reg{daria, dari}, haunt, jinni, phase, shade, spook));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // One signer short should fail.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, cheri, haunt, jinni, phase, shade, spook),
            fee(8 * baseFee), ter (tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        // Remove alice's signer list and get the owner count back.
        env(signers(alice, jtx::none), sig(alie));
        env.close();
        env.require (owners (alice, 0));
    }

    // We want to always leave an account signable.  Make sure the that we
    // disallow removing the last way a transaction may be signed.
    void test_keyDisable()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::ed25519};
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
        env(fset (alice, asfDisableMaster),
            sig(alice), ter(tecNO_ALTERNATIVE_KEY));

        // Add a regular key.
        Account const alie {"alie", KeyType::ed25519};
        env(regkey (alice, alie));

        // M1: The master key can be disabled if there's a regular key.
        env(fset (alice, asfDisableMaster), sig(alice));

        // R0: A lone regular key cannot be removed.
        env(regkey (alice, disabled), sig(alie), ter(tecNO_ALTERNATIVE_KEY));

        // Add a signer list.
        env(signers(alice, 1, {{bogie, 1}}), sig (alie));

        // R1: The regular key can be removed if there's a signer list.
        env(regkey (alice, disabled), sig(alie));

        // L0: A lone signer list cannot be removed.
        auto const baseFee = env.current()->fees().base;
        env(signers(alice, jtx::none), msig(bogie),
            fee(2 * baseFee), ter(tecNO_ALTERNATIVE_KEY));

        // Enable the master key.
        env(fclear (alice, asfDisableMaster), msig(bogie), fee(2 * baseFee));

        // L1: The signer list can be removed if the master key is enabled.
        env(signers(alice, jtx::none), msig(bogie), fee(2 * baseFee));

        // Add a signer list.
        env(signers(alice, 1, {{bogie, 1}}), sig (alice));

        // M2: The master key can be disabled if there's a signer list.
        env(fset (alice, asfDisableMaster), sig(alice));

        // Add a regular key.
        env(regkey (alice, alie), msig(bogie), fee(2 * baseFee));

        // L2: The signer list can be removed if there's a regular key.
        env(signers(alice, jtx::none), sig(alie));

        // Enable the master key.
        env(fclear (alice, asfDisableMaster), sig(alie));

        // R2: The regular key can be removed if the master key is enabled.
        env(regkey (alice, disabled), sig(alie));
    }

    // Verify that the first regular key can be made for free using the
    // master key, but not when multisigning.
    void test_regKey()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        env.fund(XRP(1000), alice);

        // Give alice a regular key with a zero fee.  Should succeed.  Once.
        Account const alie {"alie", KeyType::ed25519};
        env(regkey (alice, alie), sig (alice), fee (0));

        // Try it again and creating the regular key for free should fail.
        Account const liss {"liss", KeyType::secp256k1};
        env(regkey (alice, liss), sig (alice), fee (0), ter(telINSUF_FEE_P));

        // But paying to create a regular key should succeed.
        env(regkey (alice, liss), sig (alice));

        // In contrast, trying to multisign for a regular key with a zero
        // fee should always fail.  Even the first time.
        Account const becky {"becky", KeyType::ed25519};
        env.fund(XRP(1000), becky);

        env(signers(becky, 1, {{alice, 1}}), sig (becky));
        env(regkey (becky, alie), msig (alice), fee (0), ter(telINSUF_FEE_P));

        // Using the master key to sign for a regular key for free should
        // still work.
        env(regkey (becky, alie), sig (becky), fee (0));
    }

    // See if every kind of transaction can be successfully multi-signed.
    void test_txTypes()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice {"alice", KeyType::secp256k1};
        Account const becky {"becky", KeyType::ed25519};
        Account const zelda {"zelda", KeyType::secp256k1};
        Account const gw {"gw"};
        auto const USD = gw["USD"];
        env.fund(XRP(1000), alice, becky, zelda, gw);
        env.close();

        // alice uses a regular key with the master disabled.
        Account const alie {"alie", KeyType::secp256k1};
        env(regkey (alice, alie));
        env(fset (alice, asfDisableMaster), sig(alice));

        // Attach signers to alice.
        env(signers(alice, 2, {{becky, 1}, {bogie, 1}}), sig (alie));
        env.close();
        env.require (owners (alice, 4));

        // Multisign a ttPAYMENT.
        auto const baseFee = env.current()->fees().base;
        std::uint32_t aliceSeq = env.seq (alice);
        env(pay(alice, env.master, XRP(1)),
            msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttACCOUNT_SET.
        aliceSeq = env.seq (alice);
        env(noop(alice), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttREGULAR_KEY_SET.
        aliceSeq = env.seq (alice);
        Account const ace {"ace", KeyType::secp256k1};
        env(regkey (alice, ace), msig(becky, bogie), fee(3 * baseFee));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Multisign a ttTRUST_SET
        env(trust("alice", USD(100)),
            msig(becky, bogie), fee(3 * baseFee), require (lines("alice", 1)));
        env.close();
        env.require (owners (alice, 5));

        // Multisign a ttOFFER_CREATE transaction.
        env(pay(gw, alice, USD(50)));
        env.close();
        env.require(balance(alice, USD(50)));
        env.require(balance(gw, alice["USD"](-50)));

        std::uint32_t const offerSeq = env.seq (alice);
        env(offer(alice, XRP(50), USD(50)),
            msig (becky, bogie), fee(3 * baseFee));
        env.close();
        env.require(owners(alice, 6));

        // Now multisign a ttOFFER_CANCEL canceling the offer we just created.
        {
            aliceSeq = env.seq (alice);
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = offerSeq;
            cancelOffer[jss::TransactionType] = "OfferCancel";
            env (cancelOffer, seq (aliceSeq),
                msig (becky, bogie), fee(3 * baseFee));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            env.require(owners(alice, 5));
        }

        // Multisign a ttSIGNER_LIST_SET.
        env(signers(alice, 3, {{becky, 1}, {bogie, 1}, {demon, 1}}),
            msig (becky, bogie), fee(3 * baseFee));
        env.close();
        env.require (owners (alice, 6));
    }

    void test_badSignatureText()
    {
        // Verify that the text returned for signature failures is correct.
        using namespace jtx;

        Env env(*this);

        // lambda that submits an STTx and returns the resulting JSON.
        auto submitSTTx = [&env] (STTx const& stx)
        {
            Json::Value jvResult;
            jvResult[jss::tx_blob] = strHex (stx.getSerializer().slice());
            return env.rpc ("json", "submit", to_string(jvResult));
        };

        Account const alice {"alice"};
        env.fund(XRP(1000), alice);
        env(signers(alice, 1, {{bogie, 1}, {demon, 1}}), sig (alice));

        auto const baseFee = env.current()->fees().base;
        {
            // Single-sign, but leave an empty SigningPubKey.
            JTx tx = env.jt (noop (alice), sig(alice));
            STTx local = *(tx.stx);
            local.setFieldVL (sfSigningPubKey, Blob()); // Empty SigningPubKey
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Empty SigningPubKey.");
        }
        {
            // Single-sign, but invalidate the signature.
            JTx tx = env.jt (noop (alice), sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto badSig = local.getFieldVL (sfTxnSignature);
            badSig[20] ^= 0xAA;
            local.setFieldVL (sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                    "fails local checks: Invalid signature.");
        }
        {
            // Single-sign, but invalidate the sequence number.
            JTx tx = env.jt (noop (alice), sig(alice));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto seq = local.getFieldU32 (sfSequence);
            local.setFieldU32 (sfSequence, seq + 1);
            // Signature should fail.
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                    "fails local checks: Invalid signature.");
        }
        {
            // Multisign, but leave a nonempty sfSigningPubKey.
            JTx tx = env.jt (noop (alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local[sfSigningPubKey] = alice.pk(); // Insert sfSigningPubKey
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Both multi- and single-sign with an empty SigningPubKey.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local.sign (alice.pk(), alice.sk());
            local.setFieldVL (sfSigningPubKey, Blob()); // Empty SigningPubKey
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Cannot both single- and multi-sign.");
        }
        {
            // Multisign but invalidate one of the signatures.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            // Flip some bits in the signature.
            auto& signer = local.peekFieldArray (sfSigners).back();
            auto badSig = signer.getFieldVL (sfTxnSignature);
            badSig[20] ^= 0xAA;
            signer.setFieldVL (sfTxnSignature, badSig);
            // Signature should fail.
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception].asString().
                find ("Invalid signature on account r") != std::string::npos);
        }
        {
            // Multisign with an empty signers array should fail.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(bogie));
            STTx local = *(tx.stx);
            local.peekFieldArray (sfSigners).clear(); // Empty Signers array.
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                    "fails local checks: Invalid Signers array size.");
        }
        {
            // Multisign 9 times should fail.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee),
                msig(bogie, bogie, bogie,
                    bogie, bogie, bogie, bogie, bogie, bogie));
            STTx local = *(tx.stx);
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid Signers array size.");
        }
        {
            // The account owner may not multisign for themselves.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(alice));
            STTx local = *(tx.stx);
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Invalid multisigner.");
        }
        {
            // No duplicate multisignatures allowed.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(bogie, bogie));
            STTx local = *(tx.stx);
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Duplicate Signers not allowed.");
        }
        {
            // Multisignatures must be submitted in sorted order.
            JTx tx = env.jt (noop(alice), fee(2 * baseFee), msig(bogie, demon));
            STTx local = *(tx.stx);
            // Unsort the Signers array.
            auto& signers = local.peekFieldArray (sfSigners);
            std::reverse (signers.begin(), signers.end());
            // Signature should fail.
            auto const info = submitSTTx (local);
            BEAST_EXPECT(info[jss::result][jss::error_exception] ==
                "fails local checks: Unsorted Signers array.");
        }
    }

    void test_noMultiSigners()
    {
        using namespace jtx;
        Env env {*this};
        Account const alice {"alice", KeyType::ed25519};
        Account const becky {"becky", KeyType::secp256k1};
        env.fund(XRP(1000), alice, becky);
        env.close();

        auto const baseFee = env.current()->fees().base;
        env(noop(alice), msig(becky, demon), fee(3 * baseFee), ter(tefNOT_MULTI_SIGNING));
    }

    void test_multisigningMultisigner()
    {
        // Set up a signer list where one of the signers has both the
        // master disabled and no regular key (because that signer is
        // exclusively multisigning).  That signer should no longer be
        // able to successfully sign the signer list.

        using namespace jtx;
        Env env (*this);
        Account const alice {"alice", KeyType::ed25519};
        Account const becky {"becky", KeyType::secp256k1};
        env.fund (XRP(1000), alice, becky);
        env.close();

        // alice sets up a signer list with becky as a signer.
        env (signers (alice, 1, {{becky, 1}}));
        env.close();

        // becky sets up her signer list.
        env (signers (becky, 1, {{bogie, 1}, {demon, 1}}));
        env.close();

        // Because becky has not (yet) disabled her master key, she can
        // multisign a transaction for alice.
        auto const baseFee = env.current()->fees().base;
        env (noop (alice), msig (becky), fee (2 * baseFee));
        env.close();

        // Now becky disables her master key.
        env (fset (becky, asfDisableMaster));
        env.close();

        // Since becky's master key is disabled she can no longer
        // multisign for alice.
        env (noop (alice), msig (becky), fee (2 * baseFee),
            ter (tefMASTER_DISABLED));
        env.close();

        // Becky cannot 2-level multisign for alice.  2-level multisigning
        // is not supported.
        env (noop (alice), msig (msig::Reg {becky, bogie}), fee (2 * baseFee),
            ter (tefBAD_SIGNATURE));
        env.close();

        // Verify that becky cannot sign with a regular key that she has
        // not yet enabled.
        Account const beck {"beck", KeyType::ed25519};
        env (noop (alice), msig (msig::Reg {becky, beck}), fee (2 * baseFee),
            ter (tefBAD_SIGNATURE));
        env.close();

        // Once becky gives herself the regular key, she can sign for alice
        // using that regular key.
        env (regkey (becky, beck), msig (demon), fee (2 * baseFee));
        env.close();

        env (noop (alice), msig (msig::Reg {becky, beck}), fee (2 * baseFee));
        env.close();

        // The presence of becky's regular key does not influence whether she
        // can 2-level multisign; it still won't work.
        env (noop (alice), msig (msig::Reg {becky, demon}), fee (2 * baseFee),
            ter (tefBAD_SIGNATURE));
        env.close();
    }

    void run() override
    {
        test_noReserve();
        test_signerListSet();
        test_phantomSigners();
        test_enablement();
        test_fee();
        test_misorderedSigners();
        test_masterSigners();
        test_regularSigners();
        test_regularSignersUsingSubmitMulti();
        test_heterogeneousSigners();
        test_keyDisable();
        test_regKey();
        test_txTypes();
        test_badSignatureText();
        test_noMultiSigners();
        test_multisigningMultisigner();
    }
};

BEAST_DEFINE_TESTSUITE(MultiSign, app, ripple);

} // test
} // ripple
