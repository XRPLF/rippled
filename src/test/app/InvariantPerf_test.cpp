#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xrpl::test {

namespace {

std::ostream&
operator<<(std::ostream& os, std::chrono::nanoseconds ns)
{
    return os << ns.count() << "ns";
}

FeatureBitset
perfAmendments()
{
    return test::jtx::testableAmendments() | fixCleanup3_1_3 | fixCleanup3_2_0;
}

STTx
makeTx(TxType type)
{
    return STTx{type, [](STObject&) {}};
}

std::vector<jtx::Account>
makeAccounts(std::size_t const count)
{
    std::vector<jtx::Account> accounts;
    accounts.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        accounts.emplace_back("perf" + std::to_string(i));
    return accounts;
}

using LedgerTypes = std::array<LedgerEntryType, 30>;

LedgerTypes constexpr kConcreteLedgerTypes = {
    ltNFTOKEN_OFFER,
    ltCHECK,
    ltDID,
    ltNEGATIVE_UNL,
    ltNFTOKEN_PAGE,
    ltSIGNER_LIST,
    ltTICKET,
    ltACCOUNT_ROOT,
    ltDIR_NODE,
    ltAMENDMENTS,
    ltLEDGER_HASHES,
    ltBRIDGE,
    ltOFFER,
    ltDEPOSIT_PREAUTH,
    ltXCHAIN_OWNED_CLAIM_ID,
    ltRIPPLE_STATE,
    ltFEE_SETTINGS,
    ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID,
    ltESCROW,
    ltPAYCHAN,
    ltAMM,
    ltMPTOKEN_ISSUANCE,
    ltMPTOKEN,
    ltORACLE,
    ltCREDENTIAL,
    ltPERMISSIONED_DOMAIN,
    ltDELEGATE,
    ltVAULT,
    ltLOAN_BROKER,
    ltLOAN};

bool
needsOwnContext(LedgerEntryType const type)
{
    return type == ltPERMISSIONED_DOMAIN || type == ltVAULT;
}

AccountID
account(std::uint32_t const seed)
{
    return AccountID(seed);
}

Issue
usdIssue(std::uint32_t const seed = 1)
{
    return Issue{toCurrency("USD"), account(100'000 + seed)};
}

STAmount
usd(std::uint32_t const value, std::uint32_t const seed = 1)
{
    return STAmount{usdIssue(seed), value};
}

STNumber
stNumber(SField const& field, int const value)
{
    return STNumber{field, Number{value}};
}

MPTID
mptID(std::uint32_t const seed)
{
    return makeMptID(seed + 1, account(200'000 + seed));
}

STXChainBridge
xchainBridge(std::uint32_t const seed)
{
    auto const lockingDoor = account(210'000 + seed);
    auto const issuingDoor = account(220'000 + seed);
    return STXChainBridge{
        lockingDoor,
        Issue{toCurrency("USD"), lockingDoor},
        issuingDoor,
        Issue{toCurrency("USD"), issuingDoor}};
}

void
setSyntheticAccountRoot(SLE::ref sle, AccountID const& id)
{
    sle->setAccountID(sfAccount, id);
    sle->setFieldU32(sfSequence, 1);
    sle->setFieldAmount(sfBalance, STAmount{XRPAmount{}});
    sle->setFieldU32(sfOwnerCount, 0);
}

STArray
credentialRules(AccountID const& issuer, std::uint32_t const seed)
{
    STArray credentials(sfAcceptedCredentials, 1);
    auto cred = STObject::makeInnerObject(sfCredential);
    cred.setAccountID(sfIssuer, issuer);
    auto const type = "cred_type" + std::to_string(seed);
    cred.setFieldVL(sfCredentialType, Slice(type.data(), type.size()));
    credentials.pushBack(std::move(cred));
    return credentials;
}

STArray
nftokens(std::uint32_t const seed)
{
    auto const* format = InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);
    XRPL_ASSERT(format, "InvariantPerf_test::nftokens : NFToken inner format");

    STArray tokens(sfNFTokens, 1);
    auto const tokenID = uint256{seed * 4 + 1};
    STObject token{
        *format, sfNFToken, [&](STObject& object) { object.setFieldH256(sfNFTokenID, tokenID); }};
    tokens.pushBack(std::move(token));
    return tokens;
}

Keylet
syntheticKey(LedgerEntryType const type, std::uint32_t const seed)
{
    auto const id = account(10'000 + seed);
    switch (type)
    {
        case ltACCOUNT_ROOT:
            return keylet::account(id);
        case ltCHECK:
            return keylet::check(id, seed + 1);
        case ltDID:
            return keylet::did(id);
        case ltNEGATIVE_UNL:
            return {ltNEGATIVE_UNL, uint256{300'000 + seed}};
        case ltNFTOKEN_PAGE:
            return {ltNFTOKEN_PAGE, uint256{seed * 4 + 2}};
        case ltSIGNER_LIST:
            return keylet::signers(id);
        case ltTICKET:
            return keylet::kTicket(id, seed + 1);
        case ltDIR_NODE:
            return keylet::ownerDir(id);
        case ltAMENDMENTS:
            return {ltAMENDMENTS, uint256{310'000 + seed}};
        case ltLEDGER_HASHES:
            return {ltLEDGER_HASHES, uint256{330'000 + seed}};
        case ltBRIDGE:
            return keylet::bridge(xchainBridge(seed), STXChainBridge::ChainType::Locking);
        case ltOFFER:
            return keylet::offer(id, seed + 1);
        case ltDEPOSIT_PREAUTH:
            return keylet::depositPreauth(id, account(20'000 + seed));
        case ltXCHAIN_OWNED_CLAIM_ID:
            return keylet::xChainClaimID(xchainBridge(seed), seed + 1);
        case ltRIPPLE_STATE:
            return keylet::line(id, usdIssue(seed));
        case ltFEE_SETTINGS:
            return {ltFEE_SETTINGS, uint256{320'000 + seed}};
        case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
            return keylet::xChainCreateAccountClaimID(xchainBridge(seed), seed + 1);
        case ltESCROW:
            return keylet::escrow(id, seed + 1);
        case ltPAYCHAN:
            return keylet::payChan(id, account(30'000 + seed), seed + 1);
        case ltAMM:
            return keylet::amm(uint256{seed + 1});
        case ltNFTOKEN_OFFER:
            return keylet::nftoffer(id, seed + 1);
        case ltMPTOKEN_ISSUANCE:
            return keylet::mptIssuance(mptID(seed));
        case ltMPTOKEN:
            return keylet::mptoken(mptID(seed), id);
        case ltORACLE:
            return keylet::oracle(id, seed + 1);
        case ltCREDENTIAL: {
            auto const type = "cred_type" + std::to_string(seed);
            return keylet::credential(id, account(40'000 + seed), Slice(type.data(), type.size()));
        }
        case ltPERMISSIONED_DOMAIN:
            return keylet::permissionedDomain(id, seed + 1);
        case ltDELEGATE:
            return keylet::delegate(id, account(50'000 + seed));
        case ltVAULT:
            return keylet::vault(id, seed + 1);
        case ltLOAN_BROKER:
            return keylet::loanbroker(id, seed + 1);
        case ltLOAN:
            return keylet::loan(uint256{60'000 + seed}, seed + 1);
        default:
            return {type, uint256{seed + 1}};
    }
}

void
populateVault(SLE::ref sle, std::uint32_t const seed)
{
    auto const owner = account(70'000 + seed);
    auto const pseudo = account(80'000 + seed);

    sle->setFieldU32(sfSequence, seed + 1);
    sle->setAccountID(sfOwner, owner);
    sle->setAccountID(sfAccount, pseudo);
    sle->setFieldIssue(sfAsset, STIssue{sfAsset, xrpIssue()});
    sle->setFieldNumber(sfAssetsTotal, stNumber(sfAssetsTotal, 0));
    sle->setFieldNumber(sfAssetsAvailable, stNumber(sfAssetsAvailable, 0));
    sle->setFieldNumber(sfAssetsMaximum, stNumber(sfAssetsMaximum, 0));
    sle->setFieldNumber(sfLossUnrealized, stNumber(sfLossUnrealized, 0));
    sle->setFieldH192(sfShareMPTID, mptID(90'000 + seed));
    sle->setFieldU8(sfWithdrawalPolicy, kVaultStrategyFirstComeFirstServe);
}

SLE::pointer
makeSyntheticSle(LedgerEntryType const type, std::uint32_t const seed)
{
    auto sle = std::make_shared<SLE>(syntheticKey(type, seed));
    auto const id = account(10'000 + seed);
    auto const other = account(20'000 + seed);

    switch (type)
    {
        case ltNFTOKEN_OFFER:
            sle->setAccountID(sfOwner, id);
            sle->setFieldH256(sfNFTokenID, uint256{seed + 1});
            sle->setFieldAmount(sfAmount, STAmount{XRPAmount{1}});
            break;
        case ltCHECK:
            sle->setAccountID(sfAccount, id);
            sle->setAccountID(sfDestination, other);
            sle->setFieldAmount(sfSendMax, usd(1, seed));
            sle->setFieldU32(sfSequence, seed + 1);
            break;
        case ltDID:
            sle->setAccountID(sfAccount, id);
            break;
        case ltNFTOKEN_PAGE:
            sle->setFieldArray(sfNFTokens, nftokens(seed));
            break;
        case ltTICKET:
            sle->setAccountID(sfAccount, id);
            sle->setFieldU32(sfTicketSequence, seed + 1);
            break;
        case ltACCOUNT_ROOT:
            setSyntheticAccountRoot(sle, id);
            break;
        case ltDIR_NODE:
            sle->setFieldH256(sfRootIndex, sle->key());
            break;
        case ltBRIDGE:
            sle->at(sfXChainBridge) = xchainBridge(seed);
            sle->setAccountID(sfAccount, id);
            sle->setFieldAmount(sfSignatureReward, STAmount{XRPAmount{1}});
            sle->setFieldU64(sfXChainClaimID, 0);
            sle->setFieldU64(sfXChainAccountCreateCount, 0);
            sle->setFieldU64(sfXChainAccountClaimCount, 0);
            break;
        case ltOFFER:
            sle->setAccountID(sfAccount, id);
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldAmount(sfTakerPays, usd(1, seed));
            sle->setFieldAmount(sfTakerGets, STAmount{XRPAmount{1}});
            sle->setFieldH256(sfBookDirectory, uint256{seed + 1});
            break;
        case ltDEPOSIT_PREAUTH:
            sle->setAccountID(sfAccount, id);
            sle->setAccountID(sfAuthorize, other);
            break;
        case ltXCHAIN_OWNED_CLAIM_ID:
            sle->setAccountID(sfAccount, id);
            sle->at(sfXChainBridge) = xchainBridge(seed);
            sle->setFieldU64(sfXChainClaimID, seed + 1);
            sle->setAccountID(sfOtherChainSource, other);
            sle->setFieldAmount(sfSignatureReward, STAmount{XRPAmount{1}});
            break;
        case ltRIPPLE_STATE:
            sle->setFieldAmount(sfBalance, usd(0, seed));
            sle->setFieldAmount(sfLowLimit, usd(0, seed));
            sle->setFieldAmount(sfHighLimit, usd(0, seed));
            break;
        case ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID:
            sle->setAccountID(sfAccount, id);
            sle->at(sfXChainBridge) = xchainBridge(seed);
            sle->setFieldU64(sfXChainAccountCreateCount, seed + 1);
            break;
        case ltESCROW:
            sle->setAccountID(sfAccount, id);
            sle->setAccountID(sfDestination, other);
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldAmount(sfAmount, STAmount{XRPAmount{1}});
            break;
        case ltPAYCHAN:
            sle->setAccountID(sfAccount, id);
            sle->setAccountID(sfDestination, other);
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldAmount(sfAmount, STAmount{XRPAmount{1}});
            sle->setFieldAmount(sfBalance, STAmount{XRPAmount{}});
            sle->setFieldU32(sfSettleDelay, 1);
            break;
        case ltAMM:
            sle->setAccountID(sfAccount, id);
            sle->setFieldAmount(sfLPTokenBalance, STAmount{usdIssue(seed), 1});
            sle->setFieldIssue(sfAsset, STIssue{sfAsset, xrpIssue()});
            sle->setFieldIssue(sfAsset2, STIssue{sfAsset2, usdIssue(seed)});
            break;
        case ltMPTOKEN_ISSUANCE:
            sle->setAccountID(sfIssuer, account(200'000 + seed));
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldU64(sfOutstandingAmount, 0);
            break;
        case ltMPTOKEN:
            sle->setAccountID(sfAccount, id);
            sle->setFieldH192(sfMPTokenIssuanceID, mptID(seed));
            sle->setFieldU64(sfMPTAmount, 0);
            break;
        case ltORACLE:
            sle->setAccountID(sfOwner, id);
            sle->setFieldVL(sfProvider, Slice("provider", 8));
            sle->setFieldVL(sfAssetClass, Slice("currency", 8));
            sle->setFieldU32(sfLastUpdateTime, 1);
            break;
        case ltCREDENTIAL:
            sle->setAccountID(sfSubject, id);
            sle->setAccountID(sfIssuer, account(40'000 + seed));
            sle->setFieldVL(sfCredentialType, Slice("cred_type", 9));
            break;
        case ltPERMISSIONED_DOMAIN:
            sle->setAccountID(sfOwner, id);
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldArray(sfAcceptedCredentials, credentialRules(other, seed));
            break;
        case ltDELEGATE:
            sle->setAccountID(sfAccount, id);
            sle->setAccountID(sfAuthorize, other);
            break;
        case ltVAULT:
            populateVault(sle, seed);
            break;
        case ltLOAN_BROKER:
            sle->setFieldU32(sfSequence, seed + 1);
            sle->setFieldH256(sfVaultID, syntheticKey(ltVAULT, 110'000 + seed).key);
            sle->setAccountID(sfAccount, account(120'000 + seed));
            sle->setAccountID(sfOwner, id);
            sle->setFieldU32(sfLoanSequence, 1);
            sle->setFieldNumber(sfDebtTotal, stNumber(sfDebtTotal, 0));
            sle->setFieldNumber(sfCoverAvailable, stNumber(sfCoverAvailable, 0));
            break;
        case ltLOAN:
            sle->setFieldH256(sfLoanBrokerID, uint256{60'000 + seed});
            sle->setFieldU32(sfLoanSequence, seed + 1);
            sle->setAccountID(sfBorrower, id);
            sle->setFieldU32(sfStartDate, 1);
            sle->setFieldU32(sfPaymentInterval, 1);
            sle->setFieldU32(sfPaymentRemaining, 0);
            sle->setFieldNumber(sfPeriodicPayment, stNumber(sfPeriodicPayment, 1));
            sle->setFieldNumber(sfPrincipalOutstanding, stNumber(sfPrincipalOutstanding, 0));
            sle->setFieldNumber(sfTotalValueOutstanding, stNumber(sfTotalValueOutstanding, 0));
            sle->setFieldNumber(
                sfManagementFeeOutstanding, stNumber(sfManagementFeeOutstanding, 0));
            break;
        default:
            break;
    }

    return sle;
}

void
seedSyntheticDependencies(OpenView& ov, LedgerEntryType const type, std::uint32_t const seed)
{
    if (type == ltMPTOKEN)
    {
        auto issuance = makeSyntheticSle(ltMPTOKEN_ISSUANCE, seed);
        ov.rawInsert(issuance);
    }

    if (type == ltVAULT)
    {
        auto const vault = makeSyntheticSle(ltVAULT, seed);
        MPTID const shareID = vault->getFieldH192(sfShareMPTID);
        auto issuance = std::make_shared<SLE>(keylet::mptIssuance(shareID));
        auto const issuer = getMPTIssuer(shareID);
        issuance->setAccountID(sfIssuer, issuer);
        issuance->setFieldU32(sfSequence, seed + 90'001);
        issuance->setFieldU64(sfOutstandingAmount, 0);
        ov.rawInsert(issuance);
        return;
    }

    if (type == ltLOAN_BROKER)
    {
        auto vault = makeSyntheticSle(ltVAULT, 110'000 + seed);
        ov.rawInsert(vault);
    }
}

}  // namespace

class InvariantPerf_test : public beast::unit_test::Suite
{
    using Clock = std::chrono::steady_clock;

    struct Timing
    {
        Clock::duration mean;
        Clock::duration stdDev;
        std::size_t samples;
    };

    template <class F>
    Timing
    time(std::size_t n, F&& f)
    {
        assert(n > 0);

        double sum = 0;
        double sumSquared = 0;
        std::size_t samples = 0;

        while (samples < n)
        {
            std::array<long, 100> batch = {};
            for (auto& sample : batch)
            {
                auto const start = Clock::now();
                f();
                sample = (Clock::now() - start).count();
            }

            std::ranges::sort(batch);
            for (std::size_t i = 35; i < 65 && samples < n; ++i)
            {
                ++samples;
                sum += batch[i];
                sumSquared += batch[i] * batch[i];
            }
        }

        double const mean = sum / samples;
        double const variance = (sumSquared / samples) - (mean * mean);
        return {
            Clock::duration{static_cast<Clock::rep>(mean)},
            Clock::duration{static_cast<Clock::rep>(std::sqrt(std::max(0.0, variance)))},
            samples};
    }

    template <class SetupEnv, class SetupContext>
    void
    report(
        std::string_view name,
        std::size_t const changeCount,
        TxType const txType,
        SetupEnv&& setupEnv,
        SetupContext&& setupContext)
    {
        using namespace test::jtx;

        testcase(std::string{name});

        Env env{*this, perfAmendments()};
        setupEnv(env);
        env.close();

        OpenView ov{*env.current()};
        auto tx = makeTx(txType);
        test::StreamSink sink{beast::Severity::Warning};
        beast::Journal const journal{sink};
        ApplyContext ac{
            env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, journal};
        CurrentTransactionRulesGuard const rulesGuard(ov.rules());

        setupContext(ac);
        BEAST_EXPECT(ac.size() == changeCount);
        BEAST_EXPECT(ac.checkInvariants(tesSUCCESS, XRPAmount{}) == tesSUCCESS);

        std::size_t attempts = 0;
        std::size_t passes = 0;
        auto const result = time(3'000, [&] {
            ++attempts;
            if (ac.checkInvariants(tesSUCCESS, XRPAmount{}) == tesSUCCESS)
                ++passes;
        });

        std::cout << "InvariantPerf " << name << " changes=" << changeCount
                  << " mean=" << std::chrono::duration_cast<std::chrono::nanoseconds>(result.mean)
                  << " stdDev="
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(result.stdDev)
                  << " N=" << result.samples << '\n';

        BEAST_EXPECT(passes == attempts);
    }

    struct BenchContext
    {
        std::unique_ptr<OpenView> view;
        STTx tx;
        test::StreamSink sink;
        beast::Journal journal;
        std::unique_ptr<ApplyContext> applyContext;

        BenchContext(
            Application& app,
            std::shared_ptr<ReadView const> const& base,
            TxType const txType)
            : view{std::make_unique<OpenView>(base.get(), base)}
            , tx{makeTx(txType)}
            , sink{beast::Severity::Warning}
            , journal{sink}
            , applyContext{std::make_unique<
                  ApplyContext>(app, *view, tx, tesSUCCESS, base->fees().base, TapNone, journal)}
        {
        }
    };

    void
    seedAndStage(
        BenchContext& context,
        LedgerEntryType const type,
        std::size_t const changeCount,
        std::uint32_t const seedBase)
    {
        for (std::size_t i = 0; i < changeCount; ++i)
        {
            auto const seed = seedBase + static_cast<std::uint32_t>(i);
            try
            {
                seedSyntheticDependencies(*context.view, type, seed);

                auto sle = makeSyntheticSle(type, seed);
                auto const key = sle->key();
                context.view->rawInsert(sle);

                auto const staged = context.applyContext->view().peek(keylet::unchecked(key));
                if (!BEAST_EXPECT(staged))
                    continue;
                staged->setFieldU32(sfFlags, staged->getFlags() ^ 0x4000'0000);
                context.applyContext->view().update(staged);
            }
            catch (std::exception const& e)
            {
                fail(
                    "synthetic ledger entry setup failed for type " +
                    std::to_string(static_cast<std::uint16_t>(type)) + " seed " +
                    std::to_string(seed) + ": " + e.what());
                throw;
            }
        }
    }

    bool
    check(BenchContext& context)
    {
        return context.applyContext->checkInvariants(tesSUCCESS, XRPAmount{}) == tesSUCCESS;
    }

    void
    reportAllLedgerEntryTypes(std::size_t const changeCount)
    {
        using namespace test::jtx;

        testcase("all ledger entry types");

        Env env{*this, perfAmendments()};
        env.close();
        CurrentTransactionRulesGuard const rulesGuard(env.current()->rules());

        std::vector<std::unique_ptr<BenchContext>> contexts;
        contexts.reserve(1 + (2 * changeCount));

        auto generic = std::make_unique<BenchContext>(env.app(), env.current(), ttACCOUNT_SET);
        std::size_t expectedChanges = 0;
        std::uint32_t seedBase = 1;

        for (auto const type : kConcreteLedgerTypes)
        {
            if (needsOwnContext(type))
                continue;

            seedAndStage(*generic, type, changeCount, seedBase);
            expectedChanges += changeCount;
            seedBase += 10'000;
        }
        contexts.push_back(std::move(generic));

        for (auto const type : {ltPERMISSIONED_DOMAIN, ltVAULT})
        {
            for (std::size_t i = 0; i < changeCount; ++i)
            {
                auto context = std::make_unique<BenchContext>(
                    env.app(),
                    env.current(),
                    type == ltPERMISSIONED_DOMAIN ? ttPERMISSIONED_DOMAIN_SET : ttVAULT_SET);
                seedAndStage(*context, type, 1, seedBase + static_cast<std::uint32_t>(i));
                ++expectedChanges;
                contexts.push_back(std::move(context));
            }
            seedBase += 10'000;
        }

        std::size_t totalChanges = 0;
        for (auto const& context : contexts)
        {
            totalChanges += context->applyContext->size();
            BEAST_EXPECT(check(*context));
        }
        BEAST_EXPECT(totalChanges == expectedChanges);
        BEAST_EXPECT(expectedChanges == changeCount * kConcreteLedgerTypes.size());

        std::size_t attempts = 0;
        std::size_t passes = 0;
        auto const result = time(300, [&] {
            ++attempts;
            bool ok = true;
            for (auto const& context : contexts)
                ok = check(*context) && ok;
            if (ok)
                ++passes;
        });

        std::cout << "InvariantPerf all ledger entry types changes_per_type=" << changeCount
                  << " total_changes=" << totalChanges
                  << " mean=" << std::chrono::duration_cast<std::chrono::nanoseconds>(result.mean)
                  << " stdDev="
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(result.stdDev)
                  << " N=" << result.samples << '\n';

        BEAST_EXPECT(passes == attempts);
    }

    void
    reportAccountRootUpdates(std::size_t const changeCount)
    {
        auto accounts = makeAccounts(changeCount);

        report(
            "account root updates",
            changeCount,
            ttACCOUNT_SET,
            [&](jtx::Env& env) {
                for (auto const& account : accounts)
                    env.fund(jtx::XRP(1'000), account);
            },
            [&](ApplyContext& ac) {
                for (auto const& account : accounts)
                {
                    auto const sle = ac.view().peek(keylet::account(account.id()));
                    if (!BEAST_EXPECT(sle))
                        continue;
                    sle->at(sfSequence) += 1;
                    ac.view().update(sle);
                }
            });
    }

    void
    reportTrustLineUpdates(std::size_t const changeCount)
    {
        using namespace test::jtx;

        Account const gateway{"gateway"};
        auto const usd = gateway["USD"];
        auto accounts = makeAccounts(changeCount);

        report(
            "trust line updates",
            changeCount,
            ttPAYMENT,
            [&](Env& env) {
                env.fund(XRP(1'000'000), gateway);
                for (auto const& account : accounts)
                {
                    env.fund(XRP(1'000), account);
                    env.trust(usd(1'000), account);
                }
            },
            [&](ApplyContext& ac) {
                for (std::size_t i = 0; i < accounts.size(); ++i)
                {
                    auto const sle = ac.view().peek(keylet::line(accounts[i], usd));
                    if (!BEAST_EXPECT(sle))
                        continue;
                    sle->setFieldAmount(sfBalance, usd(static_cast<std::int64_t>(i + 1)));
                    ac.view().update(sle);
                }
            });
    }

    void
    reportOfferCreates(std::size_t const changeCount)
    {
        using namespace test::jtx;

        Account const trader{"trader"};
        Account const gateway{"gateway"};
        auto const usd = gateway["USD"];

        report(
            "offer creates",
            changeCount,
            ttOFFER_CREATE,
            [&](Env& env) {
                env.fund(XRP(1'000'000), gateway, trader);
                env.trust(usd(1'000'000), trader);
                env(pay(gateway, trader, usd(1'000'000)));
            },
            [&](ApplyContext& ac) {
                for (std::size_t i = 0; i < changeCount; ++i)
                {
                    auto const seq = static_cast<std::uint32_t>(1'000'000 + i);
                    auto sle = std::make_shared<SLE>(keylet::offer(trader.id(), seq));
                    sle->setAccountID(sfAccount, trader.id());
                    sle->setFieldU32(sfSequence, seq);
                    sle->setFieldAmount(sfTakerPays, usd(1));
                    sle->setFieldAmount(sfTakerGets, XRP(1));
                    ac.view().insert(sle);
                }
            });
    }

public:
    void
    run() override
    {
        for (auto const changeCount : {std::size_t{1}, std::size_t{16}, std::size_t{64}})
        {
            reportAccountRootUpdates(changeCount);
            reportTrustLineUpdates(changeCount);
            reportOfferCreates(changeCount);
            reportAllLedgerEntryTypes(changeCount);
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(InvariantPerf, app, xrpl);

}  // namespace xrpl::test
