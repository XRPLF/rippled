#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/batch.h>
#include <test/jtx/delegate.h>
#include <test/jtx/delivermin.h>
#include <test/jtx/did.h>
#include <test/jtx/domain.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/Dir.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xrpl::test {
class Delegate_test : public beast::unit_test::Suite
{
    void
    testFeatureDisabled(FeatureBitset features)
    {
        testcase("test feature not enabled");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1000000), gw, alice, bob);
        env.close();

        auto res = features[featurePermissionDelegationV1_1] ? Ter(tesSUCCESS) : Ter(temDISABLED);

        // can not set Delegate when feature disabled
        env(delegate::set(gw, alice, {"Payment"}), res);
        env.close();

        // can not send delegating transaction when feature disabled
        env(pay(gw, bob, XRP(100)), delegate::As(alice), res);
    }

    void
    testDelegateSet()
    {
        testcase("test valid request creating, updating, deleting permissions");
        using namespace jtx;

        Env env(*this);
        Account const gw{"gateway"};
        Account const alice{"alice"};
        env.fund(XRP(100000), gw, alice);
        env.close();

        // delegating an empty permission list when the delegate ledger object
        // does not exist is not allowed
        env(delegate::set(gw, alice, {}), Ter(tecNO_ENTRY));
        env.close();

        auto const permissions = std::vector<std::string>{
            "Payment", "EscrowCreate", "EscrowFinish", "TrustlineAuthorize", "CheckCreate"};
        env(delegate::set(gw, alice, permissions));
        env.close();

        // this lambda function is used to compare the json value of ledger
        // entry response with the given vector of permissions.
        auto comparePermissions = [&](json::Value const& jle,
                                      std::vector<std::string> const& permissions,
                                      Account const& account,
                                      Account const& authorize) {
            BEAST_EXPECT(
                !jle[jss::result].isMember(jss::error) && jle[jss::result].isMember(jss::node));
            BEAST_EXPECT(jle[jss::result][jss::node]["LedgerEntryType"] == jss::Delegate);
            BEAST_EXPECT(jle[jss::result][jss::node][jss::Account] == account.human());
            BEAST_EXPECT(jle[jss::result][jss::node][sfAuthorize.jsonName] == authorize.human());

            auto const& jPermissions = jle[jss::result][jss::node][sfPermissions.jsonName];
            unsigned i = 0;
            for (auto const& permission : permissions)
            {
                BEAST_EXPECT(
                    jPermissions[i][sfPermission.jsonName][sfPermissionValue.jsonName] ==
                    permission);
                i++;
            }
        };

        // get ledger entry with valid parameter
        comparePermissions(delegate::entry(env, gw, alice), permissions, gw, alice);

        // gw updates permission
        auto const newPermissions =
            std::vector<std::string>{"Payment", "AMMCreate", "AMMDeposit", "AMMWithdraw"};
        env(delegate::set(gw, alice, newPermissions));
        env.close();

        // get ledger entry again, permissions should be updated to
        // newPermissions
        comparePermissions(delegate::entry(env, gw, alice), newPermissions, gw, alice);

        // gw deletes all permissions delegated to alice, this will delete the ledger entry
        env(delegate::set(gw, alice, {}));
        env.close();
        auto const jle = delegate::entry(env, gw, alice);
        BEAST_EXPECT(jle[jss::result][jss::error] == "entryNotFound");

        // alice can delegate permissions to gw as well
        env(delegate::set(alice, gw, permissions));
        env.close();
        comparePermissions(delegate::entry(env, alice, gw), permissions, alice, gw);
        auto const response = delegate::entry(env, gw, alice);
        // alice has not been granted any permissions by gw
        BEAST_EXPECT(response[jss::result][jss::error] == "entryNotFound");
    }

    void
    testInvalidRequest(FeatureBitset features)
    {
        testcase("test invalid DelegateSet");
        using namespace jtx;

        Env env(*this, features);
        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(100000), gw, alice, bob);
        env.close();

        // when permissions size exceeds the limit 10, should return
        // temARRAY_TOO_LARGE
        {
            env(delegate::set(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "EscrowCancel",
                     "CheckCreate",
                     "CheckCash",
                     "CheckCancel",
                     "DepositPreauth",
                     "TrustSet",
                     "NFTokenMint",
                     "NFTokenBurn"}),
                Ter(temARRAY_TOO_LARGE));
        }

        // alice can not authorize herself
        {
            env(delegate::set(alice, alice, {"Payment"}), Ter(temMALFORMED));
        }

        // bad fee
        {
            json::Value jv;
            jv[jss::TransactionType] = jss::DelegateSet;
            jv[jss::Account] = gw.human();
            jv[sfAuthorize.jsonName] = alice.human();
            json::Value permissionsJson(json::ValueType::Array);
            json::Value permissionValue;
            permissionValue[sfPermissionValue.jsonName] = "Payment";
            json::Value permissionObj;
            permissionObj[sfPermission.jsonName] = permissionValue;
            permissionsJson.append(permissionObj);
            jv[sfPermissions.jsonName] = permissionsJson;
            jv[sfFee.jsonName] = -1;
            env(jv, Ter(temBAD_FEE));
        }

        // when provided permissions contains duplicate values, should return
        // temMALFORMED
        {
            env(delegate::set(
                    gw,
                    alice,
                    {"Payment",
                     "EscrowCreate",
                     "EscrowFinish",
                     "TrustlineAuthorize",
                     "CheckCreate",
                     "TrustlineAuthorize"}),
                Ter(temMALFORMED));
        }

        // when authorizing account which does not exist, should return
        // tecNO_TARGET
        {
            env(delegate::set(gw, Account("unknown"), {"Payment"}), Ter(tecNO_TARGET));
        }

        // non-delegable transaction
        {
            env(delegate::set(gw, alice, {"SetRegularKey"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"AccountSet"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"SignerListSet"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"DelegateSet"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"EnableAmendment"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"UNLModify"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"SetFee"}), Ter(temMALFORMED));
            env(delegate::set(gw, alice, {"Batch"}), Ter(temMALFORMED));
        }
    }

    void
    testReserve()
    {
        testcase("test reserve");
        using namespace jtx;

        // reserve requirement not met
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};

            auto const txFee = env.current()->fees().base;
            env.fund(env.current()->fees().accountReserve(0) + txFee, alice);
            env.fund(XRP(100000), bob);
            env.close();

            // alice does not have enough reserve to create Delegate
            env(delegate::set(alice, bob, {"Payment"}), Ter(tecINSUFFICIENT_RESERVE));
        }

        // reserve recovered after deleting delegation object
        {
            Env env(*this);
            Account const bob{"bob"};
            Account const alice{"alice"};
            Account const carol{"carol"};

            auto const txFee = env.current()->fees().base;

            env.fund(env.current()->fees().accountReserve(1) + (txFee * 4), alice);
            env.fund(XRP(100000), bob, carol);
            env.close();

            // alice consumes 1 txFee and requires 1 object reserve
            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            // alice does not have enough reserve to create another delegation object
            env(delegate::set(alice, carol, {"Payment"}), Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            // deleting delegation object recovers 1 reserve
            env(delegate::set(alice, bob, {}));
            env.close();

            // now alice can delegate again
            env(delegate::set(alice, carol, {"Payment"}));
        }

        // test reserve when sending transaction on behalf of other account
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(drops(env.current()->fees().accountReserve(1)), alice);
            env.fund(drops(env.current()->fees().accountReserve(2)), bob);
            env.close();

            // alice gives bob permission
            env(delegate::set(alice, bob, {"DIDSet", "DIDDelete"}));
            env.close();

            // bob set DID on behalf of alice, but alice does not have enough
            // reserve
            env(did::set(alice), did::Uri("uri"), delegate::As(bob), Ter(tecINSUFFICIENT_RESERVE));

            // bob can set DID for himself because he has enough reserve
            env(did::set(bob), did::Uri("uri"));
            env.close();
        }
    }

    void
    testFee()
    {
        testcase("test fee");
        using namespace jtx;

        // Common setup: fund alice, bob, carol with 1000 XRP.
        auto setup = [&](Env& env) {
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();
            return std::make_tuple(alice, bob, carol);
        };

        // No fee deduction for terNO_DELEGATE_PERMISSION.
        {
            Env env(*this);
            auto [alice, bob, carol] = setup(env);

            auto const aliceBalance = env.balance(alice);
            auto const bobBalance = env.balance(bob);
            auto const carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        // Delegate pays the fee successfully.
        {
            Env env(*this);
            auto [alice, bob, carol] = setup(env);
            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const aliceBalance = env.balance(alice);
            auto const bobBalance = env.balance(bob);
            auto const carolBalance = env.balance(carol);

            auto const sendAmt = XRP(100);
            auto const feeAmt = XRP(10);
            env(pay(alice, carol, sendAmt), Fee(feeAmt), delegate::As(bob));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
            BEAST_EXPECT(env.balance(bob) == bobBalance - feeAmt);
            BEAST_EXPECT(env.balance(carol) == carolBalance + sendAmt);
        }

        // Bob has insufficient balance to pay the fee, will get terINSUF_FEE_B.
        {
            Env env(*this);
            auto [alice, bob, carol] = setup(env);
            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const aliceBalance = env.balance(alice);
            auto const bobBalance = env.balance(bob);
            auto const carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(2000)),
                delegate::As(bob),
                Ter(terINSUF_FEE_B));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        // The delegated account has enough balance to pay and delegator has enough reserve
        {
            // Common setup: fund accounts and grant Bob permission to pay on Alice's behalf.
            // Alice is funded with exactly (paymentAmount + reserve + baseFee): baseFee covers
            // the DelegateSet tx cost, leaving Alice with exactly (paymentAmount + reserve).
            // highFee = reserve + baseFee, strictly greater than reserve, so that
            // max(reserve, highFee) = highFee — making the direct payment check fail.
            auto setup = [&](Env& env) {
                Account const alice{"alice"};
                Account const bob{"bob"};
                Account const carol{"carol"};

                auto const baseFee = env.current()->fees().base;
                auto const reserve = env.current()->fees().accountReserve(1);
                auto const paymentAmount = XRP(1);
                auto const highFee = reserve + baseFee;
                BEAST_EXPECT(highFee > reserve);

                env.fund(paymentAmount + reserve + baseFee, alice);
                env.fund(XRP(1000), bob);
                env.fund(XRP(1000), carol);
                env.close();

                env(delegate::set(alice, bob, {"Payment"}));
                env.close();

                env.require(Balance(alice, paymentAmount + reserve));

                return std::make_tuple(alice, bob, carol, paymentAmount, highFee, reserve);
            };

            // Alice's balance (paymentAmount + reserve) is insufficient to cover both
            // the payment and highFee directly. Even though fees are allowed to dip
            // below reserve, when Alice pays the fee herself the required funds =
            // paymentAmount + max(reserve, highFee) = paymentAmount + highFee
            // (since highFee > reserve), which still exceeds her balance.
            // tec: highFee is consumed from Alice's balance.
            {
                Env env(*this);
                auto [alice, bob, carol, paymentAmount, highFee, reserve] = setup(env);
                auto const aliceBalance = env.balance(alice);
                auto const bobBalance = env.balance(bob);
                auto const carolBalance = env.balance(carol);

                env(pay(alice, carol, paymentAmount), Fee(highFee), Ter(tecUNFUNDED_PAYMENT));

                // tec consumes the fee from Alice; carol and bob are unaffected.
                BEAST_EXPECT(env.balance(alice) == aliceBalance - highFee);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(carol) == carolBalance);
            }

            // The payment succeeds because the delegated account pays the fee.
            // Alice only needs (paymentAmount + reserve).
            {
                Env env(*this);
                auto [alice, bob, carol, paymentAmount, highFee, reserve] = setup(env);

                auto const alicePrePay = env.balance(alice, XRP);
                auto const bobPrePay = env.balance(bob, XRP);
                auto const carolPrePay = env.balance(carol, XRP);

                env(pay(alice, carol, paymentAmount), delegate::As(bob), Fee(highFee));
                env.close();

                env.require(Balance(alice, alicePrePay - paymentAmount));
                env.require(Balance(bob, bobPrePay - highFee));
                env.require(Balance(carol, carolPrePay + paymentAmount));
            }
        }

        // Delegated account can pay the fee even if it dips below reserve.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};

            auto const baseFee = env.current()->fees().base;
            auto const baseReserve = env.current()->fees().accountReserve(0);

            env.fund(env.current()->fees().accountReserve(1) + baseFee + XRP(1), alice);
            env.fund(baseReserve, bob);
            env.fund(XRP(1000), carol);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const alicePreTx = env.balance(alice, XRP);
            auto const bobPreTx = env.balance(bob, XRP);

            // After paying for this transaction, bob's balance will
            // dip below the base reserve
            env(pay(alice, carol, XRP(1)), delegate::As(bob));
            env.close();

            // Bob's balance is now less than the base reserve.
            BEAST_EXPECT(env.balance(bob, XRP) < baseReserve);
            env.require(Balance(bob, bobPreTx - drops(baseFee)));

            // Alice's balance only decreased by the 1.0 XRP she sent.
            env.require(Balance(alice, alicePreTx - XRP(1)));
        }

        // The delegated account has enough balance for the fee, but delegator
        // runs into tecUNFUNDED_PAYMENT.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};

            auto const baseFee = env.current()->fees().base;
            auto const reserve = env.current()->fees().accountReserve(1);

            // Alice is funded with (reserve + baseFee): after DelegateSet she has
            // exactly 'reserve', which is insufficient to send XRP(10) while keeping
            // reserve. Bob has plenty to pay the fee.
            env.fund(reserve + baseFee, alice);
            env.fund(XRP(1000), bob);
            env.fund(XRP(1000), carol);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const alicePrePay = env.balance(alice, XRP);
            auto const bobPrePay = env.balance(bob, XRP);
            auto const carolPrePay = env.balance(carol, XRP);

            // Bob pays the fee, but Alice has insufficient balance to send XRP(10).
            env(pay(alice, carol, XRP(10)), delegate::As(bob), Ter(tecUNFUNDED_PAYMENT));

            env.require(Balance(alice, alicePrePay));
            env.require(Balance(bob, bobPrePay - drops(baseFee)));
            env.require(Balance(carol, carolPrePay));
        }
    }

    void
    testSequence()
    {
        testcase("test sequence");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        auto aliceSeq = env.seq(alice);
        auto bobSeq = env.seq(bob);
        env(delegate::set(alice, bob, {"Payment"}));
        env(delegate::set(bob, alice, {"Payment"}));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
        BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
        aliceSeq = env.seq(alice);
        bobSeq = env.seq(bob);

        for (auto i = 0; i < 20; ++i)
        {
            // bob is the delegated account, his sequence won't kIncrement
            env(pay(alice, carol, XRP(10)), Fee(XRP(10)), delegate::As(bob));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            BEAST_EXPECT(env.seq(bob) == bobSeq);
            aliceSeq = env.seq(alice);

            // bob sends payment for himself, his sequence will kIncrement
            env(pay(bob, carol, XRP(10)), Fee(XRP(10)));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
            BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
            bobSeq = env.seq(bob);

            // alice is the delegated account, her sequence won't kIncrement
            env(pay(bob, carol, XRP(10)), Fee(XRP(10)), delegate::As(alice));
            env.close();
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
            BEAST_EXPECT(env.seq(bob) == bobSeq + 1);
            bobSeq = env.seq(bob);

            // alice sends payment for herself, her sequence will kIncrement
            env(pay(alice, carol, XRP(10)), Fee(XRP(10)));
            BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);
            BEAST_EXPECT(env.seq(bob) == bobSeq);
            aliceSeq = env.seq(alice);
        }
    }

    void
    testAccountDelete()
    {
        testcase("test deleting account");
        using namespace jtx;

        // Delegator (alice) deletes account: Delegate object is cleaned up from
        // both alice's and bob's owner directories.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const delegateKey = keylet::delegate(alice.id(), bob.id());
            BEAST_EXPECT(env.closed()->exists(delegateKey));

            auto hasKey = [](xrpl::Dir const& dir, uint256 const& key) {
                return std::any_of(  // NOLINT(modernize-use-ranges)
                    dir.begin(), dir.end(), [&](auto const& sle) { return sle->key() == key; });
            };

            // Delegate object should appear in both alice's and bob's directories
            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(alice.id())), delegateKey.key));
            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(bob.id())), delegateKey.key));

            for (std::uint32_t i = 0; i < 256; ++i)
                env.close();

            auto const aliceBalance = env.balance(alice);
            auto const carolBalance = env.balance(carol);

            // alice deletes account, this will remove the Delegate object from
            // both alice's and bob's owner directories
            auto const deleteFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, carol), Fee(deleteFee));
            env.close();

            BEAST_EXPECT(!env.closed()->exists(keylet::account(alice.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(alice.id())));
            BEAST_EXPECT(!env.closed()->exists(delegateKey));
            // bob's directory should no longer reference the Delegate object
            BEAST_EXPECT(
                !hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(bob.id())), delegateKey.key));
            BEAST_EXPECT(env.balance(carol) == carolBalance + aliceBalance - deleteFee);
        }

        // Delegatee (bob) deletes account: Delegate object is cleaned up from
        // both alice's and bob's owner directories, freeing alice's reserve so
        // she can subsequently delete her own account.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto const delegateKey = keylet::delegate(alice.id(), bob.id());
            BEAST_EXPECT(env.closed()->exists(delegateKey));

            auto hasKey = [](xrpl::Dir const& dir, uint256 const& key) {
                return std::any_of(  // NOLINT(modernize-use-ranges)
                    dir.begin(), dir.end(), [&](auto const& sle) { return sle->key() == key; });
            };

            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(alice.id())), delegateKey.key));
            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(bob.id())), delegateKey.key));

            // The Delegate entry counts against alice's ownerCount.
            auto const sleAlice = env.closed()->read(keylet::account(alice.id()));
            BEAST_EXPECT(sleAlice);
            BEAST_EXPECT(sleAlice->getFieldU32(sfOwnerCount) == 1);

            for (std::uint32_t i = 0; i < 256; ++i)
                env.close();

            auto const bobBalance = env.balance(bob);
            auto const carolBalance = env.balance(carol);

            // bob (the authorized/delegatee account) deletes his account.
            // This must clean up the Delegate object from both alice's and
            // bob's owner directories so alice's delegation does not survive
            // a potential account resurrection.
            auto const deleteFee = drops(env.current()->fees().increment);
            env(acctdelete(bob, carol), Fee(deleteFee));
            env.close();

            BEAST_EXPECT(!env.closed()->exists(keylet::account(bob.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(bob.id())));
            BEAST_EXPECT(!env.closed()->exists(delegateKey));
            // alice's directory should no longer reference the Delegate object
            BEAST_EXPECT(
                !hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(alice.id())), delegateKey.key));
            BEAST_EXPECT(env.balance(carol) == carolBalance + bobBalance - deleteFee);

            // alice's ownerCount is now 0; she can delete her own account.
            auto const sleAlice2 = env.closed()->read(keylet::account(alice.id()));
            BEAST_EXPECT(sleAlice2);
            BEAST_EXPECT(sleAlice2->getFieldU32(sfOwnerCount) == 0);

            auto const aliceDeleteFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, carol), Fee(aliceDeleteFee));
            env.close();

            BEAST_EXPECT(!env.closed()->exists(keylet::account(alice.id())));
        }

        // Multiple delegators -> same delegatee: when the delegatee (bob)
        // deletes his account, ALL Delegate objects (from alice and carol)
        // must be cleaned up from every delegator's directory.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            Account const dave{"dave"};
            env.fund(XRP(100000), alice, bob, carol, dave);
            env.close();

            // Both alice and carol delegate to bob
            env(delegate::set(alice, bob, {"Payment"}));
            env(delegate::set(carol, bob, {"EscrowCreate"}));
            env.close();

            auto const aliceBobKey = keylet::delegate(alice.id(), bob.id());
            auto const carolBobKey = keylet::delegate(carol.id(), bob.id());

            auto hasKey = [](xrpl::Dir const& dir, uint256 const& key) {
                return std::any_of(  // NOLINT(modernize-use-ranges)
                    dir.begin(), dir.end(), [&](auto const& sle) { return sle->key() == key; });
            };

            // Both Delegate objects exist and are in bob's directory
            BEAST_EXPECT(env.closed()->exists(aliceBobKey));
            BEAST_EXPECT(env.closed()->exists(carolBobKey));
            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(bob.id())), aliceBobKey.key));
            BEAST_EXPECT(
                hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(bob.id())), carolBobKey.key));

            for (std::uint32_t i = 0; i < 256; ++i)
                env.close();

            auto const bobBalance = env.balance(bob);
            auto const daveBalance = env.balance(dave);

            auto const deleteFee = drops(env.current()->fees().increment);
            env(acctdelete(bob, dave), Fee(deleteFee));
            env.close();

            // bob's account and directory are gone
            BEAST_EXPECT(!env.closed()->exists(keylet::account(bob.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(bob.id())));

            // Both Delegate objects are erased
            BEAST_EXPECT(!env.closed()->exists(aliceBobKey));
            BEAST_EXPECT(!env.closed()->exists(carolBobKey));

            // alice's and carol's directories no longer reference the objects
            BEAST_EXPECT(
                !hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(alice.id())), aliceBobKey.key));
            BEAST_EXPECT(
                !hasKey(xrpl::Dir(*env.closed(), keylet::ownerDir(carol.id())), carolBobKey.key));

            BEAST_EXPECT(env.balance(dave) == daveBalance + bobBalance - deleteFee);
        }
    }

    void
    testDelegateTransaction()
    {
        testcase("test delegate transaction");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};

        XRPAmount const baseFee{env.current()->fees().base};

        // use different initial amount to distinguish the source balance
        env.fund(XRP(10000), alice);
        env.fund(XRP(20000), bob);
        env.fund(XRP(30000), carol);
        env.close();

        auto aliceBalance = env.balance(alice, XRP);
        auto bobBalance = env.balance(bob, XRP);
        auto carolBalance = env.balance(carol, XRP);

        // can not send transaction on one's own behalf
        env(pay(alice, bob, XRP(50)), delegate::As(alice), Ter(temBAD_SIGNER));
        env.require(Balance(alice, aliceBalance));

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();
        env.require(Balance(alice, aliceBalance - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);

        // bob pays 50 XRP to carol on behalf of alice
        env(pay(alice, carol, XRP(50)), delegate::As(bob));
        env.close();
        env.require(Balance(alice, aliceBalance - XRP(50)));
        env.require(Balance(carol, carolBalance + XRP(50)));
        // bob pays the fee
        env.require(Balance(bob, bobBalance - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);
        carolBalance = env.balance(carol, XRP);

        // bob pays 50 XRP to bob self on behalf of alice
        env(pay(alice, bob, XRP(50)), delegate::As(bob));
        env.close();
        env.require(Balance(alice, aliceBalance - XRP(50)));
        env.require(Balance(bob, bobBalance + XRP(50) - drops(baseFee)));
        aliceBalance = env.balance(alice, XRP);
        bobBalance = env.balance(bob, XRP);

        // bob pay 50 XRP to alice herself on behalf of alice
        env(pay(alice, alice, XRP(50)), delegate::As(bob), Ter(temREDUNDANT));
        env.close();

        // bob does not have permission to create check
        env(check::create(alice, bob, XRP(10)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

        // carol does not have permission to create check
        env(check::create(alice, bob, XRP(10)),
            delegate::As(carol),
            Ter(terNO_DELEGATE_PERMISSION));
    }

    void
    testPaymentGranular(FeatureBitset features)
    {
        testcase("test payment granular");
        using namespace jtx;

        // test PaymentMint and PaymentBurn
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gateway"};
            Account const gw2{"gateway2"};
            auto const usd = gw["USD"];
            auto const eur = gw2["EUR"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw, gw2);
            env.trust(usd(200), alice);
            env.trust(eur(400), gw);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};
            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);
            auto gw2Balance = env.balance(gw2, XRP);

            // delegate ledger object is not created yet
            env(pay(gw, alice, usd(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.require(Balance(bob, bobBalance));

            // gw gives bob burn permission
            env(delegate::set(gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(Balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob sends a payment transaction on behalf of gw
            env(pay(gw, alice, usd(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            env.require(Balance(bob, bobBalance));

            // gw gives bob mint permission, alice gives bob burn permission
            env(delegate::set(gw, bob, {"PaymentMint"}));
            env(delegate::set(alice, bob, {"PaymentBurn"}));
            env.close();
            env.require(Balance(alice, aliceBalance - drops(baseFee)));
            env.require(Balance(gw, gwBalance - drops(baseFee)));
            aliceBalance = env.balance(alice, XRP);
            gwBalance = env.balance(gw, XRP);

            // can not send XRP
            env(pay(gw, alice, XRP(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            env.require(Balance(bob, bobBalance));

            // mint 50 USD
            env(pay(gw, alice, usd(50)), delegate::As(bob));
            env.close();
            env.require(Balance(bob, bobBalance - drops(baseFee)));
            env.require(Balance(gw, gwBalance));
            env.require(Balance(gw, alice["USD"](-50)));
            env.require(Balance(alice, usd(50)));
            BEAST_EXPECT(env.balance(bob, usd) == usd(0));
            bobBalance = env.balance(bob, XRP);

            // burn 30 USD
            env(pay(alice, gw, usd(30)), delegate::As(bob));
            env.close();
            env.require(Balance(bob, bobBalance - drops(baseFee)));
            env.require(Balance(gw, gwBalance));
            env.require(Balance(gw, alice["USD"](-20)));
            env.require(Balance(alice, usd(20)));
            BEAST_EXPECT(env.balance(bob, usd) == usd(0));
            bobBalance = env.balance(bob, XRP);

            // bob has both mint and burn permissions
            env(delegate::set(gw, bob, {"PaymentMint", "PaymentBurn"}));
            env.close();
            env.require(Balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // mint 100 USD for gw
            env(pay(gw, alice, usd(100)), delegate::As(bob));
            env.close();
            env.require(Balance(gw, alice["USD"](-120)));
            env.require(Balance(alice, usd(120)));
            env.require(Balance(bob, bobBalance - drops(baseFee)));
            bobBalance = env.balance(bob, XRP);

            // gw2 pays gw 200 EUR
            env(pay(gw2, gw, eur(200)));
            env.close();
            env.require(Balance(gw2, gw2Balance - drops(baseFee)));
            gw2Balance = env.balance(gw2, XRP);
            env.require(Balance(gw2, gw["EUR"](-200)));
            env.require(Balance(gw, eur(200)));

            // burn 100 EUR for gw
            env(pay(gw, gw2, eur(100)), delegate::As(bob));
            env.close();
            env.require(Balance(gw2, gw["EUR"](-100)));
            env.require(Balance(gw, eur(100)));
            env.require(Balance(bob, bobBalance - drops(baseFee)));
            env.require(Balance(gw, gwBalance));
            env.require(Balance(gw2, gw2Balance));
            env.require(Balance(alice, aliceBalance));
        }

        // test PaymentMint won't affect Payment transaction level delegation.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gateway"};
            auto const usd = gw["USD"];

            env.fund(XRP(10000), alice);
            env.fund(XRP(20000), bob);
            env.fund(XRP(40000), gw);
            env.trust(usd(200), alice);
            env.close();

            XRPAmount const baseFee{env.current()->fees().base};

            auto aliceBalance = env.balance(alice, XRP);
            auto bobBalance = env.balance(bob, XRP);
            auto gwBalance = env.balance(gw, XRP);

            // gw gives bob PaymentBurn permission
            env(delegate::set(gw, bob, {"PaymentBurn"}));
            env.close();
            env.require(Balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob can not mint on behalf of gw because he only has burn
            // permission
            env(pay(gw, alice, usd(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            env.require(Balance(bob, bobBalance));

            // gw gives bob Payment permission as well
            env(delegate::set(gw, bob, {"PaymentBurn", "Payment"}));
            env.close();
            env.require(Balance(gw, gwBalance - drops(baseFee)));
            gwBalance = env.balance(gw, XRP);

            // bob now can mint on behalf of gw
            env(pay(gw, alice, usd(50)), delegate::As(bob));
            env.close();
            env.require(Balance(bob, bobBalance - drops(baseFee)));
            env.require(Balance(gw, gwBalance));
            env.require(Balance(alice, aliceBalance));
            env.require(Balance(gw, alice["USD"](-50)));
            env.require(Balance(alice, usd(50)));
            BEAST_EXPECT(env.balance(bob, usd) == usd(0));
        }

        // disallow cross currency payment with only PaymentBurn/PaymentMint
        // permission
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gateway"};
            Account const carol{"carol"};
            auto const usd = gw["USD"];

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(50000), alice);
            env.trust(usd(50000), bob);
            env.trust(usd(50000), carol);
            env(pay(gw, alice, usd(10000)));
            env(pay(gw, bob, usd(10000)));
            env(pay(gw, carol, usd(10000)));
            env.close();

            // PaymentMint
            {
                env(offer(carol, XRP(100), usd(501)));
                BEAST_EXPECT(expectOffers(env, carol, 1));
                env(delegate::set(gw, bob, {"PaymentMint"}));
                env.close();

                // bob can not send cross currency payment on behalf of the gw,
                // even with PaymentMint permission and gw being the issuer.
                env(pay(gw, alice, usd(5000)),
                    Sendmax(XRP(1001)),
                    Txflags(tfPartialPayment),
                    delegate::As(bob),
                    Ter(terNO_DELEGATE_PERMISSION));
                BEAST_EXPECT(expectOffers(env, carol, 1));

                env(pay(gw, alice, usd(5000)),
                    Path(~XRP),
                    Txflags(tfPartialPayment),
                    delegate::As(bob),
                    Ter(terNO_DELEGATE_PERMISSION));
                BEAST_EXPECT(expectOffers(env, carol, 1));

                // succeed with direct payment
                env(pay(gw, alice, usd(100)), delegate::As(bob));
                env.close();
            }

            // PaymentBurn
            {
                env(offer(bob, XRP(100), usd(501)));
                BEAST_EXPECT(expectOffers(env, bob, 1));
                env(delegate::set(alice, bob, {"PaymentBurn"}));
                env.close();

                // bob can not send cross currency payment on behalf of alice,
                // even with PaymentBurn permission and gw being the issuer.
                env(pay(alice, gw, usd(5000)),
                    Sendmax(XRP(1001)),
                    Txflags(tfPartialPayment),
                    delegate::As(bob),
                    Ter(terNO_DELEGATE_PERMISSION));
                BEAST_EXPECT(expectOffers(env, bob, 1));

                env(pay(alice, gw, usd(5000)),
                    Path(~XRP),
                    Txflags(tfPartialPayment),
                    delegate::As(bob),
                    Ter(terNO_DELEGATE_PERMISSION));
                BEAST_EXPECT(expectOffers(env, bob, 1));

                // succeed with direct payment
                env(pay(alice, gw, usd(100)), delegate::As(bob));
                env.close();
            }
        }

        // PaymentMint/PaymentBurn with sfSendMax of the same asset is allowed,
        // same-asset SendMax is still a direct payment, not cross-currency.
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gw"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(200), alice);
            env.close();

            env(delegate::set(gw, bob, {"PaymentMint"}));
            env.close();

            // sfSendMax with same asset as sfAmount, still a direct payment
            env(pay(gw, alice, usd(50)), Sendmax(usd(50)), delegate::As(bob));
            env.require(Balance(alice, usd(50)));

            env(delegate::set(alice, bob, {"PaymentBurn"}));
            env.close();

            env(pay(alice, gw, usd(30)), Sendmax(usd(30)), delegate::As(bob));
            env.require(Balance(alice, usd(20)));
        }

        // Test invalid fields or flags not allowed in granular permission template
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gw"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(200), alice);
            env.close();

            env(delegate::set(gw, bob, {"PaymentMint"}));
            env(delegate::set(alice, bob, {"PaymentBurn"}));
            env.close();

            // sfDeliverMin (with tfPartialPayment) is not in the PaymentMint
            // or PaymentBurn template.
            env(pay(gw, alice, usd(100)),
                DeliverMin(usd(50)),
                Txflags(tfPartialPayment),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(pay(alice, gw, usd(50)),
                DeliverMin(usd(25)),
                Txflags(tfPartialPayment),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));

            // sfDomainID is not in the PaymentMint or PaymentBurn template.
            env(pay(gw, alice, usd(100)),
                Domain(uint256{1}),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(pay(alice, gw, usd(50)),
                Domain(uint256{1}),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
        }

        // Delegate account holds no granular permissions for the tx type:
        // getGranularPermission returns empty set.
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gw"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(200), alice);
            env.close();

            // Bob holds only an AccountSet granular permission.
            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            // Payment has granular permissions defined in permissions.macro,
            // but bob only holds AccountSet's granular permission,
            // getGranularPermission returns empty.
            env(pay(alice, gw, usd(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
        }

        // PaymentMint and PaymentBurn for MPT
        {
            std::string logs;
            Env env(*this, features, std::make_unique<CaptureLogs>(&logs));
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gateway"};

            MPTTester mpt(env, gw, {.holders = {alice, bob}});
            mpt.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});

            mpt.authorize({.account = alice});
            mpt.authorize({.account = bob});

            auto const MPT = mpt["MPT"];  // NOLINT(readability-identifier-naming)
            env(pay(gw, alice, MPT(500)));
            env(pay(gw, bob, MPT(500)));
            env.close();
            auto aliceMPT = env.balance(alice, MPT);
            auto bobMPT = env.balance(bob, MPT);

            // PaymentMint
            {
                env(delegate::set(gw, bob, {"PaymentMint"}));
                env.close();

                env(pay(gw, alice, MPT(50)), delegate::As(bob));
                BEAST_EXPECT(env.balance(alice, MPT) == aliceMPT + MPT(50));
                BEAST_EXPECT(env.balance(bob, MPT) == bobMPT);
                aliceMPT = env.balance(alice, MPT);
            }

            // PaymentBurn
            {
                env(delegate::set(alice, bob, {"PaymentBurn"}));
                env.close();

                env(pay(alice, gw, MPT(50)), delegate::As(bob));
                BEAST_EXPECT(env.balance(alice, MPT) == aliceMPT - MPT(50));
                BEAST_EXPECT(env.balance(bob, MPT) == bobMPT);
                aliceMPT = env.balance(alice, MPT);
            }

            // Grant both granular permissions and tx level permission.
            {
                env(delegate::set(alice, bob, {"PaymentBurn", "PaymentMint", "Payment"}));
                env.close();
                env(pay(alice, gw, MPT(50)), delegate::As(bob));
                BEAST_EXPECT(env.balance(alice, MPT) == aliceMPT - MPT(50));
                BEAST_EXPECT(env.balance(bob, MPT) == bobMPT);
                aliceMPT = env.balance(alice, MPT);
                env(pay(alice, bob, MPT(100)), delegate::As(bob));
                BEAST_EXPECT(env.balance(alice, MPT) == aliceMPT - MPT(100));
                BEAST_EXPECT(env.balance(bob, MPT) == bobMPT + MPT(100));
            }
        }

        // Verify granular permissions of different tx types in the same SLE are scoped
        // correctly. AccountSet permissions don't apply to Payment and vice versa
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gw"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(200), alice);
            env.close();

            // Alice granted bob with both AccountDomainSet and PaymentMint.
            env(delegate::set(alice, bob, {"AccountDomainSet", "PaymentMint"}));
            env.close();

            // PaymentMint fails at granular semantic check because alice is not the issuer.
            env(pay(alice, gw, usd(50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // AccountDomainSet applies correctly to AccountSet
            std::string const domain = "example.com";
            auto jt = noop(alice);
            jt[sfDomain] = strHex(domain);
            jt[sfDelegate] = bob.human();
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // gw gives bob PaymentMint and bob can mint on gw's behalf
            env(delegate::set(gw, bob, {"PaymentMint"}));
            env.close();
            env(pay(gw, alice, usd(50)), delegate::As(bob));
            env.require(Balance(alice, usd(50)));
        }
    }

    void
    testTrustSetGranular()
    {
        testcase("test TrustSet granular permissions");
        using namespace jtx;

        // test TrustlineUnfreeze, TrustlineFreeze and TrustlineAuthorize
        {
            Env env(*this);
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();

            env(delegate::set(alice, bob, {"TrustlineUnfreeze"}));
            env.close();
            // bob can not create trustline on behalf of alice because he only
            // has unfreeze permission
            env(trust(alice, gw["USD"](50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            // alice creates trustline by herself
            env(trust(alice, gw["USD"](50)));
            env.close();

            // gw gives bob unfreeze permission
            env(delegate::set(gw, bob, {"TrustlineUnfreeze"}));
            env.close();

            // unsupported flags
            env(trust(alice, gw["USD"](50), tfSetNoRipple),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(trust(alice, gw["USD"](50), tfClearNoRipple),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(trust(gw, gw["USD"](0), alice, tfSetDeepFreeze),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(trust(gw, gw["USD"](0), alice, tfClearDeepFreeze),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            // supported flags with wrong permission
            env(trust(gw, gw["USD"](0), alice, tfSetfAuth),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env(trust(gw, gw["USD"](0), alice, tfSetFreeze),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            env(delegate::set(gw, bob, {"TrustlineAuthorize"}));
            env.close();
            env(trust(gw, gw["USD"](0), alice, tfClearFreeze),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            // although trustline authorize is granted, bob can not change the
            // limit number
            env(trust(gw, gw["USD"](50), alice, tfSetfAuth),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            // supported flags with correct permission
            env(trust(gw, gw["USD"](0), alice, tfSetfAuth), delegate::As(bob));
            env.close();
            env(delegate::set(gw, bob, {"TrustlineAuthorize", "TrustlineFreeze"}));
            env.close();
            env(trust(gw, gw["USD"](0), alice, tfSetFreeze), delegate::As(bob));
            env.close();
            env(delegate::set(gw, bob, {"TrustlineAuthorize", "TrustlineUnfreeze"}));
            env.close();
            env(trust(gw, gw["USD"](0), alice, tfClearFreeze), delegate::As(bob));
            env.close();
            // but bob can not freeze trustline because he no longer has freeze
            // permission
            env(trust(gw, gw["USD"](0), alice, tfSetFreeze),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));

            // cannot update LimitAmount with granular permission, both high and
            // low account
            env(trust(alice, gw["USD"](100)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env(trust(gw, alice["USD"](100)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // can not set QualityIn or QualityOut
            auto tx = trust(alice, gw["USD"](50));
            tx["QualityIn"] = "1000";
            env(tx, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            auto tx2 = trust(alice, gw["USD"](50));
            tx2["QualityOut"] = "1000";
            env(tx2, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            auto tx3 = trust(gw, alice["USD"](50));
            tx3["QualityIn"] = "1000";
            env(tx3, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            auto tx4 = trust(gw, alice["USD"](50));
            tx4["QualityOut"] = "1000";
            env(tx4, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // granting TrustSet can make it work
            env(delegate::set(gw, bob, {"TrustSet"}));
            env.close();
            auto tx5 = trust(gw, alice["USD"](50));
            tx5["QualityOut"] = "1000";
            env(tx5, delegate::As(bob));
            auto tx6 = trust(alice, gw["USD"](50));
            tx6["QualityOut"] = "1000";
            env(tx6, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env(delegate::set(alice, bob, {"TrustSet"}));
            env.close();
            env(tx6, delegate::As(bob));
        }

        // test mix of transaction level delegation and granular delegation
        {
            Env env(*this);
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();

            // bob does not have permission
            env(trust(alice, gw["USD"](50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env(delegate::set(alice, bob, {"TrustlineUnfreeze", "NFTokenCreateOffer"}));
            env.close();
            // bob still does not have permission
            env(trust(alice, gw["USD"](50)), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // add TrustSet permission and some unrelated permission
            env(delegate::set(
                alice,
                bob,
                {"TrustlineUnfreeze", "NFTokenCreateOffer", "TrustSet", "AccountTransferRateSet"}));
            env.close();
            env(trust(alice, gw["USD"](50)), delegate::As(bob));
            env.close();

            env(delegate::set(
                gw,
                bob,
                {"TrustlineUnfreeze", "NFTokenCreateOffer", "TrustSet", "AccountTransferRateSet"}));
            env.close();

            // since bob has TrustSet permission, he does not need
            // TrustlineFreeze granular permission to freeze the trustline
            env(trust(gw, gw["USD"](0), alice, tfSetFreeze), delegate::As(bob));
            env(trust(gw, gw["USD"](0), alice, tfClearFreeze), delegate::As(bob));
            // bob can perform all the operations regarding TrustSet
            env(trust(gw, gw["USD"](0), alice, tfSetFreeze), delegate::As(bob));
            env(trust(gw, gw["USD"](0), alice, tfSetDeepFreeze), delegate::As(bob));
            env(trust(gw, gw["USD"](0), alice, tfClearDeepFreeze), delegate::As(bob));
            env(trust(gw, gw["USD"](0), alice, tfSetfAuth), delegate::As(bob));
            env(trust(alice, gw["USD"](50), tfSetNoRipple), delegate::As(bob));
            env(trust(alice, gw["USD"](50), tfClearNoRipple), delegate::As(bob));
        }

        // tfFullyCanonicalSig won't block delegated transaction
        {
            Env env(*this);
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();

            env(delegate::set(gw, bob, {"TrustlineAuthorize"}));
            env.close();
            env(trust(gw, gw["USD"](0), alice, tfSetfAuth | tfFullyCanonicalSig),
                delegate::As(bob));
        }

        {
            Env env(*this);
            Account const gw{"gw"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), gw, alice, bob);

            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(alice, gw["USD"](50)));
            env.close();
            env(delegate::set(gw, bob, {"TrustlineAuthorize"}));
            env.close();

            env(trust(gw, gw["USD"](0), alice, tfSetfAuth), delegate::As(bob));
            env.close();

            // sfQualityOut is a valid TrustSet field, but not permitted in granular template
            json::Value txJson = trust(gw, gw["USD"](0), alice, tfSetfAuth);
            txJson[sfQualityOut.jsonName] = 100;
            env(txJson, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // tfSetNoRipple is a valid flag for TrustSet, but not permitted in granular template
            env(trust(gw, gw["USD"](0), alice, tfSetfAuth | tfSetNoRipple),
                delegate::As(bob),
                Ter(terNO_DELEGATE_PERMISSION));
        }
    }

    void
    testAccountSetGranular()
    {
        testcase("test AccountSet granular permissions");
        using namespace jtx;

        // test AccountDomainSet, AccountEmailHashSet,
        // AccountMessageKeySet,AccountTransferRateSet, and AccountTickSizeSet
        // granular permissions
        {
            Env env(*this);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            // alice gives bob some random permission, which is not related to
            // the AccountSet transaction
            env(delegate::set(alice, bob, {"TrustlineUnfreeze"}));
            env.close();

            // bob does not have permission to set domain
            // on behalf of alice
            std::string const domain = "example.com";
            auto jt = noop(alice);
            jt[sfDomain] = strHex(domain);
            jt[sfDelegate] = bob.human();

            // add granular permission related to AccountSet but is not the
            // correct permission for domain set
            env(delegate::set(alice, bob, {"TrustlineUnfreeze", "AccountEmailHashSet"}));
            env.close();
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            // alice give granular permission of AccountDomainSet to bob
            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            // bob set account domain on behalf of alice
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob can reset domain
            jt[sfDomain] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfDomain));

            // bob tries to set unauthorized flag, it will fail
            std::string const failDomain = "fail_domain_update";
            jt[sfFlags] = tfRequireAuth;
            jt[sfDomain] = strHex(failDomain);
            env(jt, Ter(terNO_DELEGATE_PERMISSION));
            // reset flag number
            jt[sfFlags] = 0;

            // bob tries to update domain and set email hash,
            // but he does not have permission to set email hash
            jt[sfDomain] = strHex(domain);
            std::string const mh("5F31A79367DC3137FADA860C05742EE6");
            jt[sfEmailHash] = mh;
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            // alice give granular permission of AccountEmailHashSet to bob
            env(delegate::set(alice, bob, {"AccountDomainSet", "AccountEmailHashSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT(to_string((*env.le(alice))[sfEmailHash]) == mh);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));

            // bob does not have permission to set message key for alice
            auto const rkp = randomKeyPair(KeyType::Ed25519);
            jt[sfMessageKey] = strHex(rkp.first.slice());
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            // alice give granular permission of AccountMessageKeySet to bob
            env(delegate::set(
                alice, bob, {"AccountDomainSet", "AccountEmailHashSet", "AccountMessageKeySet"}));
            env.close();

            // bob can set message key for alice
            env(jt);
            BEAST_EXPECT(strHex((*env.le(alice))[sfMessageKey]) == strHex(rkp.first.slice()));
            jt[sfMessageKey] = "";
            env(jt);
            BEAST_EXPECT(!env.le(alice)->isFieldPresent(sfMessageKey));

            // bob does not have permission to set transfer rate for alice
            env(rate(alice, 2.0), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // alice give granular permission of AccountTransferRateSet to bob
            env(delegate::set(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet"}));
            env.close();
            auto jtRate = rate(alice, 2.0);
            jtRate[sfDelegate] = bob.human();
            env(jtRate, delegate::As(bob));
            BEAST_EXPECT((*env.le(alice))[sfTransferRate] == 2000000000);

            // bob does not have permission to set ticksize for alice
            jt[sfTickSize] = 8;
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            // alice give granular permission of AccountTickSizeSet to bob
            env(delegate::set(
                alice,
                bob,
                {"AccountDomainSet",
                 "AccountEmailHashSet",
                 "AccountMessageKeySet",
                 "AccountTransferRateSet",
                 "AccountTickSizeSet"}));
            env.close();
            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfTickSize] == 8);

            // can not set asfRequireAuth flag for alice
            env(fset(alice, asfRequireAuth), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // reset Delegate will delete the Delegate
            // object
            env(delegate::set(alice, bob, {}));
            // bib still does not have permission to set asfRequireAuth for
            // alice
            env(fset(alice, asfRequireAuth), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            // alice can set for herself
            env(fset(alice, asfRequireAuth));
            env.require(Flags(alice, asfRequireAuth));
            env.close();

            // can not update tick size because bob no longer has permission
            jt[sfTickSize] = 7;
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            env(delegate::set(
                alice, bob, {"AccountDomainSet", "AccountEmailHashSet", "AccountMessageKeySet"}));
            env.close();

            // bob does not have permission to set wallet locater for alice
            std::string const locator =
                "9633EC8AF54F16B5286DB1D7B519EF49EEFC050C0C8AC4384F1D88ACD1BFDF"
                "05";
            auto jv2 = noop(alice);
            jv2[sfDomain] = strHex(domain);
            jv2[sfDelegate] = bob.human();
            jv2[sfWalletLocator] = locator;
            env(jv2, Ter(terNO_DELEGATE_PERMISSION));
        }

        // can not set AccountSet flags on behalf of other account,
        // in permissions.macro, the template for AccountSet does
        // not allow any flag set or clear.
        {
            Env env(*this);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto testSetClearFlag = [&](std::uint32_t flag) {
                // bob can not set flag on behalf of alice
                env(fset(alice, flag), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
                // alice set by herself
                env(fset(alice, flag));
                env.close();
                env.require(Flags(alice, flag));
                // bob can not clear on behalf of alice
                env(fclear(alice, flag), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            };

            // testSetClearFlag(asfNoFreeze);
            testSetClearFlag(asfRequireAuth);
            testSetClearFlag(asfAllowTrustLineClawback);

            // alice gives some granular permissions to bob
            env(delegate::set(
                alice, bob, {"AccountDomainSet", "AccountEmailHashSet", "AccountMessageKeySet"}));
            env.close();

            testSetClearFlag(asfDefaultRipple);
            testSetClearFlag(asfDepositAuth);
            testSetClearFlag(asfDisallowIncomingCheck);
            testSetClearFlag(asfDisallowIncomingNFTokenOffer);
            testSetClearFlag(asfDisallowIncomingPayChan);
            testSetClearFlag(asfDisallowIncomingTrustline);
            testSetClearFlag(asfDisallowXRP);
            testSetClearFlag(asfRequireDest);
            testSetClearFlag(asfGlobalFreeze);

            // bob can not set asfAccountTxnID on behalf of alice
            env(fset(alice, asfAccountTxnID), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env(fset(alice, asfAccountTxnID));
            env.close();
            BEAST_EXPECT(env.le(alice)->isFieldPresent(sfAccountTxnID));
            env(fclear(alice, asfAccountTxnID), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // bob can not set asfAuthorizedNFTokenMinter on behalf of alice
            json::Value jt = fset(alice, asfAuthorizedNFTokenMinter);
            jt[sfDelegate] = bob.human();
            jt[sfNFTokenMinter] = bob.human();
            env(jt, Ter(terNO_DELEGATE_PERMISSION));

            // bob gives alice some permissions
            env(delegate::set(
                bob, alice, {"AccountDomainSet", "AccountEmailHashSet", "AccountMessageKeySet"}));
            env.close();

            // since we can not set asfNoFreeze if asfAllowTrustLineClawback is
            // set, which can not be clear either. Test alice set asfNoFreeze on
            // behalf of bob.
            env(fset(alice, asfNoFreeze), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
            env(fset(bob, asfNoFreeze));
            env.close();
            env.require(Flags(bob, asfNoFreeze));
            // alice can not clear on behalf of bob
            env(fclear(alice, asfNoFreeze), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

            // bob can not set asfDisableMaster on behalf of alice
            Account const bobKey{"bobKey", KeyType::Secp256k1};
            env(regkey(bob, bobKey));
            env.close();
            env(fset(alice, asfDisableMaster),
                delegate::As(bob),
                Sig(bob),
                Ter(terNO_DELEGATE_PERMISSION));
        }

        // tfFullyCanonicalSig won't block delegated transaction
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            env(delegate::set(alice, bob, {"AccountDomainSet", "AccountEmailHashSet"}));
            env.close();

            std::string const domain = "example.com";
            auto jt = noop(alice);
            jt[sfDomain] = strHex(domain);
            jt[sfDelegate] = bob.human();
            jt[sfFlags] = tfFullyCanonicalSig;

            env(jt);
            BEAST_EXPECT((*env.le(alice))[sfDomain] == makeSlice(domain));
        }

        // setting invalid field not in permissions.macro template will be rejected.
        {
            Env env(*this);
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            // Alice gives Bob permission to set her Domain
            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            std::string const domain = "example.com";
            auto txJson = noop(alice);
            txJson[sfDomain] = strHex(domain);
            txJson[sfDelegate] = bob.human();

            // sfNFTokenMinter is a valid field in AccountSet tx, but
            // it is not permitted for granular template
            txJson[sfNFTokenMinter] = bob.human();

            env(txJson, Ter(terNO_DELEGATE_PERMISSION));
        }

        // Delegated AccountSet with no fields and no flags is allowed,
        // because it is allowed in the non-delegated case as well.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            auto jt = noop(alice);
            jt[sfDelegate] = bob.human();
            env(jt);
        }

        // Revoking all permissions deletes the SLE and subsequent attempts are rejected.
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            env(delegate::set(alice, bob, {"AccountDomainSet"}));
            env.close();

            std::string const domain = "example.com";
            auto jt = noop(alice);
            jt[sfDomain] = strHex(domain);
            jt[sfDelegate] = bob.human();
            env(jt);

            // empty DelegateSet deletes the SLE
            env(delegate::set(alice, bob, {}));
            env.close();

            env(jt, Ter(terNO_DELEGATE_PERMISSION));
        }
    }

    void
    testMPTokenIssuanceSetGranular()
    {
        testcase("test MPTokenIssuanceSet granular");
        using namespace jtx;

        // test MPTokenIssuanceUnlock and MPTokenIssuanceLock permissions
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // delegate ledger object is not created yet
            mpt.set(
                {.account = alice,
                 .flags = tfMPTLock,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});

            // alice gives granular permission to bob of MPTokenIssuanceUnlock
            env(delegate::set(alice, bob, {"MPTokenIssuanceUnlock"}));
            env.close();
            // bob does not have lock permission
            mpt.set(
                {.account = alice,
                 .flags = tfMPTLock,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});
            // bob now has lock permission, but does not have unlock permission
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});

            // now bob can lock and unlock
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            env.close();
        }

        // test mix of granular and transaction level permission
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
            // bob does not have unlock permission
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});

            // alice gives bob some unrelated permission with
            // MPTokenIssuanceLock
            env(delegate::set(alice, bob, {"NFTokenMint", "MPTokenIssuanceLock", "NFTokenBurn"}));
            env.close();
            // bob can not unlock
            mpt.set(
                {.account = alice,
                 .flags = tfMPTUnlock,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});

            // alice add MPTokenIssuanceSet to permissions
            env(delegate::set(
                alice,
                bob,
                {"NFTokenMint", "MPTokenIssuanceLock", "NFTokenBurn", "MPTokenIssuanceSet"}));
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            // alice can lock by herself
            mpt.set({.account = alice, .flags = tfMPTLock});
            mpt.set({.account = alice, .flags = tfMPTUnlock, .delegate = bob});
            mpt.set({.account = alice, .flags = tfMPTLock, .delegate = bob});
        }

        // tfFullyCanonicalSig won't block delegated transaction
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), alice, bob);
            env.close();

            MPTTester mpt(env, alice, {.fund = false});
            env.close();
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob of MPTokenIssuanceLock
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();
            mpt.set({.account = alice, .flags = tfMPTLock | tfFullyCanonicalSig, .delegate = bob});
        }

        // field not permitted to exist in granular delegation
        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(100000), alice, bob);

            MPTTester mpt(env, alice, {.fund = false});
            mpt.create({.flags = tfMPTCanLock});
            env.close();

            // alice gives granular permission to bob for MPTokenIssuanceLock
            env(delegate::set(alice, bob, {"MPTokenIssuanceLock"}));
            env.close();

            // Field is not permitted, permitted fields for delegation is defined in
            // permissions.macro.
            mpt.set(
                {.account = alice,
                 .mutableFlags = 2,
                 .delegate = bob,
                 .err = terNO_DELEGATE_PERMISSION});

            // Notice: flags not defined in permissions.macro are not permitted for delegation.
            // Since preflight will check invalid flag for the tx, it is not reachable.
            // If any new flag is defined into the transaction in the future,
            // but is not allowed for delegation, the transaction will be rejected with
            // terNO_DELEGATE_PERMISSION. The set of permitted flags for delegation is defined in
            // permissions.macro.
        }
    }

    void
    testSingleSign()
    {
        testcase("test single sign");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(100000), alice, bob, carol);
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);

        env(pay(alice, carol, XRP(100)), Fee(XRP(10)), delegate::As(bob), Sig(bob));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
        BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
        BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(100));
    }

    void
    testSingleSignBadSecret()
    {
        testcase("test single sign with bad secret");
        using namespace jtx;

        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            env(delegate::set(alice, bob, {"Payment"}));
            env.close();

            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(10)),
                delegate::As(bob),
                Sig(alice),
                Ter(tefBAD_AUTH));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            env(delegate::set(alice, bob, {"TrustSet"}));
            env.close();

            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(10)),
                delegate::As(bob),
                Sig(carol),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(10)),
                delegate::As(bob),
                Sig(alice),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }

        {
            Env env(*this);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(100000), alice, bob, carol);
            env.close();

            auto aliceBalance = env.balance(alice);
            auto bobBalance = env.balance(bob);
            auto carolBalance = env.balance(carol);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(10)),
                delegate::As(bob),
                Sig(alice),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);

            env(pay(alice, carol, XRP(100)),
                Fee(XRP(10)),
                delegate::As(bob),
                Sig(carol),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance);
            BEAST_EXPECT(env.balance(bob) == bobBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance);
        }
    }

    void
    testMultiSign()
    {
        testcase("test multi sign");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const daria{"daria"};
        Account const edward{"edward"};
        env.fund(XRP(100000), alice, bob, carol, daria, edward);
        env.close();

        env(signers(bob, 2, {{daria, 1}, {edward, 1}}));
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);
        auto dariaBalance = env.balance(daria);
        auto edwardBalance = env.balance(edward);

        env(pay(alice, carol, XRP(100)), Fee(XRP(10)), delegate::As(bob), Msig(daria, edward));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(100));
        BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
        BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(100));
        BEAST_EXPECT(env.balance(daria) == dariaBalance);
        BEAST_EXPECT(env.balance(edward) == edwardBalance);
    }

    void
    testMultiSignQuorumNotMet()
    {
        testcase("test multi sign which does not meet quorum");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const daria{"daria"};
        Account const edward{"edward"};
        Account const fred{"fred"};
        env.fund(XRP(100000), alice, bob, carol, daria, edward, fred);
        env.close();

        env(signers(bob, 3, {{daria, 1}, {edward, 1}, {fred, 1}}));
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);
        auto dariaBalance = env.balance(daria);
        auto edwardBalance = env.balance(edward);

        env(pay(alice, carol, XRP(100)),
            Fee(XRP(10)),
            delegate::As(bob),
            Msig(daria, edward),
            Ter(tefBAD_QUORUM));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance);
        BEAST_EXPECT(env.balance(bob) == bobBalance);
        BEAST_EXPECT(env.balance(carol) == carolBalance);
        BEAST_EXPECT(env.balance(daria) == dariaBalance);
        BEAST_EXPECT(env.balance(edward) == edwardBalance);
    }

    void
    testMultiSignDelegatorAsSigner()
    {
        // checkMultiSign disallows the owner of the account to
        // be part of the multisigner list. When it is a delegated transaction,
        // the delegate account should not be part of the multisigner list.
        testcase("test delegator as multisigner in delegate's signer list");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const daria{"daria"};
        env.fund(XRP(100000), alice, bob, carol, daria);
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        // bob's signer list includes the delegator alice and daria
        env(signers(bob, 2, {{alice, 1}, {daria, 1}}));
        env.close();

        auto aliceBalance = env.balance(alice);
        auto bobBalance = env.balance(bob);
        auto carolBalance = env.balance(carol);
        auto const amt = 100;

        // alice can sign as a multisigner for bob
        env(pay(alice, carol, XRP(100)), Fee(XRP(10)), delegate::As(bob), Msig(alice, daria));
        env.close();

        BEAST_EXPECT(env.balance(alice) == aliceBalance - XRP(amt));
        BEAST_EXPECT(env.balance(bob) == bobBalance - XRP(10));
        BEAST_EXPECT(env.balance(carol) == carolBalance + XRP(amt));

        // alice can not sign as a multisigner if she sent the transaction by herself.
        env(pay(alice, carol, XRP(100)), Fee(XRP(10)), Msig(alice, daria), Ter(telENV_RPC_FAILED));
        env.close();

        // Get new balances
        aliceBalance = env.balance(alice);
        bobBalance = env.balance(bob);
        carolBalance = env.balance(carol);

        // bob (the delegate) should not appear as a multisigner in his transaction sent on behalf
        // of alice. STTx::checkMultiSign catches this at the local-check stage, so the jtx
        // framework returns telENV_RPC_FAILED.
        env(pay(alice, carol, XRP(50)),
            Fee(XRP(10)),
            delegate::As(bob),
            Msig(alice, bob),
            Ter(telENV_RPC_FAILED));
        env.close();
        BEAST_EXPECT(env.balance(alice) == aliceBalance);
        BEAST_EXPECT(env.balance(bob) == bobBalance);
        BEAST_EXPECT(env.balance(carol) == carolBalance);
    }

    void
    testSignForDelegated()
    {
        // In sortAndValidateSigners, if it is a delegated transaction, the delegate account is
        // the forbidden account from appearing in its own Signers array.
        testcase("test sign_for with delegated transaction");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const daria{"daria"};
        env.fund(XRP(100000), alice, bob, carol, daria);
        env.close();

        env(delegate::set(alice, bob, {"Payment"}));
        env.close();

        // bob's signer list includes the delegator alice and daria
        env(signers(bob, 2, {{alice, 1}, {daria, 1}}));
        env.close();

        auto const baseFee = env.current()->fees().base;

        auto const sendAmt = 1'000'000;
        auto makeDelegateTx = [&]() -> json::Value {
            json::Value jv;
            jv[jss::tx_json][jss::Account] = alice.human();
            jv[jss::tx_json][sfDelegate.jsonName] = bob.human();
            jv[jss::tx_json][jss::TransactionType] = jss::Payment;
            jv[jss::tx_json][jss::Destination] = carol.human();
            jv[jss::tx_json][jss::Amount] = sendAmt;
            jv[jss::tx_json][jss::Fee] = std::to_string((10 * baseFee).drops());
            jv[jss::tx_json][jss::Sequence] = env.seq(alice);
            jv[jss::tx_json][jss::SigningPubKey] = "";
            return jv;
        };

        // The delegator alice and daria both sign via sign_for, which is valid
        {
            auto const aliceBalance = env.balance(alice);
            auto const bobBalance = env.balance(bob);
            auto const dariaBalance = env.balance(daria);
            auto const carolBalance = env.balance(carol);

            json::Value jv = makeDelegateTx();
            jv[jss::account] = alice.human();
            jv[jss::secret] = alice.name();
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jv2;
            jv2[jss::tx_json] = jrr[jss::tx_json];
            jv2[jss::account] = daria.human();
            jv2[jss::secret] = daria.name();
            jrr = env.rpc("json", "sign_for", to_string(jv2))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");

            json::Value jvSubmit;
            jvSubmit[jss::tx_json] = jrr[jss::tx_json];
            jrr = env.rpc("json", "submit_multisigned", to_string(jvSubmit))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "success");
            env.close();
            BEAST_EXPECT(env.balance(alice) == aliceBalance - XRPAmount(sendAmt));
            BEAST_EXPECT(env.balance(bob) == bobBalance - (10 * baseFee));
            BEAST_EXPECT(env.balance(daria) == dariaBalance);
            BEAST_EXPECT(env.balance(carol) == carolBalance + XRPAmount(sendAmt));
        }

        // The delegated account bob attempts sign_for, will be rejected.
        {
            json::Value jv = makeDelegateTx();
            jv[jss::account] = bob.human();
            jv[jss::secret] = bob.name();
            auto jrr = env.rpc("json", "sign_for", to_string(jv))[jss::result];
            BEAST_EXPECT(jrr[jss::status] == "error");
            BEAST_EXPECT(
                jrr[jss::error_message].asString().contains(
                    "A Signer may not be the transaction's Account"));
        }
    }

    void
    testPermissionValue(FeatureBitset features)
    {
        testcase("test permission value");
        using namespace jtx;

        Env env(*this, features);

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(100000), alice, bob);
        env.close();

        auto buildRequest = [&](auto value) -> json::Value {
            json::Value jv;
            jv[jss::TransactionType] = jss::DelegateSet;
            jv[jss::Account] = alice.human();
            jv[sfAuthorize.jsonName] = bob.human();

            json::Value permissionsJson(json::ValueType::Array);
            json::Value permissionValue;
            permissionValue[sfPermissionValue.jsonName] = value;
            json::Value permissionObj;
            permissionObj[sfPermission.jsonName] = permissionValue;
            permissionsJson.append(permissionObj);
            jv[sfPermissions.jsonName] = permissionsJson;

            return jv;
        };

        // invalid permission value.
        // neither granular permission nor transaction level permission
        for (auto value : {0, 100000, 54321})
        {
            auto jv = buildRequest(value);
            env(jv, Ter(temMALFORMED));
        }
    }

    void
    testTxRequireFeatures(FeatureBitset features)
    {
        testcase("test delegate disabled tx");
        using namespace jtx;

        // map of tx and required feature.
        // non-delegable tx are not included.
        // NFTokenMint, NFTokenBurn, NFTokenCreateOffer, NFTokenCancelOffer,
        // NFTokenAcceptOffer are not included, they are tested separately.
        std::unordered_map<std::string, uint256> txRequiredFeatures{
            {"Clawback", featureClawback},
            {"AMMClawback", featureAMMClawback},
            {"AMMCreate", featureAMM},
            {"AMMDeposit", featureAMM},
            {"AMMWithdraw", featureAMM},
            {"AMMVote", featureAMM},
            {"AMMBid", featureAMM},
            {"AMMDelete", featureAMM},
            {"XChainCreateClaimID", featureXChainBridge},
            {"XChainCommit", featureXChainBridge},
            {"XChainClaim", featureXChainBridge},
            {"XChainAccountCreateCommit", featureXChainBridge},
            {"XChainAddClaimAttestation", featureXChainBridge},
            {"XChainAddAccountCreateAttestation", featureXChainBridge},
            {"XChainModifyBridge", featureXChainBridge},
            {"XChainCreateBridge", featureXChainBridge},
            {"DIDSet", featureDID},
            {"DIDDelete", featureDID},
            {"OracleSet", featurePriceOracle},
            {"OracleDelete", featurePriceOracle},
            {"LedgerStateFix", fixNFTokenPageLinks},
            {"MPTokenIssuanceCreate", featureMPTokensV1},
            {"MPTokenIssuanceDestroy", featureMPTokensV1},
            {"MPTokenIssuanceSet", featureMPTokensV1},
            {"MPTokenAuthorize", featureMPTokensV1},
            {"CredentialCreate", featureCredentials},
            {"CredentialAccept", featureCredentials},
            {"CredentialDelete", featureCredentials},
            {"NFTokenModify", featureDynamicNFT},
            {"PermissionedDomainSet", featurePermissionedDomains},
            {"PermissionedDomainDelete", featurePermissionedDomains}};

        // Can not delegate tx if any required feature disabled.
        {
            auto txAmendmentDisabled = [&](FeatureBitset features, std::string const& tx) {
                BEAST_EXPECT(txRequiredFeatures.contains(tx));

                Env env(*this, features - txRequiredFeatures[tx]);

                Account const alice{"alice"};
                Account const bob{"bob"};
                env.fund(XRP(100000), alice, bob);
                env.close();

                env(delegate::set(alice, bob, {tx}), Ter(temMALFORMED));
            };

            for (auto const& tx : txRequiredFeatures)
                txAmendmentDisabled(features, tx.first);
        }

        // if all the required features in txRequiredFeatures are enabled, will
        // succeed
        {
            auto txAmendmentEnabled = [&](std::string const& tx) {
                Env env(*this, features);

                Account const alice{"alice"};
                Account const bob{"bob"};
                env.fund(XRP(100000), alice, bob);
                env.close();

                env(delegate::set(alice, bob, {tx}));
            };

            for (auto const& tx : txRequiredFeatures)
                txAmendmentEnabled(tx.first);
        }

        // Granular permissions also require the amendment for their underlying
        // transaction type.
        {
            for (auto const permission : {"MPTokenIssuanceLock", "MPTokenIssuanceUnlock"})
            {
                Env env(*this, features - featureMPTokensV1);

                Account const alice{"alice"};
                Account const bob{"bob"};
                env.fund(XRP(100000), alice, bob);
                env.close();

                env(delegate::set(alice, bob, {permission}), Ter(temMALFORMED));
            }
        }
    }

    void
    testGranularSandboxCheckOrder()
    {
        testcase("Make sure GranularSandbox is checked after transaction-level permission");

        using namespace jtx;

        Env env(*this);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), gw, alice, bob);

        env(fset(gw, asfRequireAuth));
        env.close();
        env(trust(alice, gw["USD"](50)));
        env.close();
        env(delegate::set(gw, bob, {"TrustlineAuthorize"}));
        env.close();

        env(trust(gw, gw["USD"](0), alice, tfSetfAuth), delegate::As(bob));
        env.close();

        // sfQualityOut is a valid TrustSet field, but not permitted in granular template
        json::Value txJson = trust(gw, gw["USD"](0), alice, tfSetfAuth);
        txJson[sfQualityOut.jsonName] = 100;
        env(txJson, delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));

        // Now Alice grants Bob with transaction level permission
        env(delegate::set(gw, bob, {"TrustlineAuthorize", "TrustSet"}));
        env.close();

        // NOTE: This case is to ensure that if a delegate possesses a
        // transaction-level permission (e.g., TrustSet), the granular sandbox must not incorrectly
        // block the transaction. The function checkGranularSandbox MUST be called after the
        // transaction-level permission check. This test case is to avoid future refactor mistakes,
        // modifying the order will fail here.
        env(txJson, delegate::As(bob));
    }

    void
    testTxDelegableCount()
    {
        testcase("Delegable Transactions Completeness");

        std::size_t delegableCount = 0;

#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define TRANSACTION(tag, value, name, txDelegable, ...) \
    if (txDelegable == xrpl::Delegable)                 \
    {                                                   \
        delegableCount++;                               \
    }

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")

        // ====================================================================
        // IMPORTANT NOTICE:
        //
        // If this test fails, it indicates that the 'Delegation::delegable' status
        // in transactions.macro has been changed. Delegation allows accounts to act
        // on behalf of others, significantly increasing the security surface.
        //
        //
        // To ENSURE any added transaction is safe and compatible with delegation:
        //
        // 1. Verify that the transaction is intended to be delegable.
        // 2. Every standard test case for that transaction MUST be
        //    duplicated and verified for a Delegated context.
        // 3. Ensure that Fee, Reserve, and Signing are correctly handled.
        //
        // DO NOT modify expectedDelegableCount unless all scenarios, including
        // edge cases, have been fully tested and verified.
        // ====================================================================
        std::size_t const expectedDelegableCount = 51;

        BEAST_EXPECTS(
            delegableCount == expectedDelegableCount,
            "\n[SECURITY] New delegable transaction detected!"
            "\n  Expected: " +
                std::to_string(expectedDelegableCount) +
                "\n  Actual:   " + std::to_string(delegableCount) +
                "\n  Action: Verify security requirements to interact with Delegation feature");
    }

    void
    testNonDelegableTxWithDelegate(FeatureBitset features)
    {
        testcase("non-delegable tx with sfDelegate is rejected at preflight");
        using namespace jtx;

        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        // Transactions that are notDelegable and have no granular permissions
        // will be rejected with temINVALID at preflight.
        // Note: pseudo-transactions (EnableAmendment, SetFee and UNLModify) are also
        // notDelegable but are excluded here — passesLocalChecks() blocks them
        // before preflight1 is ever reached.
        {
            // SetRegularKey, SignerListSet, AccountDelete, DelegateSet.
            env(regkey(alice, bob), delegate::As(bob), Ter(temINVALID));
            env(signers(alice, 1, {{bob, 1}}), delegate::As(bob), Ter(temINVALID));
            env(acctdelete(alice, bob), delegate::As(bob), Ter(temINVALID));
            env(delegate::set(alice, bob, {"Payment"}), delegate::As(bob), Ter(temINVALID));

            // SAV transactions.
            {
                Vault const vault{env};
                auto [createTx, keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
                env(createTx, delegate::As(bob), Ter(temINVALID));

                env(vault.set({.owner = alice, .id = keylet.key}),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(vault.del({.owner = alice, .id = keylet.key}),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(1)}),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(1)}),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(vault.clawback({.issuer = alice, .id = keylet.key, .holder = bob}),
                    delegate::As(bob),
                    Ter(temINVALID));
            }

            // Batch transaction: the outer Batch itself is non-delegable.
            {
                auto const seq = env.seq(alice);
                auto const batchFee = batch::calcBatchFee(env, 0, 1);
                env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                    batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                    delegate::As(bob),
                    Ter(temINVALID));
            }

            // Lending protocol transactions
            {
                Vault const vault{env};
                auto [createTx, keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
                env(createTx);

                env(loanBroker::set(alice, keylet.key), delegate::As(bob), Ter(temINVALID));
                env(loanBroker::del(alice, keylet.key), delegate::As(bob), Ter(temINVALID));
                env(loanBroker::coverDeposit(alice, keylet.key, XRP(1)),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(loanBroker::coverWithdraw(alice, keylet.key, XRP(1)),
                    delegate::As(bob),
                    Ter(temINVALID));
                env(loanBroker::coverClawback(alice), delegate::As(bob), Ter(temINVALID));

                env(loan::set(alice, keylet.key, Number(100)), delegate::As(bob), Ter(temINVALID));
                env(loan::manage(alice, keylet.key, 0), delegate::As(bob), Ter(temINVALID));
                env(loan::del(alice, keylet.key), delegate::As(bob), Ter(temINVALID));
                env(loan::pay(alice, keylet.key, XRP(1)), delegate::As(bob), Ter(temINVALID));
            }
        }

        // AccountSet is notDelegable at tx level but has granular permissions,
        // so sfDelegate passes preflight and is rejected at invokeCheckPermission with
        // terNO_DELEGATE_PERMISSION.
        {
            env(fset(alice, asfDefaultRipple), delegate::As(bob), Ter(terNO_DELEGATE_PERMISSION));
        }
    }

    void
    testDelegateUtilsNullptrCheck()
    {
        testcase("DelegateUtils nullptr check");

        // checkTxPermission nullptr check
        STTx const tx{ttPAYMENT, [](STObject&) {}};
        BEAST_EXPECT(checkTxPermission(nullptr, tx) == terNO_DELEGATE_PERMISSION);

        // getGranularPermission nullptr check
        auto const granularPermissions = getGranularPermission(nullptr, ttPAYMENT);
        BEAST_EXPECT(granularPermissions.empty());
    }

    void
    run() override
    {
        FeatureBitset const all = jtx::testableAmendments();

        testFeatureDisabled(all - featurePermissionDelegationV1_1);
        testFeatureDisabled(all);
        testDelegateSet();
        testInvalidRequest(all);
        testReserve();
        testFee();
        testSequence();
        testAccountDelete();
        testDelegateTransaction();
        testPaymentGranular(all);
        testTrustSetGranular();
        testAccountSetGranular();
        testMPTokenIssuanceSetGranular();
        testSingleSign();
        testSingleSignBadSecret();
        testMultiSign();
        testMultiSignQuorumNotMet();
        testMultiSignDelegatorAsSigner();
        testSignForDelegated();
        testPermissionValue(all);
        testTxRequireFeatures(all);
        testGranularSandboxCheckOrder();
        testTxDelegableCount();
        testNonDelegableTxWithDelegate(all);
        testDelegateUtilsNullptrCheck();
    }
};
BEAST_DEFINE_TESTSUITE(Delegate, app, xrpl);
}  // namespace xrpl::test
