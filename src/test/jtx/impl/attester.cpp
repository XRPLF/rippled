#include <test/jtx/attester.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/XChainAttestations.h>

namespace ripple {
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
    std::optional<AccountID> const& dst)
{
    auto const toSign = Attestations::AttestationClaim::message(
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAccount,
        wasLockingChainSend,
        claimID,
        dst);
    return sign(pk, sk, makeSlice(toSign));
}

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
    AccountID const& dst)
{
    auto const toSign = Attestations::AttestationCreateAccount::message(
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAmount,
        rewardAccount,
        wasLockingChainSend,
        createCount,
        dst);
    return sign(pk, sk, makeSlice(toSign));
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
