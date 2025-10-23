#ifndef XRPL_TEST_JTX_ATTESTER_H_INCLUDED
#define XRPL_TEST_JTX_ATTESTER_H_INCLUDED

#include <xrpl/basics/Buffer.h>
#include <xrpl/protocol/AccountID.h>

#include <cstdint>
#include <optional>

namespace ripple {

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
}  // namespace ripple

#endif
