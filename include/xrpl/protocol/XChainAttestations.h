#pragma once

#include <xrpl/basics/Buffer.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>
#include <boost/container/vector.hpp>

#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

namespace xrpl {

namespace Attestations {

struct AttestationBase
{
    // Account associated with the public key
    AccountID attestationSignerAccount;
    // Public key from the witness server attesting to the event
    PublicKey publicKey;
    // Signature from the witness server attesting to the event
    Buffer signature;
    // Account on the sending chain that triggered the event (sent the
    // transaction)
    AccountID sendingAccount;
    // Amount transferred on the sending chain
    STAmount sendingAmount;
    // Account on the destination chain that collects a share of the attestation
    // reward
    AccountID rewardAccount;
    // Amount was transferred on the locking chain
    bool wasLockingChainSend;

    explicit AttestationBase(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend);

    AttestationBase(AttestationBase const&) = default;

    virtual ~AttestationBase() = default;

    AttestationBase&
    operator=(AttestationBase const&) = default;

    // verify that the signature attests to the data.
    [[nodiscard]] bool
    verify(STXChainBridge const& bridge) const;

protected:
    explicit AttestationBase(STObject const& o);
    explicit AttestationBase(json::Value const& v);

    [[nodiscard]] static bool
    equalHelper(AttestationBase const& lhs, AttestationBase const& rhs);

    [[nodiscard]] static bool
    sameEventHelper(AttestationBase const& lhs, AttestationBase const& rhs);

    void
    addHelper(STObject& o) const;

private:
    [[nodiscard]] virtual std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const = 0;
};

// Attest to a regular cross-chain transfer
struct AttestationClaim : AttestationBase
{
    std::uint64_t claimID;
    std::optional<AccountID> dst;

    explicit AttestationClaim(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimId,
        std::optional<AccountID> const& dst);

    explicit AttestationClaim(
        STXChainBridge const& bridge,
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimId,
        std::optional<AccountID> const& dst);

    explicit AttestationClaim(STObject const& o);
    explicit AttestationClaim(json::Value const& v);

    [[nodiscard]] STObject
    toSTObject() const;

    // return true if the two attestations attest to the same thing
    [[nodiscard]] bool
    sameEvent(AttestationClaim const& rhs) const;

    [[nodiscard]] static std::vector<std::uint8_t>
    message(
        STXChainBridge const& bridge,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t claimID,
        std::optional<AccountID> const& dst);

    [[nodiscard]] bool
    validAmounts() const;

private:
    [[nodiscard]] std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const override;

    friend bool
    operator==(AttestationClaim const& lhs, AttestationClaim const& rhs);
};

struct CmpByClaimID
{
    bool
    operator()(AttestationClaim const& lhs, AttestationClaim const& rhs) const
    {
        return lhs.claimID < rhs.claimID;
    }
};

// Attest to a cross-chain transfer that creates an account
struct AttestationCreateAccount : AttestationBase
{
    // createCount on the sending chain. This is the value of the `CreateCount`
    // field of the bridge on the sending chain when the transaction was
    // executed.
    std::uint64_t createCount;
    // Account to create on the destination chain
    AccountID toCreate;
    // Total amount of the reward pool
    STAmount rewardAmount;

    explicit AttestationCreateAccount(STObject const& o);

    explicit AttestationCreateAccount(json::Value const& v);

    explicit AttestationCreateAccount(
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        Buffer signature,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& toCreate);

    explicit AttestationCreateAccount(
        STXChainBridge const& bridge,
        AccountID attestationSignerAccount,
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& toCreate);

    [[nodiscard]] STObject
    toSTObject() const;

    // return true if the two attestations attest to the same thing
    [[nodiscard]] bool
    sameEvent(AttestationCreateAccount const& rhs) const;

    friend bool
    operator==(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs);

    [[nodiscard]] static std::vector<std::uint8_t>
    message(
        STXChainBridge const& bridge,
        AccountID const& sendingAccount,
        STAmount const& sendingAmount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::uint64_t createCount,
        AccountID const& dst);

    [[nodiscard]] bool
    validAmounts() const;

private:
    [[nodiscard]] std::vector<std::uint8_t>
    message(STXChainBridge const& bridge) const override;
};

struct CmpByCreateCount
{
    bool
    operator()(AttestationCreateAccount const& lhs, AttestationCreateAccount const& rhs) const
    {
        return lhs.createCount < rhs.createCount;
    }
};

};  // namespace Attestations

// Result when checking when two attestation match.
enum class AttestationMatch {
    // One of the fields doesn't match, and it isn't the dst field
    NonDstMismatch,
    // all of the fields match, except the dst field
    MatchExceptDst,
    // all of the fields match
    Match
};

struct XChainClaimAttestation
{
    using TSignedAttestation = Attestations::AttestationClaim;
    static SField const& arrayFieldName;

    AccountID keyAccount;
    PublicKey publicKey;
    STAmount amount;
    AccountID rewardAccount;
    bool wasLockingChainSend;
    std::optional<AccountID> dst;

    struct MatchFields
    {
        STAmount amount;
        bool wasLockingChainSend;
        std::optional<AccountID> dst;
        MatchFields(TSignedAttestation const& att);
        MatchFields(STAmount a, bool b, std::optional<AccountID> const& d)
            : amount{std::move(a)}, wasLockingChainSend{b}, dst{d}
        {
        }
    };

    explicit XChainClaimAttestation(
        AccountID const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        std::optional<AccountID> const& dst);

    explicit XChainClaimAttestation(
        STAccount const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        STAccount const& rewardAccount,
        bool wasLockingChainSend,
        std::optional<STAccount> const& dst);

    explicit XChainClaimAttestation(TSignedAttestation const& claimAtt);

    explicit XChainClaimAttestation(STObject const& o);

    explicit XChainClaimAttestation(json::Value const& v);

    [[nodiscard]] AttestationMatch
    match(MatchFields const& rhs) const;

    [[nodiscard]] STObject
    toSTObject() const;

    friend bool
    operator==(XChainClaimAttestation const& lhs, XChainClaimAttestation const& rhs);
};

struct XChainCreateAccountAttestation
{
    using TSignedAttestation = Attestations::AttestationCreateAccount;
    static SField const& arrayFieldName;

    AccountID keyAccount;
    PublicKey publicKey;
    STAmount amount;
    STAmount rewardAmount;
    AccountID rewardAccount;
    bool wasLockingChainSend;
    AccountID dst;

    struct MatchFields
    {
        STAmount amount;
        STAmount rewardAmount;
        bool wasLockingChainSend;
        AccountID dst;

        MatchFields(TSignedAttestation const& att);
    };

    explicit XChainCreateAccountAttestation(
        AccountID const& keyAccount,
        PublicKey const& publicKey,
        STAmount const& amount,
        STAmount const& rewardAmount,
        AccountID const& rewardAccount,
        bool wasLockingChainSend,
        AccountID const& dst);

    explicit XChainCreateAccountAttestation(TSignedAttestation const& claimAtt);

    explicit XChainCreateAccountAttestation(STObject const& o);

    explicit XChainCreateAccountAttestation(json::Value const& v);

    [[nodiscard]] STObject
    toSTObject() const;

    [[nodiscard]] AttestationMatch
    match(MatchFields const& rhs) const;

    friend bool
    operator==(
        XChainCreateAccountAttestation const& lhs,
        XChainCreateAccountAttestation const& rhs);
};

// Attestations from witness servers for a particular claim ID and bridge.
// Only one attestation per signature is allowed.
template <class TAttestation>
class XChainAttestationsBase
{
public:
    using AttCollection = std::vector<TAttestation>;

private:
    // Set a max number of allowed attestations to limit the amount of memory
    // allocated and processing time. This number is much larger than the actual
    // number of attestation a server would ever expect.
    static constexpr std::uint32_t kMaxAttestations = 256;
    AttCollection attestations_;

protected:
    // Prevent slicing to the base class
    ~XChainAttestationsBase() = default;

public:
    XChainAttestationsBase() = default;
    XChainAttestationsBase(XChainAttestationsBase const& rhs) = default;
    XChainAttestationsBase&
    operator=(XChainAttestationsBase const& rhs) = default;

    explicit XChainAttestationsBase(AttCollection&& sigs);

    explicit XChainAttestationsBase(json::Value const& v);

    explicit XChainAttestationsBase(STArray const& arr);

    [[nodiscard]] STArray
    toSTArray() const;

    [[nodiscard]] typename AttCollection::const_iterator
    begin() const;

    [[nodiscard]] typename AttCollection::const_iterator
    end() const;

    typename AttCollection::iterator
    begin();

    typename AttCollection::iterator
    end();

    template <class F>
    std::size_t
    eraseIf(F&& f);

    [[nodiscard]] std::size_t
    size() const;

    [[nodiscard]] bool
    empty() const;

    [[nodiscard]] AttCollection const&
    attestations() const;

    template <class T>
    void
    emplaceBack(T&& att);
};

template <class TAttestation>
[[nodiscard]] inline bool
operator==(
    XChainAttestationsBase<TAttestation> const& lhs,
    XChainAttestationsBase<TAttestation> const& rhs)
{
    return lhs.attestations() == rhs.attestations();
}

template <class TAttestation>
inline typename XChainAttestationsBase<TAttestation>::AttCollection const&
XChainAttestationsBase<TAttestation>::attestations() const
{
    return attestations_;
};

template <class TAttestation>
template <class T>
inline void
XChainAttestationsBase<TAttestation>::emplaceBack(T&& att)
{
    attestations_.emplace_back(std::forward<T>(att));
};

template <class TAttestation>
template <class F>
inline std::size_t
XChainAttestationsBase<TAttestation>::eraseIf(F&& f)
{
    return std::erase_if(attestations_, std::forward<F>(f));
}

template <class TAttestation>
inline std::size_t
XChainAttestationsBase<TAttestation>::size() const
{
    return attestations_.size();
}

template <class TAttestation>
inline bool
XChainAttestationsBase<TAttestation>::empty() const
{
    return attestations_.empty();
}

class XChainClaimAttestations final : public XChainAttestationsBase<XChainClaimAttestation>
{
    using TBase = XChainAttestationsBase<XChainClaimAttestation>;
    using TBase::TBase;
};

class XChainCreateAccountAttestations final
    : public XChainAttestationsBase<XChainCreateAccountAttestation>
{
    using TBase = XChainAttestationsBase<XChainCreateAccountAttestation>;
    using TBase::TBase;
};

}  // namespace xrpl
