#include <test/jtx/xchain_bridge.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/attester.h>
#include <test/jtx/multisign.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/jss.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test::jtx {

// use this for creating a bridge for a transaction
json::Value
bridge(
    Account const& lockingChainDoor,
    Issue const& lockingChainIssue,
    Account const& issuingChainDoor,
    Issue const& issuingChainIssue)
{
    json::Value jv;
    jv[jss::LockingChainDoor] = lockingChainDoor.human();
    jv[jss::LockingChainIssue] = toJson(lockingChainIssue);
    jv[jss::IssuingChainDoor] = issuingChainDoor.human();
    jv[jss::IssuingChainIssue] = toJson(issuingChainIssue);
    return jv;
}

// use this for creating a bridge for a rpc query
json::Value
bridgeRpc(
    Account const& lockingChainDoor,
    Issue const& lockingChainIssue,
    Account const& issuingChainDoor,
    Issue const& issuingChainIssue)
{
    json::Value jv;
    jv[jss::LockingChainDoor] = lockingChainDoor.human();
    jv[jss::LockingChainIssue] = toJson(lockingChainIssue);
    jv[jss::IssuingChainDoor] = issuingChainDoor.human();
    jv[jss::IssuingChainIssue] = toJson(issuingChainIssue);
    return jv;
}

json::Value
bridgeCreate(
    Account const& acc,
    json::Value const& bridge,
    STAmount const& reward,
    std::optional<STAmount> const& minAccountCreate)
{
    json::Value jv;

    jv[jss::Account] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    jv[sfSignatureReward.getJsonName()] = reward.getJson(JsonOptions::Values::None);
    if (minAccountCreate)
    {
        jv[sfMinAccountCreateAmount.getJsonName()] =
            minAccountCreate->getJson(JsonOptions::Values::None);
    }

    jv[jss::TransactionType] = jss::XChainCreateBridge;
    return jv;
}

json::Value
bridgeModify(
    Account const& acc,
    json::Value const& bridge,
    std::optional<STAmount> const& reward,
    std::optional<STAmount> const& minAccountCreate)
{
    json::Value jv;

    jv[jss::Account] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    if (reward)
        jv[sfSignatureReward.getJsonName()] = reward->getJson(JsonOptions::Values::None);
    if (minAccountCreate)
    {
        jv[sfMinAccountCreateAmount.getJsonName()] =
            minAccountCreate->getJson(JsonOptions::Values::None);
    }

    jv[jss::TransactionType] = jss::XChainModifyBridge;
    return jv;
}

json::Value
xchainCreateClaimId(
    Account const& acc,
    json::Value const& bridge,
    STAmount const& reward,
    Account const& otherChainSource)
{
    json::Value jv;

    jv[jss::Account] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    jv[sfSignatureReward.getJsonName()] = reward.getJson(JsonOptions::Values::None);
    jv[sfOtherChainSource.getJsonName()] = otherChainSource.human();

    jv[jss::TransactionType] = jss::XChainCreateClaimID;
    return jv;
}

json::Value
xchainCommit(
    Account const& acc,
    json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    std::optional<Account> const& dst)
{
    json::Value jv;

    jv[jss::Account] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    jv[sfXChainClaimID.getJsonName()] = claimID;
    jv[jss::Amount] = amt.value.getJson(JsonOptions::Values::None);
    if (dst)
        jv[sfOtherChainDestination.getJsonName()] = dst->human();

    jv[jss::TransactionType] = jss::XChainCommit;
    return jv;
}

json::Value
xchainClaim(
    Account const& acc,
    json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    Account const& dst)
{
    json::Value jv;

    jv[sfAccount.getJsonName()] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    jv[sfXChainClaimID.getJsonName()] = claimID;
    jv[sfDestination.getJsonName()] = dst.human();
    jv[sfAmount.getJsonName()] = amt.value.getJson(JsonOptions::Values::None);

    jv[jss::TransactionType] = jss::XChainClaim;
    return jv;
}

json::Value
sidechainXchainAccountCreate(
    Account const& acc,
    json::Value const& bridge,
    Account const& dst,
    AnyAmount const& amt,
    AnyAmount const& reward)
{
    json::Value jv;

    jv[sfAccount.getJsonName()] = acc.human();
    jv[sfXChainBridge.getJsonName()] = bridge;
    jv[sfDestination.getJsonName()] = dst.human();
    jv[sfAmount.getJsonName()] = amt.value.getJson(JsonOptions::Values::None);
    jv[sfSignatureReward.getJsonName()] = reward.value.getJson(JsonOptions::Values::None);

    jv[jss::TransactionType] = jss::XChainAccountCreateCommit;
    return jv;
}

json::Value
claimAttestation(
    jtx::Account const& submittingAccount,
    json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::Account const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<jtx::Account> const& dst,
    jtx::Signer const& signer)
{
    STXChainBridge const stBridge(jvBridge);

    auto const& pk = signer.account.pk();
    auto const& sk = signer.account.sk();
    auto const sig = signClaimAttestation(
        pk,
        sk,
        stBridge,
        sendingAccount,
        sendingAmount.value,
        rewardAccount,
        wasLockingChainSend,
        claimID,
        dst);

    json::Value result;

    result[sfAccount.getJsonName()] = submittingAccount.human();
    result[sfXChainBridge.getJsonName()] = jvBridge;

    result[sfAttestationSignerAccount.getJsonName()] = signer.account.human();
    result[sfPublicKey.getJsonName()] = strHex(pk.slice());
    result[sfSignature.getJsonName()] = strHex(sig);
    result[sfOtherChainSource.getJsonName()] = toBase58(sendingAccount);
    result[sfAmount.getJsonName()] = sendingAmount.value.getJson(JsonOptions::Values::None);
    result[sfAttestationRewardAccount.getJsonName()] = toBase58(rewardAccount);
    result[sfWasLockingChainSend.getJsonName()] = wasLockingChainSend ? 1 : 0;

    result[sfXChainClaimID.getJsonName()] = STUInt64{claimID}.getJson(JsonOptions::Values::None);
    if (dst)
        result[sfDestination.getJsonName()] = toBase58(*dst);

    result[jss::TransactionType] = jss::XChainAddClaimAttestation;

    return result;
}

json::Value
createAccountAttestation(
    jtx::Account const& submittingAccount,
    json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::AnyAmount const& rewardAmount,
    jtx::Account const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    jtx::Account const& dst,
    jtx::Signer const& signer)
{
    STXChainBridge const stBridge(jvBridge);

    auto const& pk = signer.account.pk();
    auto const& sk = signer.account.sk();
    auto const sig = jtx::signCreateAccountAttestation(
        pk,
        sk,
        stBridge,
        sendingAccount,
        sendingAmount.value,
        rewardAmount.value,
        rewardAccount,
        wasLockingChainSend,
        createCount,
        dst);

    json::Value result;

    result[sfAccount.getJsonName()] = submittingAccount.human();
    result[sfXChainBridge.getJsonName()] = jvBridge;

    result[sfAttestationSignerAccount.getJsonName()] = signer.account.human();
    result[sfPublicKey.getJsonName()] = strHex(pk.slice());
    result[sfSignature.getJsonName()] = strHex(sig);
    result[sfOtherChainSource.getJsonName()] = toBase58(sendingAccount);
    result[sfAmount.getJsonName()] = sendingAmount.value.getJson(JsonOptions::Values::None);
    result[sfAttestationRewardAccount.getJsonName()] = toBase58(rewardAccount);
    result[sfWasLockingChainSend.getJsonName()] = wasLockingChainSend ? 1 : 0;

    result[sfXChainAccountCreateCount.getJsonName()] =
        STUInt64{createCount}.getJson(JsonOptions::Values::None);
    result[sfDestination.getJsonName()] = toBase58(dst);
    result[sfSignatureReward.getJsonName()] = rewardAmount.value.getJson(JsonOptions::Values::None);

    result[jss::TransactionType] = jss::XChainAddAccountCreateAttestation;

    return result;
}

JValueVec
claimAttestations(
    jtx::Account const& submittingAccount,
    json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    std::vector<jtx::Account> const& rewardAccounts,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<jtx::Account> const& dst,
    std::vector<jtx::Signer> const& signers,
    std::size_t const numAtts,
    std::size_t const fromIdx)
{
    assert(fromIdx + numAtts <= rewardAccounts.size());
    assert(fromIdx + numAtts <= signers.size());
    JValueVec vec;
    vec.reserve(numAtts);
    for (auto i = fromIdx; i < fromIdx + numAtts; ++i)
    {
        vec.emplace_back(claimAttestation(
            submittingAccount,
            jvBridge,
            sendingAccount,
            sendingAmount,
            rewardAccounts[i],
            wasLockingChainSend,
            claimID,
            dst,
            signers[i]));
    }
    return vec;
}

JValueVec
createAccountAttestations(
    jtx::Account const& submittingAccount,
    json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::AnyAmount const& rewardAmount,
    std::vector<jtx::Account> const& rewardAccounts,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    jtx::Account const& dst,
    std::vector<jtx::Signer> const& signers,
    std::size_t const numAtts,
    std::size_t const fromIdx)
{
    assert(fromIdx + numAtts <= rewardAccounts.size());
    assert(fromIdx + numAtts <= signers.size());
    JValueVec vec;
    vec.reserve(numAtts);
    for (auto i = fromIdx; i < fromIdx + numAtts; ++i)
    {
        vec.emplace_back(createAccountAttestation(
            submittingAccount,
            jvBridge,
            sendingAccount,
            sendingAmount,
            rewardAmount,
            rewardAccounts[i],
            wasLockingChainSend,
            createCount,
            dst,
            signers[i]));
    }
    return vec;
}

XChainBridgeObjects::XChainBridgeObjects()
    : mcDoor("mcDoor")
    , mcAlice("mcAlice")
    , mcBob("mcBob")
    , mcCarol("mcCarol")
    , mcGw("mcGw")
    , scDoor("scDoor")
    , scAlice("scAlice")
    , scBob("scBob")
    , scCarol("scCarol")
    , scGw("scGw")
    , scAttester("scAttester")
    , scReward("scReward")
    , mcuDoor("mcuDoor")
    , mcuAlice("mcuAlice")
    , mcuBob("mcuBob")
    , mcuCarol("mcuCarol")
    , mcuGw("mcuGw")
    , scuDoor("scuDoor")
    , scuAlice("scuAlice")
    , scuBob("scuBob")
    , scuCarol("scuCarol")
    , scuGw("scuGw")
    , mcUSD(mcGw["USD"])
    , scUSD(scGw["USD"])
    , jvXRPBridgeRPC(bridgeRpc(mcDoor, xrpIssue(), Account::kMaster, xrpIssue()))
    , jvb(bridge(mcDoor, xrpIssue(), Account::kMaster, xrpIssue()))
    , jvub(bridge(mcuDoor, xrpIssue(), Account::kMaster, xrpIssue()))
    , features(testableAmendments() | FeatureBitset{featureXChainBridge})
    , signers([] {
        static constexpr int kNumSigners = kUtXchainDefaultNumSigners;
        std::vector<Signer> result;
        result.reserve(kNumSigners);
        for (int i = 0; i < kNumSigners; ++i)
        {
            using namespace std::literals;
            auto const a = Account(
                "signer_"s + std::to_string(i), (i % 2) ? KeyType::Ed25519 : KeyType::Secp256k1);
            result.emplace_back(a);
        }
        return result;
    }())
    , altSigners([] {
        static constexpr int kNumSigners = kUtXchainDefaultNumSigners;
        std::vector<Signer> result;
        result.reserve(kNumSigners);
        for (int i = 0; i < kNumSigners; ++i)
        {
            using namespace std::literals;
            auto const a = Account(
                "alt_signer_"s + std::to_string(i),
                (i % 2) ? KeyType::Ed25519 : KeyType::Secp256k1);
            result.emplace_back(a);
        }
        return result;
    }())
    , payee([&] {
        std::vector<Account> r;
        r.reserve(signers.size());
        for (int i = 0, e = signers.size(); i != e; ++i)
        {
            r.push_back(scReward);
        }
        return r;
    }())
    , payees([&] {
        std::vector<Account> r;
        r.reserve(signers.size());
        for (int i = 0, e = signers.size(); i != e; ++i)
        {
            using namespace std::literals;
            auto const a = Account("reward_"s + std::to_string(i));
            r.push_back(a);
        }
        return r;
    }())
    , reward(XRP(1))
    , splitRewardQuorum(divide(reward, STAmount(kUtXchainDefaultQuorum), reward.get<Issue>()))
    , splitRewardEveryone(divide(reward, STAmount(kUtXchainDefaultNumSigners), reward.get<Issue>()))
    , tinyReward(drops(37))
    , tinyRewardSplit(
          (divide(tinyReward, STAmount(kUtXchainDefaultQuorum), tinyReward.get<Issue>())))
    , tinyRewardRemainder(
          tinyReward -
          multiply(tinyRewardSplit, STAmount(kUtXchainDefaultQuorum), tinyReward.get<Issue>()))
    , oneXrp(XRP(1))
    , xrpDust(divide(oneXrp, STAmount(10000), oneXrp.get<Issue>()))
{
}

void
XChainBridgeObjects::createMcBridgeObjects(Env& mcEnv)
{
    STAmount const xrpFunds{XRP(10000)};
    mcEnv.fund(xrpFunds, mcDoor, mcAlice, mcBob, mcCarol, mcGw);

    // Signer's list must match the attestation signers
    mcEnv(jtx::signers(mcDoor, signers.size(), signers));

    // create XRP bridges in both direction
    auto const reward = XRP(1);
    STAmount const minCreate = XRP(20);

    mcEnv(bridgeCreate(mcDoor, jvb, reward, minCreate));
    mcEnv.close();
}

void
XChainBridgeObjects::createScBridgeObjects(Env& scEnv)
{
    STAmount const xrpFunds{XRP(10000)};
    scEnv.fund(xrpFunds, scDoor, scAlice, scBob, scCarol, scGw, scAttester, scReward);

    // Signer's list must match the attestation signers
    scEnv(jtx::signers(Account::kMaster, signers.size(), signers));

    // create XRP bridges in both direction
    auto const reward = XRP(1);
    STAmount const minCreate = XRP(20);

    scEnv(bridgeCreate(Account::kMaster, jvb, reward, minCreate));
    scEnv.close();
}

void
XChainBridgeObjects::createBridgeObjects(Env& mcEnv, Env& scEnv)
{
    createMcBridgeObjects(mcEnv);
    createScBridgeObjects(scEnv);
}
}  // namespace xrpl::test::jtx
