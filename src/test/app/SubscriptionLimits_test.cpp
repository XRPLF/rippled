#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/subscription.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <tuple>

namespace xrpl::test {

class SubscriptionLimits_test : public beast::unit_test::Suite
{
    FeatureBitset const all_{jtx::testableAmendments() | featureSubscription};

    void
    testFrequencyPastTimeRange()
    {
        testcase("Frequency whose period ends past the time range");
        using namespace jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");

        // A full claim is allowed once; the next period never starts.
        auto const fullClaims = [&](bool endAtLimit) {
            Env env{*this, all_};
            env.fund(XRP(10000), alice, bob);
            env.close();

            std::uint32_t const start =
                env.current()->header().parentCloseTime.time_since_epoch().count();
            std::uint32_t const frequency = endAtLimit
                ? static_cast<std::uint32_t>((std::uint64_t{1} << 32) - start)
                : std::uint32_t{0xFFFF'FFFF};

            auto const subId = keylet::subscription(alice.id(), bob.id(), env.seq(alice)).key;
            env(subscription::create(alice, bob, XRP(100), NetClock::duration{frequency}));
            env.close();

            auto const aliceBefore = env.balance(alice);
            env(subscription::claim(bob, subId, XRP(100)));
            env.close();
            for (int i = 0; i < 3; ++i)
            {
                env(subscription::claim(bob, subId, XRP(100)), Ter(tecTOO_SOON));
                env.close();
            }
            BEAST_EXPECT(env.balance(alice) == aliceBefore - XRP(100));
        };
        fullClaims(true);
        fullClaims(false);

        // After a partial claim, the rest of the period is all that remains.
        {
            Env env{*this, all_};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const subId = keylet::subscription(alice.id(), bob.id(), env.seq(alice)).key;
            env(subscription::create(alice, bob, XRP(100), NetClock::duration{0xFFFF'FFFF}));
            env.close();

            auto const aliceBefore = env.balance(alice);
            env(subscription::claim(bob, subId, XRP(50)));
            env.close();
            env(subscription::claim(bob, subId, XRP(100)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
            env(subscription::claim(bob, subId, XRP(50)));
            env.close();
            env(subscription::claim(bob, subId, XRP(50)), Ter(tecTOO_SOON));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBefore - XRP(100));
        }
    }

    void
    testClaimAmountSign()
    {
        testcase("Claim amount must be positive");
        using namespace jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gateway");
        auto const usd = gw["USD"];

        Env env{*this, all_};
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(usd(1000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();
        env(fset(alice, asfDepositAuth));
        env.close();

        auto const xrpSub = keylet::subscription(alice.id(), bob.id(), env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s, std::nullopt, tfSingleUse));
        auto const usdSub = keylet::subscription(alice.id(), bob.id(), env.seq(alice));
        env(subscription::create(alice, bob, usd(10), 100s));
        env.close();

        auto const aliceBefore = env.balance(alice);
        env(subscription::claim(bob, xrpSub.key, XRP(-5)), Ter(temBAD_AMOUNT));
        env(subscription::claim(bob, xrpSub.key, XRP(0)), Ter(temBAD_AMOUNT));
        env(subscription::claim(bob, usdSub.key, usd(-5)), Ter(temBAD_AMOUNT));
        env(subscription::claim(bob, usdSub.key, usd(0)), Ter(temBAD_AMOUNT));
        env.close();

        // The DepositAuth source receives nothing and the single-use subscription stays.
        BEAST_EXPECT(env.balance(alice) == aliceBefore);
        auto const sle = env.le(xrpSub);
        if (BEAST_EXPECT(sle))
        {
            BEAST_EXPECT(sle->getFieldAmount(sfBalance) == XRP(10));
        }
    }

    void
    testIssuerNoRipple()
    {
        testcase("Claim respects the issuer's NoRipple");
        using namespace jtx;
        using namespace std::chrono_literals;

        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];

        Env env{*this, all_};
        env.fund(XRP(10000), gw, alice, bob);
        env.close();
        env(trust(alice, usd(10000)));
        env(trust(bob, usd(10000)));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();

        auto const subId = keylet::subscription(alice.id(), bob.id(), env.seq(alice)).key;
        env(subscription::create(alice, bob, usd(10), 100s));
        env.close();

        // The issuer sets NoRipple on its side of both lines.
        env(pay(alice, gw, usd(100)));
        env.close();
        env(fclear(gw, asfDefaultRipple));
        env(trust(gw, alice["USD"](0), tfSetNoRipple));
        env(trust(gw, bob["USD"](0), tfSetNoRipple));
        env.close();
        env(pay(gw, alice, usd(100)));
        env.close();

        auto const aliceBefore = env.balance(alice, usd);
        auto const bobBefore = env.balance(bob, usd);
        env(subscription::claim(bob, subId, usd(10)), Ter(terNO_RIPPLE));
        env.close();
        BEAST_EXPECT(env.balance(alice, usd) == aliceBefore);
        BEAST_EXPECT(env.balance(bob, usd) == bobBefore);

        // A new subscription between the same holders is refused too.
        env(subscription::create(alice, bob, usd(10), 100s), Ter(terNO_RIPPLE));
    }

    void
    testPseudoAccountDestination()
    {
        testcase("Pseudo-account destination");
        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, all_};
        Account const owner("owner");
        Account const alice("alice");
        env.fund(XRP(10'000), owner, alice);
        env.close();

        Vault const vault{env};
        auto [createTx, vaultKeylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(createTx);
        env.close();

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        Account const pseudo{"vault-pseudo", vaultSle->at(sfAccount)};
        env.memoize(pseudo);

        env(subscription::create(alice, pseudo, XRP(10), 100s), Ter(tecNO_PERMISSION));
        env.close();

        env(Vault::del({.owner = owner, .id = vaultKeylet.key}));
        env.close();
        BEAST_EXPECT(!env.le(vaultKeylet));
    }

    void
    testDisabledFieldsInInnerObjects()
    {
        testcase("Subscription fields in inner objects while disabled");
        using namespace jtx;

        FeatureBitset const disabled = all_ - featureSubscription;
        Account const alice("alice");
        Account const bob("bob");

        // A DelegateSet Permissions element named Memo carrying Subscription fields.
        auto const delegateWith = [&](SField const& field, json::Value const& value) {
            Env env{*this, disabled};
            env.fund(XRP(100000), alice, bob);
            env.close();

            json::Value inner;
            inner[sfPermissionValue.jsonName] = "Payment";
            inner[field.jsonName] = value;
            json::Value element;
            element[sfMemo.jsonName] = inner;
            json::Value permissions(json::ValueType::Array);
            permissions.append(element);

            json::Value jv;
            jv[jss::TransactionType] = jss::DelegateSet;
            jv[jss::Account] = alice.human();
            jv[sfAuthorize.jsonName] = bob.human();
            jv[sfPermissions.jsonName] = permissions;

            env(jv, Ter(temDISABLED));
            env.close();
            BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));
        };
        delegateWith(sfFrequency, 1);
        delegateWith(
            sfSubscriptionID, "0102030405060708090A0B0C0D0E0F100102030405060708090A0B0C0D0E0F10");

        // A Signers element named Memo carrying Frequency.
        {
            Env env{*this, disabled};
            env.fund(XRP(1000), alice, bob);
            env.close();
            env(signers(alice, 1, {{bob, 1}}));
            env.close();

            auto const baseFee = env.current()->fees().base;
            JTx const jt = env.jt(noop(alice), Msig(bob), Fee(2 * baseFee));
            STObject const& signer = jt.stx->getFieldArray(sfSigners)[0];
            STObject memoSigner(sfMemo);
            memoSigner.setAccountID(sfAccount, signer.getAccountID(sfAccount));
            memoSigner.setFieldVL(sfSigningPubKey, signer.getFieldVL(sfSigningPubKey));
            memoSigner.setFieldVL(sfTxnSignature, signer.getFieldVL(sfTxnSignature));
            STArray memoSigners(sfSigners);
            memoSigners.push_back(memoSigner);
            // Replace the array rather than assign into it, which would keep the element names.
            STObject tx = *jt.stx;
            tx.makeFieldAbsent(sfSigners);
            tx.setFieldArray(sfSigners, memoSigners);
            Serializer ser;
            tx.add(ser);
            Blob blob = ser.peekData();

            // Frequency (UINT32 code 86, value 86400) as the first field of the Memo element.
            std::array<std::uint8_t, 3> const memoStart{0xEA, 0x73, 0x21};
            auto const found = std::ranges::search(blob, memoStart);
            if (!BEAST_EXPECT(!found.empty()))
                return;
            std::array<std::uint8_t, 6> const frequency{0x20, 0x56, 0x00, 0x01, 0x51, 0x80};
            blob.insert(std::next(found.begin()), frequency.begin(), frequency.end());

            std::uint32_t const seqBefore = env.seq(alice);
            auto const jr = env.rpc("submit", strHex(blob));
            auto const result = jr[jss::result][jss::engine_result].asString();
            env.close();
            BEAST_EXPECTS(result == "temDISABLED", json::to_string(jr));
            BEAST_EXPECT(env.seq(alice) == seqBefore);
        }

        // A BatchSigners element named Memo carrying StartTime.
        {
            Env env{*this, disabled};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const bobSeq = env.seq(bob);
            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const signedBatch = env.jt(
                batch::outer(alice, aliceSeq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(10)), aliceSeq + 1),
                batch::Inner(pay(bob, alice, XRP(5)), bobSeq),
                batch::Sig(bob));

            json::Value jv = signedBatch.jv;
            json::Value memo = jv[sfBatchSigners.jsonName][0u][sfBatchSigner.jsonName];
            memo[sfStartTime.jsonName] = 1;
            json::Value element(json::ValueType::Object);
            element[sfMemo.jsonName] = memo;
            jv[sfBatchSigners.jsonName][0u] = element;

            env(env.jt(jv, Ter(std::ignore)));
            auto const result = env.ter();
            env.close();
            BEAST_EXPECT(!isTesSuccess(result));
            BEAST_EXPECT(env.seq(bob) == bobSeq);
        }
    }

public:
    void
    run() override
    {
        testFrequencyPastTimeRange();
        testClaimAmountSign();
        testIssuerNoRipple();
        testPseudoAccountDestination();
        testDisabledFieldsInInnerObjects();
    }
};

BEAST_DEFINE_TESTSUITE(SubscriptionLimits, app, xrpl);

}  // namespace xrpl::test
