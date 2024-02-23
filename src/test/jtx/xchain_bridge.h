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

#ifndef RIPPLE_TEST_JTX_XCHAINBRIDGE_H_INCLUDED
#define RIPPLE_TEST_JTX_XCHAINBRIDGE_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/XChainAttestations.h>
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>
#include <test/jtx/multisign.h>

namespace ripple {
namespace test {
namespace jtx {

using JValueVec = std::vector<Json::Value>;

constexpr std::size_t UT_XCHAIN_DEFAULT_NUM_SIGNERS = 5;
constexpr std::size_t UT_XCHAIN_DEFAULT_QUORUM = 4;

Json::Value
bridge(
    Account const& lockingChainDoor,
    Issue const& lockingChainIssue,
    Account const& issuingChainDoor,
    Issue const& issuingChainIssue);

Json::Value
bridge_create(
    Account const& acc,
    Json::Value const& bridge,
    STAmount const& reward,
    std::optional<STAmount> const& minAccountCreate = std::nullopt);

Json::Value
bridge_modify(
    Account const& acc,
    Json::Value const& bridge,
    std::optional<STAmount> const& reward,
    std::optional<STAmount> const& minAccountCreate = std::nullopt);

Json::Value
xchain_create_claim_id(
    Account const& acc,
    Json::Value const& bridge,
    STAmount const& reward,
    Account const& otherChainSource);

Json::Value
xchain_commit(
    Account const& acc,
    Json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    std::optional<Account> const& dst = std::nullopt);

Json::Value
xchain_claim(
    Account const& acc,
    Json::Value const& bridge,
    std::uint32_t claimID,
    AnyAmount const& amt,
    Account const& dst);

Json::Value
sidechain_xchain_account_create(
    Account const& acc,
    Json::Value const& bridge,
    Account const& dst,
    AnyAmount const& amt,
    AnyAmount const& xChainFee);

Json::Value
sidechain_xchain_account_claim(
    Account const& acc,
    Json::Value const& bridge,
    Account const& dst,
    AnyAmount const& amt);

Json::Value
claim_attestation(
    jtx::Account const& submittingAccount,
    Json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::Account const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<jtx::Account> const& dst,
    jtx::signer const& signer);

Json::Value
create_account_attestation(
    jtx::Account const& submittingAccount,
    Json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::AnyAmount const& rewardAmount,
    jtx::Account const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    jtx::Account const& dst,
    jtx::signer const& signer);

JValueVec
claim_attestations(
    jtx::Account const& submittingAccount,
    Json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    std::vector<jtx::Account> const& rewardAccounts,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<jtx::Account> const& dst,
    std::vector<jtx::signer> const& signers,
    std::size_t const numAtts = UT_XCHAIN_DEFAULT_QUORUM,
    std::size_t const fromIdx = 0);

JValueVec
create_account_attestations(
    jtx::Account const& submittingAccount,
    Json::Value const& jvBridge,
    jtx::Account const& sendingAccount,
    jtx::AnyAmount const& sendingAmount,
    jtx::AnyAmount const& rewardAmount,
    std::vector<jtx::Account> const& rewardAccounts,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    jtx::Account const& dst,
    std::vector<jtx::signer> const& signers,
    std::size_t const numAtts = UT_XCHAIN_DEFAULT_QUORUM,
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

    Json::Value const jvXRPBridgeRPC;
    Json::Value jvb;   // standard xrp bridge def for tx
    Json::Value jvub;  // standard xrp bridge def for tx, unfunded accounts

    FeatureBitset const features;
    std::vector<signer> const signers;
    std::vector<signer> const alt_signers;
    std::vector<Account> const payee;
    std::vector<Account> const payees;
    std::uint32_t const quorum;

    STAmount const reward;                 // 1 xrp
    STAmount const split_reward_quorum;    // 250,000 drops
    STAmount const split_reward_everyone;  // 200,000 drops

    const STAmount tiny_reward;            // 37 drops
    const STAmount tiny_reward_split;      // 9 drops
    const STAmount tiny_reward_remainder;  // 1 drops

    const STAmount one_xrp;
    const STAmount xrp_dust;

    static constexpr int drop_per_xrp = 1000000;

    XChainBridgeObjects();

    void
    createMcBridgeObjects(Env& mcEnv);

    void
    createScBridgeObjects(Env& scEnv);

    void
    createBridgeObjects(Env& mcEnv, Env& scEnv);

    JValueVec
    att_create_acct_vec(
        std::uint64_t createCount,
        jtx::AnyAmount const& amt,
        jtx::Account const& dst,
        std::size_t const numAtts,
        std::size_t const fromIdx = 0)
    {
        return create_account_attestations(
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

    Json::Value
    create_bridge(
        Account const& acc,
        Json::Value const& bridge = Json::nullValue,
        STAmount const& _reward = XRP(1),
        std::optional<STAmount> const& minAccountCreate = std::nullopt)
    {
        return bridge_create(
            acc,
            bridge == Json::nullValue ? jvb : bridge,
            _reward,
            minAccountCreate);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
