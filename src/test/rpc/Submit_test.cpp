#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <string>

namespace xrpl::test {

class Submit_test : public beast::unit_test::Suite
{
public:
    void
    testFailHardValidation()
    {
        testcase("fail_hard parameter validation");
        using namespace jtx;
        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Lambda to test invalid fail_hard parameter types
        auto testInvalidFailHard = [&](auto const& param) {
            // Test with tx_blob path
            {
                JTx const jt = env.jt(pay(alice, bob, XRP(1)));
                auto const txBlob = strHex(jt.stx->getSerializer().slice());

                json::Value params;
                params[jss::tx_blob] = txBlob;
                params[jss::fail_hard] = param;
                auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'fail_hard', not boolean.");
            }

            // Test with tx_json path (deprecated signing)
            {
                json::Value params;
                params[jss::secret] = toBase58(generateSeed("alice"));
                params[jss::tx_json] = pay("alice", "bob", XRP(1));
                params[jss::fail_hard] = param;
                auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'fail_hard', not boolean.");
            }
        };

        // Test all invalid types
        testInvalidFailHard("true");
        testInvalidFailHard("yes");
        testInvalidFailHard(1);
        testInvalidFailHard(0);
        testInvalidFailHard(1.5);
        testInvalidFailHard(json::Value(json::ValueType::Object));
        testInvalidFailHard(json::Value(json::ValueType::Array));

        // Valid boolean values should work (not return invalidParams)
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = true;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
        {
            JTx const jt = env.jt(pay(alice, bob, XRP(1)));
            auto const txBlob = strHex(jt.stx->getSerializer().slice());

            json::Value params;
            params[jss::tx_blob] = txBlob;
            params[jss::fail_hard] = false;
            auto const jrr = env.rpc("json", "submit", to_string(params))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::error) || jrr[jss::error] != "invalidParams");
        }
    }

    void
    testSTIssueV1SignedVaultSubmit()
    {
        testcase("V1 STIssue signed Vault tx succeeds submit signature verification");
        using namespace jtx;

        Env env{*this};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(100'000), issuer, owner);
        env.close();
        BEAST_EXPECT(env.current()->rules().enabled(fixCleanup3_2_0));

        MPTTester mpt{env, issuer, kMPT_INIT_NO_FUND};
        mpt.create({.flags = tfMPTCanTransfer | tfMPTRequireAuth});
        mpt.authorize({.account = owner});
        mpt.authorize({.account = issuer, .holder = owner});

        PrettyAsset const asset = mpt.issuanceID();
        env(pay(issuer, owner, asset(100)));
        env.close();

        Vault const vault{env};
        auto [jv, keylet] = vault.create({.owner = owner, .asset = asset});
        (void)keylet;
        jv[jss::Fee] = to_string(env.current()->fees().base);
        jv[jss::Sequence] = env.seq(owner);
        jv[jss::SigningPubKey] = strHex(owner.pk().slice());

        STTx tx{parse(jv)};
        tx.sign(owner.pk(), owner.sk());
        BEAST_EXPECT(tx.checkSign(env.current()->rules()));

        auto const jrr = env.rpc("submit", strHex(tx.getSerializer().slice()))[jss::result];
        BEAST_EXPECT(jrr[jss::engine_result] == "tesSUCCESS");
    }

    void
    testSTIssueV2SignedVaultSubmit()
    {
        testcase("V2 STIssue signed Vault tx fails submit signature verification");
        using namespace jtx;

        Env env{*this};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(100'000), issuer, owner);
        env.close();
        BEAST_EXPECT(env.current()->rules().enabled(fixCleanup3_2_0));

        MPTTester mpt{env, issuer, kMPT_INIT_NO_FUND};
        mpt.create({.flags = tfMPTCanTransfer | tfMPTRequireAuth});
        mpt.authorize({.account = owner});
        mpt.authorize({.account = issuer, .holder = owner});

        PrettyAsset const asset = mpt.issuanceID();
        env(pay(issuer, owner, asset(100)));
        env.close();

        Vault const vault{env};
        auto [jv, keylet] = vault.create({.owner = owner, .asset = asset});
        (void)keylet;
        jv[jss::Fee] = to_string(env.current()->fees().base);
        jv[jss::Sequence] = env.seq(owner);
        jv[jss::SigningPubKey] = strHex(owner.pk().slice());

        // Model an external client that already writes the V2 STIssue wire
        // format. The test must not rely on CurrentTransactionRulesGuard to
        // produce the bytes that are signed.
        MPTIssue const mptIssue = asset.raw().get<MPTIssue>();
        auto const serializeV1Asset = [&mptIssue]() {
            Serializer s;
            STIssue const st{sfAsset, Asset{mptIssue}};
            st.addFieldID(s);
            st.add(s);
            return s.getData();
        };
        auto const serializeV2Asset = [&mptIssue]() {
            Serializer s;
            STIssue const st{sfAsset, Asset{mptIssue}};
            st.addFieldID(s);
            s.addBitString(mptIssue.getIssuer());
            s.addBitString(xrpAccount());
            s.addRaw(mptIssue.getMptID().data(), sizeof(std::uint32_t));
            return s.getData();
        };
        auto const replaceAsset = [this](Blob data, Blob const& from, Blob const& to) {
            BEAST_EXPECT(from.size() == to.size());
            auto found = std::ranges::search(data, from);
            BEAST_EXPECT(!found.empty());
            if (!found.empty())
                std::copy(to.begin(), to.end(), found.begin());
            return data;
        };

        Blob const v1Asset = serializeV1Asset();
        Blob const v2Asset = serializeV2Asset();
        BEAST_EXPECT(v1Asset != v2Asset);

        STTx tx{parse(jv)};
        Serializer signingData;
        signingData.add32(HashPrefix::TxSign);
        tx.addWithoutSigningFields(signingData);
        Blob const clientSigningData = replaceAsset(signingData.getData(), v1Asset, v2Asset);
        auto const sig = sign(owner.pk(), owner.sk(), makeSlice(clientSigningData));
        Slice const sigSlice{sig.data(), sig.size()};
        BEAST_EXPECT(verify(owner.pk(), makeSlice(clientSigningData), sigSlice));
        tx.setFieldVL(sfTxnSignature, sigSlice);

        Serializer txSerializer;
        tx.add(txSerializer);
        Blob const clientTx = replaceAsset(txSerializer.getData(), v1Asset, v2Asset);

        SerialIter sit{makeSlice(clientTx)};
        STTx const submittedTx{sit};
        BEAST_EXPECT(submittedTx[sfAsset] == asset.raw());
        BEAST_EXPECT(!submittedTx.checkSign(env.current()->rules()));

        auto const jrr = env.rpc("submit", strHex(clientTx))[jss::result];
        BEAST_EXPECT(jrr[jss::error] == "invalidTransaction");
        BEAST_EXPECT(
            jrr[jss::error_exception].asString().find("Invalid signature") != std::string::npos);
    }

    void
    run() override
    {
        testFailHardValidation();
        testSTIssueV1SignedVaultSubmit();
        testSTIssueV2SignedVaultSubmit();
    }
};

BEAST_DEFINE_TESTSUITE(Submit, rpc, xrpl);

}  // namespace xrpl::test
