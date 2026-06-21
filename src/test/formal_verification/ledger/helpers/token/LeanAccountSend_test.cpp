#include <test/formal_verification/ffi/ledger/helpers/TokenHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/rate.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <vector>

namespace xrpl::test {

using namespace formal_verification;

class LeanAccountSend_test : public LedgerSuite
{
    void
    runAccountSend(
        Sandbox& sb,
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        TER expected,
        char const* label,
        WaiveTransferFee waive = WaiveTransferFee::No,
        AllowMPTOverflow overflow = AllowMPTOverflow::No)
    {
        Asset const asset = amount.asset();
        auto const aFromPre = holdingSTAmount(sb, from, asset);
        auto const aToPre = holdingSTAmount(sb, to, asset);
        auto const mFromPre = holdingMPTAmount(sb, from, asset);
        auto const mToPre = holdingMPTAmount(sb, to, asset);
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = accountSend(
                sb,
                from,
                to,
                amount,
                beast::Journal{beast::Journal::getNullSink()},
                waive,
                overflow);
            LeanTerResult const leanRes =
                formal_verification::accountSend(ledger, from, to, amount, waive, overflow);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
        if (from != to)
        {
            expectHoldingDir(aFromPre, holdingSTAmount(sb, from, asset), true, label);
            expectHoldingDir(aToPre, holdingSTAmount(sb, to, asset), false, label);
            expectHoldingDir(mFromPre, holdingMPTAmount(sb, from, asset), true, label);
            expectHoldingDir(mToPre, holdingMPTAmount(sb, to, asset), false, label);
        }
        expectNoNegativeNativeBalance(sb, label);
    }

    void
    runAccountSendThrows(
        Sandbox& sb,
        AccountID const& from,
        AccountID const& to,
        STAmount const& amount,
        char const* label,
        WaiveTransferFee waive = WaiveTransferFee::No,
        AllowMPTOverflow overflow = AllowMPTOverflow::No)
    {
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            bool cppThrew = false;
            std::string cppError;
            try
            {
                [[maybe_unused]] auto const ter = accountSend(
                    sb,
                    from,
                    to,
                    amount,
                    beast::Journal{beast::Journal::getNullSink()},
                    waive,
                    overflow);
            }
            catch (std::exception const& e)
            {
                cppThrew = true;
                cppError = e.what();
            }
            LeanTerResult const leanRes =
                formal_verification::accountSend(ledger, from, to, amount, waive, overflow);
            BEAST_EXPECT(cppThrew);
            BEAST_EXPECT(leanRes.threw);
            BEAST_EXPECTS(
                !cppError.empty() && cppError == leanRes.error,
                "cpp=[" + cppError + "] lean=[" + leanRes.error + "]");
        });
    }

    // accountSend(from, to, amount), then assert the `from` ownerCount delta
    void
    runAccountSendOwnerDelta(
        Sandbox& sb,
        jtx::Account const& from,
        jtx::Account const& to,
        STAmount const& amount,
        int ownerDelta,
        char const* label)
    {
        auto const acct = keylet::account(from.id());
        int const before = static_cast<int>(sb.read(acct)->getFieldU32(sfOwnerCount));
        runAccountSend(sb, from.id(), to.id(), amount, tesSUCCESS, label);
        BEAST_EXPECT(
            static_cast<int>(sb.read(acct)->getFieldU32(sfOwnerCount)) - before == ownerDelta);
    }

    void
    testAccountSendXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(10'000), alice, bob);
        env.close();

        // not sending: zero amount is a no-op success
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), bob.id(), XRP(0), tesSUCCESS, "accountSend.xrp_zero");
        }
        // sender == receiver: no-op success
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), alice.id(), XRP(50), tesSUCCESS, "accountSend.xrp_self");
        }
        // both accounts kZero: the equal-account guard returns success before any peek
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, xrpAccount(), xrpAccount(), XRP(50), tesSUCCESS, "accountSend.xrp_both_null");
        }
        // normal send: debit sender, credit receiver
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), bob.id(), XRP(100), tesSUCCESS, "accountSend.xrp_send");
        }
        // null sender (kZero): credit receiver only, pathfinding style
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, xrpAccount(), bob.id(), XRP(100), tesSUCCESS, "accountSend.xrp_null_sender");
        }
        // null receiver (kZero): debit sender only
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb,
                alice.id(),
                xrpAccount(),
                XRP(100),
                tesSUCCESS,
                "accountSend.xrp_null_receiver");
        }
        // both accounts non-kZero but unfunded: peek misses both, no debit or credit
        {
            Account const ghostA("ghostA");
            Account const ghostB("ghostB");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, ghostA.id(), ghostB.id(), XRP(100), tesSUCCESS, "accountSend.xrp_both_missing");
        }
        // sender balance < amount on a CLOSED view
        {
            Sandbox sb(env.closed().get(), TapNone);
            runAccountSend(
                sb,
                alice.id(),
                bob.id(),
                XRP(1'000'000),
                tecFAILED_PROCESSING,
                "accountSend.xrp_insufficient");
        }
        // boundary: credit brings the receiver to exactly the native cap kMaxNativeN
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance, STAmount{XRPAmount{std::int64_t(STAmount::kMaxNativeN) - 1'000'000}});
            sb.update(sle);
            runAccountSend(
                sb, xrpAccount(), bob.id(), XRP(1), tesSUCCESS, "accountSend.xrp_max_balance");
        }
        // beyond the cap: both sides store the same out-of-range int64 (neither caps)
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance, STAmount{XRPAmount{std::int64_t(STAmount::kMaxNativeN)}});
            sb.update(sle);
            runAccountSend(
                sb, xrpAccount(), bob.id(), XRP(1), tesSUCCESS, "accountSend.xrp_overflow");
        }
    }

    // Known-failing balance/holding-invariant flag: the unguarded native int64 overflow
    // wraps INT64_MAX+1 to INT64_MIN (both C++ and the model agree)
    void
    testAccountSendXRPHoldingInvariant()
    {
        using namespace jtx;
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(10'000), bob);
        env.close();

        // receiver at INT64_MAX, credit 1 drop: the native int64 add wraps to INT64_MIN
        // and both sides set() the same -2^63 drops - UB
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance,
                STAmount{std::uint64_t(std::numeric_limits<std::int64_t>::max()), false});
            sb.update(sle);
            runAccountSend(
                sb,
                xrpAccount(),
                bob.id(),
                STAmount{XRPAmount{1}},
                tesSUCCESS,
                "accountSend.xrp_int64min");
        }
    }

    void
    testAccountSendIOU()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(10'000), gw, alice, bob);
        env.close();
        env.trust(gw["USD"](1'000'000'000), alice, bob);
        env.close();
        env(pay(gw, alice, gw["USD"](1000)));
        env(pay(gw, bob, gw["USD"](1000)));
        env(rate(gw, 1.25));
        env.close();
        auto const USD = gw["USD"];

        // negative amount
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb,
                gw.id(),
                alice.id(),
                STAmount{USD.issue(), std::uint64_t{5}, 0, true},
                tecINTERNAL,
                "accountSend.iou_negative");
        }
        // zero amount: no-op success
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), bob.id(), USD(0), tesSUCCESS, "accountSend.iou_zero");
        }
        // sender == receiver: no-op success
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), alice.id(), USD(10), tesSUCCESS, "accountSend.iou_self");
        }
        // direct send: issuer -> holder (issuing own IOUs)
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, gw.id(), alice.id(), USD(100), tesSUCCESS, "accountSend.iou_issue");
        }
        // direct send: holder -> issuer (redeeming IOUs)
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(sb, alice.id(), gw.id(), USD(100), tesSUCCESS, "accountSend.iou_redeem");
        }
        // directSendNoFeeIOU: redeeming the full zero-limit balance clears the reserve and
        // deletes the line.
        {
            std::vector<Account> trio{Account("d1"), Account("d2"), Account("d3")};
            std::sort(trio.begin(), trio.end(), [](Account const& a, Account const& b) {
                return a.id() < b.id();
            });
            Account const iss = trio[1];
            Env env2(*this);
            env2.fund(XRP(10'000), trio[0], iss, trio[2]);
            env2.close();
            auto const ISS = iss["USD"];
            for (auto const& holder : {trio[0], trio[2]})
            {
                Sandbox sb(&*env2.current(), TapNone);
                BEAST_EXPECT(
                    accountSend(
                        sb,
                        iss.id(),
                        holder.id(),
                        ISS(100),
                        beast::Journal{beast::Journal::getNullSink()}) == tesSUCCESS);
                runAccountSend(
                    sb,
                    holder.id(),
                    iss.id(),
                    ISS(100),
                    tesSUCCESS,
                    holder.id() < iss.id() ? "accountSend.iou_redeem_delete_lo"
                                           : "accountSend.iou_redeem_delete_hi");
                // self-validate the delete branch actually fired
                BEAST_EXPECT(!sb.read(keylet::line(holder.id(), iss.id(), ISS.issue().currency)));
            }
        }
        // directSendNoFeeIOU with bSenderHigh true (sender id > receiver id): the two
        // saBalance.negate() fire. Partial redeem keeps the line (non-delete update).
        {
            std::vector<Account> pair{Account("sh1"), Account("sh2")};
            std::sort(pair.begin(), pair.end(), [](Account const& a, Account const& b) {
                return a.id() < b.id();
            });
            Account const iss = pair[0];     // lower id
            Account const holder = pair[1];  // higher id -> bSenderHigh on redeem
            Env env2(*this);
            env2.fund(XRP(10'000), iss, holder);
            env2.close();
            env2.trust(iss["USD"](1'000'000), holder);
            env2.close();
            env2(pay(iss, holder, iss["USD"](1000)));
            env2.close();
            Sandbox sb(&*env2.current(), TapNone);
            runAccountSend(
                sb,
                holder.id(),
                iss.id(),
                iss["USD"](100),
                tesSUCCESS,
                "accountSend.iou_sender_high");
        }
        // fee waived, no transfer-rate charge
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb,
                alice.id(),
                bob.id(),
                USD(100),
                tesSUCCESS,
                "accountSend.iou_transit_waived",
                WaiveTransferFee::Yes);
        }
        // rate 1.25 applied, sender pays 125 to deliver 100
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, alice.id(), bob.id(), USD(100), tesSUCCESS, "accountSend.iou_transit_fee");
        }
        // send to a holder with no line: directSendNoFeeIOU creates the trust line
        {
            Account const carol("carol");
            env.fund(XRP(10'000), carol);
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, gw.id(), carol.id(), USD(100), tesSUCCESS, "accountSend.iou_create_line");
        }
        // line absent and receiver account absent: directSendNoFeeIOU -> tefINTERNAL
        {
            Account const ghost("ghostC");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb, gw.id(), ghost.id(), USD(100), tefINTERNAL, "accountSend.iou_no_account");
        }
        // extreme: issue near kMaxValue, the balance add renormalizes (rounding)
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSend(
                sb,
                gw.id(),
                alice.id(),
                USD(9'999'999'999'999'999LL),
                tesSUCCESS,
                "accountSend.iou_large");
        }
        // extreme: balance -= amount overflows the exponent past kMaxOffset
        {
            Sandbox sb(&*env.current(), TapNone);
            auto line = sb.peek(keylet::line(alice.id(), gw.id(), USD.issue().currency));
            bool const aliceHigh = alice.id() > gw.id();
            STAmount big{
                line->getFieldAmount(sfBalance).get<Issue>(),
                STAmount::kMaxValue,
                STAmount::kMaxOffset};
            if (aliceHigh)
                big.negate();  // alice's balance = +kMaxValue at the max offset
            line->setFieldAmount(sfBalance, big);
            sb.update(line);
            runAccountSendThrows(
                sb,
                gw.id(),
                alice.id(),
                STAmount{USD.issue(), STAmount::kMaxValue, STAmount::kMaxOffset},
                "accountSend.iou_overflow");
        }
    }

    void
    testAccountSendMPT()
    {
        using namespace jtx;
        auto const mkAmt = [](Asset const& a, std::uint64_t v) {
            return STAmount{a.get<MPTIssue>(), v};
        };

        {
            Account const bob("bob");
            Account const carol("carol");
            Account const dan("dan");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), bob, 1000);
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const issuer = mpt.issuer().id();
            auto const mptID = mptAsset.get<MPTIssue>().getMptID();

            // zero amount: no-op success
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 0),
                    tesSUCCESS,
                    "accountSend.mpt_zero");
            }
            // sender == receiver: no-op success
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    bob.id(),
                    mkAmt(mptAsset, 10),
                    tesSUCCESS,
                    "accountSend.mpt_self");
            }
            // issuance absent: tecOBJECT_NOT_FOUND
            {
                Sandbox sb(&*env.current(), TapNone);
                MPTIssue const absent{makeMptID(99, issuer)};
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    STAmount{absent, std::uint64_t{10}},
                    tecOBJECT_NOT_FOUND,
                    "accountSend.mpt_absent");
            }
            // direct: issuer -> holder, OutstandingAmount += amt
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    issuer,
                    carol.id(),
                    mkAmt(mptAsset, 100),
                    tesSUCCESS,
                    "accountSend.mpt_issue");
            }
            // direct issue, receiver balance at UINT64_MAX: the credit add overflows
            {
                Sandbox sb(&*env.current(), TapNone);
                auto tok = sb.peek(keylet::mptoken(mptID, carol.id()));
                tok->setFieldU64(sfMPTAmount, std::numeric_limits<std::uint64_t>::max());
                sb.update(tok);
                runAccountSend(
                    sb,
                    issuer,
                    carol.id(),
                    mkAmt(mptAsset, 1),
                    tecINTERNAL,
                    "accountSend.mpt_receiver_overflow");
            }
            // direct: holder -> issuer, OutstandingAmount -= amt
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    issuer,
                    mkAmt(mptAsset, 100),
                    tesSUCCESS,
                    "accountSend.mpt_redeem");
            }
            // direct redeem, the sender holds no MPToken -> tecNO_AUTH
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    dan.id(),
                    issuer,
                    mkAmt(mptAsset, 10),
                    tecNO_AUTH,
                    "accountSend.mpt_sender_no_auth");
            }
            // direct redeem with OutstandingAmount staged below the redeem amount
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mptID));
                iss->setFieldU64(sfOutstandingAmount, 50);
                sb.update(iss);
                runAccountSend(
                    sb,
                    bob.id(),
                    issuer,
                    mkAmt(mptAsset, 100),
                    tecINTERNAL,
                    "accountSend.mpt_redeem_outstanding_underflow");
            }
            // transit: holder -> holder, fee waived
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 100),
                    tesSUCCESS,
                    "accountSend.mpt_transit_waived",
                    WaiveTransferFee::Yes);
            }
            // transit issuer leg overflows OutstandingAmount (staged UINT64_MAX) -> tecPATH_DRY
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mptID));
                iss->setFieldU64(sfOutstandingAmount, std::numeric_limits<std::uint64_t>::max());
                sb.update(iss);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 1),
                    tecPATH_DRY,
                    "accountSend.mpt_transit_outstanding_overflow");
            }
            // transit issuer leg to a receiver with no MPToken -> tecNO_AUTH
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    dan.id(),
                    mkAmt(mptAsset, 10),
                    tecNO_AUTH,
                    "accountSend.mpt_no_auth");
            }
            // transit sender leg: sender balance < amt (partial mutation before fail)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 100'000),
                    tecINSUFFICIENT_FUNDS,
                    "accountSend.mpt_insufficient");
            }
        }
        // transit with a transfer fee: rate 1.2 charged to the sender
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .transferFee = 20000,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), bob, 1000);
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            // fee charged: sender pays 120 to deliver 100
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 100),
                    tesSUCCESS,
                    "accountSend.mpt_transit_fee");
            }
            // same token, fee waived: sender pays only 100 (discriminates the waive flag)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    bob.id(),
                    carol.id(),
                    mkAmt(mptAsset, 100),
                    tesSUCCESS,
                    "accountSend.mpt_transit_fee_waived",
                    WaiveTransferFee::Yes);
            }
        }
        // issuing overflow vs MaximumAmount: tecPATH_DRY, allowOverflow bypasses
        {
            Account const bob("bob");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob},
                    .create = MPTCreate{
                        .maxAmt = 1000,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), bob, 1000);
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const issuer = mpt.issuer().id();

            // isMPTOverflow first condition: sendAmount (2000) > maximumAmount (1000)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    mkAmt(mptAsset, 2000),
                    tecPATH_DRY,
                    "accountSend.mpt_overflow_sendamount");
            }
            // isMPTOverflow second condition: sendAmount ok but outstanding (1000) >
            // maximumAmount - sendAmount (999)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    mkAmt(mptAsset, 1),
                    tecPATH_DRY,
                    "accountSend.mpt_overflow_outstanding");
            }
            // allowOverflow=Yes relaxes the second condition: outstanding may exceed
            // maximumAmount (1000 -> 1001), so the send succeeds
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    mkAmt(mptAsset, 1),
                    tesSUCCESS,
                    "accountSend.mpt_overflow_allowed",
                    WaiveTransferFee::No,
                    AllowMPTOverflow::Yes);
            }
            // allowOverflow=Yes does NOT relax the first condition: a single send above
            // maximumAmount (2000 > 1000) still returns tecPATH_DRY
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    mkAmt(mptAsset, 2000),
                    tecPATH_DRY,
                    "accountSend.mpt_overflow_allowed_sendamount",
                    WaiveTransferFee::No,
                    AllowMPTOverflow::Yes);
            }
            // allowOverflow=Yes still rejects a true uint64 OutstandingAmount overflow
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mptAsset.get<MPTIssue>().getMptID()));
                iss->setFieldU64(sfOutstandingAmount, std::numeric_limits<std::uint64_t>::max());
                sb.update(iss);
                runAccountSend(
                    sb,
                    issuer,
                    bob.id(),
                    mkAmt(mptAsset, 1),
                    tecPATH_DRY,
                    "accountSend.mpt_overflow_allowed_uint64",
                    WaiveTransferFee::No,
                    AllowMPTOverflow::Yes);
            }
        }
    }

    // directSendNoFeeIOU reserve-clear if: one case per conjunct, each making only that
    // conjunct false, verified by the holder ownerCount NOT dropping (if not entered).
    void
    testAccountSendIOUReserveClear()
    {
        using namespace jtx;
        std::vector<Account> pair{Account("rc1"), Account("rc2")};
        std::sort(pair.begin(), pair.end(), [](Account const& a, Account const& b) {
            return a.id() < b.id();
        });
        Account const lo = pair[0];
        Account const hi = pair[1];
        Env env(*this);
        env.fund(XRP(10'000), lo, hi);
        env.close();
        auto const cur = lo["USD"].issue().currency;
        auto const lineK = keylet::line(lo.id(), hi.id(), cur);
        auto const j = beast::Journal{beast::Journal::getNullSink()};
        auto deliver = [&](Sandbox& sb, Account const& iss, Account const& holder) {
            BEAST_EXPECT(accountSend(sb, iss.id(), holder.id(), iss["USD"](100), j) == tesSUCCESS);
        };

        // all conjuncts true: the if fires, ownerCount drops by 1 (line deleted)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), -1, "accountSend.reserve_all_true");
        }
        // A false: saBefore <= 0 (balance staged to 0 before the full redeem)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfBalance, STAmount{Issue{cur, noAccount()}, 0});
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_saBefore");
        }
        // B false: saBalance > 0 after (partial redeem keeps a positive balance)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](50), 0, "accountSend.reserve_no_saBalance");
        }
        // C false: sender reserve flag clear
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) & ~lsfLowReserve);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_reserveFlag");
        }
        // D false: line no-ripple == account default-ripple (set lsfLowNoRipple)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | lsfLowNoRipple);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_noRippleDiff");
        }
        // E false: line frozen
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | lsfLowFreeze);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_notFrozen");
        }
        // F false (low): sender trust limit nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfLowLimit, STAmount{Issue{cur, lo.id()}, 1000});
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_zeroLimit_lo");
        }
        // G false (low): sender quality in nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfLowQualityIn, 1u);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_qualityIn_lo");
        }
        // H false (low): sender quality out nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfLowQualityOut, 1u);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, lo, hi, hi["USD"](100), 0, "accountSend.reserve_no_qualityOut_lo");
        }

        // F false (high): sender trust limit nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfHighLimit, STAmount{Issue{cur, hi.id()}, 1000});
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, hi, lo, lo["USD"](100), 0, "accountSend.reserve_no_zeroLimit_hi");
        }
        // G false (high): sender quality in nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfHighQualityIn, 1u);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, hi, lo, lo["USD"](100), 0, "accountSend.reserve_no_qualityIn_hi");
        }
        // H false (high): sender quality out nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfHighQualityOut, 1u);
            sb.update(line);
            runAccountSendOwnerDelta(
                sb, hi, lo, lo["USD"](100), 0, "accountSend.reserve_no_qualityOut_hi");
        }
    }

    void
    runTests() override
    {
        testAccountSendXRP();
        testAccountSendXRPHoldingInvariant();
        testAccountSendIOU();
        testAccountSendMPT();
        testAccountSendIOUReserveClear();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAccountSend, formal_verification, xrpl);

}  // namespace xrpl::test
