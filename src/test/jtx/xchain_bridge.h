#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/multisign.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/XChainAttestations.h>

namespace xrpl::test::jtx {

using JValueVec = std::vector<json::Value>;

constexpr std::size_t kUtXchainDefaultNumSigners = 5;
constexpr std::size_t kUtXchainDefaultQuorum = 4;

json::Value
bridge(
    Account const& lockingChainDoor,
    Issue const& lockingChainIssue,
    Account const& issuingChainDoor,
    Issue const& issuingChainIssue);

json::Value
bridgeCreate(
    Account const& acc,
    json::Value const& bridge,
    STAmount const& reward,
    std::optional<STAmount> const& minAccountCreate = std::nullopt);

json::Value
bridgeModify(
    Account const& acc,
    json::Value const& bridge,
    std::optional<STAmount> const& reward,
    std::optional<STAmount> const& minAccountCreate = std::nullopt);

json::Value
xchainCreateClaimId(
    Account const& acc,
    json::Value const& bridge,
    STAmount const& reward,
    Account const& otherChainSource);

json::Value
xchainCommit(
    Account const& acc,
    json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    std::optional<Account> const& dst = std::nullopt);

json::Value
xchainClaim(
    Account const& acc,
    json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    Account const& dst);

json::Value
sidechainXchainAccountCreate(
    Account const& acc,
    json::Value const& bridge,
    Account const& dst,
    AnyAmount const& amt,
    AnyAmount const& xChainFee);

json::Value
sidechainXchainAccountClaim(
    Account const& acc,
    json::Value const& bridge,
    Account const& dst,
    AnyAmount const& amt);

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
    jtx::Signer const& signer);

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
    jtx::Signer const& signer);

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
    std::size_t const numAtts = kUtXchainDefaultQuorum,
    std::size_t const fromIdx = 0);

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
    std::size_t const numAtts = kUtXchainDefaultQuorum,
    std::size_t const fromIdx = 0);

struct XChainBridgeObjects
{
    // funded accounts
    Account const mcDoor;
    Account const mcAlice;
    Account const mcBob;
    Account const mcCarol;
    Account const mcGw;
    Account const scDoor;
    Account const scAlice;
    Account const scBob;
    Account const scCarol;
    Account const scGw;
    Account const scAttester;
    Account const scReward;

    // unfunded accounts
    Account const mcuDoor;
    Account const mcuAlice;
    Account const mcuBob;
    Account const mcuCarol;
    Account const mcuGw;
    Account const scuDoor;
    Account const scuAlice;
    Account const scuBob;
    Account const scuCarol;
    Account const scuGw;

    IOU const mcUSD;
    IOU const scUSD;

    json::Value const jvXRPBridgeRPC;
    json::Value jvb;   // standard xrp bridge def for tx
    json::Value jvub;  // standard xrp bridge def for tx, unfunded accounts

    FeatureBitset const features;
    std::vector<Signer> const signers;
    std::vector<Signer> const altSigners;
    std::vector<Account> const payee;
    std::vector<Account> const payees;
    std::uint32_t const quorum{kUtXchainDefaultQuorum};

    STAmount const reward;               // 1 xrp
    STAmount const splitRewardQuorum;    // 250,000 drops
    STAmount const splitRewardEveryone;  // 200,000 drops

    STAmount const tinyReward;           // 37 drops
    STAmount const tinyRewardSplit;      // 9 drops
    STAmount const tinyRewardRemainder;  // 1 drops

    STAmount const oneXrp;
    STAmount const xrpDust;

    static constexpr int kDropPerXrp = 1000000;

    XChainBridgeObjects();

    void
    createMcBridgeObjects(Env& mcEnv);

    void
    createScBridgeObjects(Env& scEnv);

    void
    createBridgeObjects(Env& mcEnv, Env& scEnv);

    JValueVec
    attCreateAcctVec(
        std::uint64_t createCount,
        jtx::AnyAmount const& amt,
        jtx::Account const& dst,
        std::size_t const numAtts,
        std::size_t const fromIdx = 0)
    {
        return createAccountAttestations(
            scAttester,
            jvb,
            mcCarol,
            amt,
            reward,
            payees,
            true,
            createCount,
            dst,
            signers,
            numAtts,
            fromIdx);
    }

    [[nodiscard]] json::Value
    createBridge(
        Account const& acc,
        json::Value const& bridge = json::ValueType::Null,
        STAmount const& reward = XRP(1),
        std::optional<STAmount> const& minAccountCreate = std::nullopt) const
    {
        return bridgeCreate(
            acc, bridge == json::ValueType::Null ? jvb : bridge, reward, minAccountCreate);
    }
};

}  // namespace xrpl::test::jtx
