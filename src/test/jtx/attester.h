#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/protocol/AccountID.h>

#include <cstdint>
#include <optional>

namespace xrpl {

class PublicKey;
class SecretKey;
class STXChainBridge;
class STAmount;

namespace test {
namespace jtx {

Buffer
sign_claim_attestation(
    PublicKey const& pk,
    SecretKey const& sk,
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<AccountID> const& dst);

Buffer
sign_create_account_attestation(
    PublicKey const& pk,
    SecretKey const& sk,
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    STAmount const& rewardAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    AccountID const& dst);
}  // namespace jtx
}  // namespace test
}  // namespace xrpl
