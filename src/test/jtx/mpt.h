#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/owners.h>
#include <test/jtx/tag.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::test::jtx {

class MPTTester;

auto const kMptDexFlags = tfMPTCanTrade | tfMPTCanTransfer;

/**
 * @brief Create a zero-initialized buffer for malformed cryptography test
 * inputs.
 *
 * xrpl::Buffer(size) allocates uninitialized heap memory. Because CI runs unit
 * tests sequentially in the same process, uninitialized memory can recycle
 * valid secp256k1 keys or Pedersen commitments from earlier tests. Explicitly
 * zeroing the buffer guarantees structural validation fails deterministically.
 *
 * @param size The number of zero bytes to allocate.
 * @return A buffer containing size zero bytes.
 */
[[nodiscard]] inline Buffer
gMakeZeroBuffer(std::size_t size)
{
    Buffer b(size);
    if (size > 0)
        std::memset(b.data(), 0, size);
    return b;
}

/** @brief Test helper that checks MPT flag settings after creation. */
class MptFlags
{
private:
    MPTTester& tester_;
    std::uint32_t flags_;
    std::optional<Account> holder_;

public:
    MptFlags(
        MPTTester& tester,
        std::uint32_t flags,
        std::optional<Account> const& holder = std::nullopt)
        : tester_(tester), flags_(flags), holder_(holder)
    {
    }

    void
    operator()(Env& env) const;
};

/** @brief Test helper that checks MPT issuance or holder balances. */
class MptBalance
{
private:
    MPTTester const& tester_;
    Account const& account_;
    std::int64_t const amount_;

public:
    MptBalance(MPTTester& tester, Account const& account, std::int64_t amount)
        : tester_(tester), account_(account), amount_(amount)
    {
    }

    void
    operator()(Env& env) const;
};

/** @brief Test helper that accepts any condition supplied by a callback. */
class RequireAny
{
private:
    std::function<bool()> cb_;

public:
    RequireAny(std::function<bool()> const& cb) : cb_(cb)
    {
    }

    void
    operator()(Env& env) const;
};

using Holders = std::vector<Account>;

/** @brief Arguments for building an MPTokenIssuanceCreate test transaction. */
struct MPTCreate
{
    static inline std::vector<Account> allHolders = {};
    std::optional<Account> issuer = std::nullopt;
    std::optional<std::uint64_t> maxAmt = std::nullopt;
    std::optional<std::uint8_t> assetScale = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    // authorize if seated.
    // if empty vector then authorize all holders
    std::optional<std::vector<Account>> authorize = std::nullopt;
    // pay if seated. if authorize is not seated then authorize.
    // if empty vector then pay to either authorize or all holders.
    std::optional<std::pair<std::vector<Account>, std::uint64_t>> pay = std::nullopt;
    std::optional<std::uint32_t> flags = {0};
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    bool authHolder = false;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for initializing funded MPT test accounts and issuance. */
struct MPTInit
{
    // Default-initialized so designated-initializer call sites that omit
    // `holders` don't trip GCC's -Werror=missing-field-initializers.
    Holders holders = {};  // NOLINT(readability-redundant-member-init)
    std::optional<Account> auditor = std::nullopt;
    PrettyAmount const xrp = XRP(10'000);
    PrettyAmount const xrpHolders = XRP(10'000);
    bool fund = true;
    bool close = true;
    // create MPTIssuanceID if seated and follow rules for MPTCreate args
    std::optional<MPTCreate> create = std::nullopt;
};
static MPTInit const kMptInitNoFund{.fund = false};

/** @brief Full constructor arguments for MPTTester initialization. */
struct MPTInitDef
{
    Env& env;
    Account issuer;
    Holders holders = {};  // NOLINT(readability-redundant-member-init)
    std::optional<Account> auditor = std::nullopt;
    std::uint16_t transferFee = 0;
    std::optional<std::uint64_t> pay = std::nullopt;
    std::uint32_t flags = kMptDexFlags;
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    bool authHolder = false;
    bool fund = false;
    bool close = true;
    std::optional<std::uint64_t> maxAmt = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building an MPTokenIssuanceDestroy test transaction. */
struct MPTDestroy
{
    std::optional<Account> issuer = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building an MPTokenAuthorize test transaction. */
struct MPTAuthorize
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building an MPTokenIssuanceSet test transaction. */
struct MPTSet
{
    std::optional<Account> account = std::nullopt;
    std::optional<std::variant<Account, AccountID>> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<std::string> metadata = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<uint256> domainID = std::nullopt;
    std::optional<Buffer> issuerPubKey = std::nullopt;
    std::optional<Buffer> auditorPubKey = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building a ConfidentialMPTConvert test transaction. */
struct MPTConvert
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<bool> fillAuditorEncryptedAmt = true;
    // indicates whether to autofill schnorr proof.
    // default : auto generate proof if holderPubKey is present.
    // true: force proof generation.
    // false: force proof omission.
    std::optional<bool> fillSchnorrProof = std::nullopt;
    std::optional<Buffer> holderPubKey = std::nullopt;
    std::optional<Buffer> holderEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<Buffer> auditorEncryptedAmt = std::nullopt;

    std::optional<Buffer> blindingFactor = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<XRPAmount> fee = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building a ConfidentialMPTMergeInbox test transaction. */
struct MPTMergeInbox
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<XRPAmount> fee = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building a ConfidentialMPTSend test transaction. */
struct MPTConfidentialSend
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> dest = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    // amt is to generate encrypted amounts for testing purposes
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<Buffer> senderEncryptedAmt = std::nullopt;
    std::optional<Buffer> destEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<Buffer> auditorEncryptedAmt = std::nullopt;
    std::optional<bool> fillAuditorEncryptedAmt = true;
    std::optional<std::vector<std::string>> credentials = std::nullopt;
    // not an txn param, only used for autofilling
    std::optional<Buffer> blindingFactor = std::nullopt;
    std::optional<Buffer> amountCommitment = std::nullopt;
    std::optional<Buffer> balanceCommitment = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<std::uint32_t> destinationTag = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<XRPAmount> fee = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building a ConfidentialMPTConvertBack test transaction. */
struct MPTConvertBack
{
    std::optional<Account> account = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<Buffer> proof = std::nullopt;
    std::optional<Buffer> holderEncryptedAmt = std::nullopt;
    std::optional<Buffer> issuerEncryptedAmt = std::nullopt;
    std::optional<Buffer> auditorEncryptedAmt = std::nullopt;
    std::optional<bool> fillAuditorEncryptedAmt = true;
    // not an txn param, only used for autofilling
    std::optional<Buffer> blindingFactor = std::nullopt;
    std::optional<Buffer> pedersenCommitment = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<XRPAmount> fee = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/** @brief Arguments for building a ConfidentialMPTClawback test transaction. */
struct MPTConfidentialClawback
{
    std::optional<Account> account = std::nullopt;
    std::optional<Account> holder = std::nullopt;
    std::optional<MPTID> id = std::nullopt;
    std::optional<std::uint64_t> amt = std::nullopt;
    std::optional<std::string> proof = std::nullopt;
    std::optional<Account> delegate = std::nullopt;
    std::optional<std::uint32_t> ticketSeq = std::nullopt;
    std::optional<std::uint32_t> ownerCount = std::nullopt;
    std::optional<std::uint32_t> holderCount = std::nullopt;
    std::optional<std::uint32_t> flags = std::nullopt;
    std::optional<XRPAmount> fee = std::nullopt;
    std::optional<TER> err = std::nullopt;
};

/**
 * @brief Stores the parameters that are exclusively used to generate a
 * Pedersen linkage proof.
 */
struct PedersenProofParams
{
    /** @brief The Pedersen commitment used by the proof. */
    Buffer const pedersenCommitment;

    /** @brief Either the spending balance or the value being transferred. */
    uint64_t const amt;

    /** @brief The encrypted amount linked to the Pedersen commitment. */
    Buffer const encryptedAmt;

    /** @brief The blinding factor used to create the Pedersen commitment. */
    Buffer const blindingFactor;
};

/**
 * @brief When building multiple confidential sends from the same account inside a
 * single batch transaction, pass this state to the transaction builder for
 * each subsequent send so that its proof references the post previous-send
 * encrypted balance rather than the stale pre-send ledger state.
 *
 * The fields mirror what the ledger will contain after the preceding send's
 * doApply() completes:
 *   encSpending = homomorphicSubtract(prevEncSpending, senderEncAmt)
 *   version     = prevVersion + 1
 */
struct ConfidentialSendChainState
{
    /** @brief Decrypted spending balance after the previous send. */
    std::uint64_t spending;

    /** @brief Encrypted spending balance after the previous send. */
    Buffer encSpending;

    /** @brief sfConfidentialBalanceVersion after the previous send. */
    std::uint32_t version;
};

/**
 * @brief Use this when building a second (or later) confidential send from the same
 * account in the same batch. Pass the state to the chain aware
 * transaction builder so that the next proof is constructed against the
 * correct post-send encrypted balance and version.
 *
 * @param currentSpending     Decrypted spending balance before the send.
 * @param currentEncSpending  sfConfidentialBalanceSpending before the send.
 * @param currentVersion      sfConfidentialBalanceVersion before the send.
 * @param sendAmt             Plaintext amount being sent.
 * @param senderEncAmt        sfSenderEncryptedAmount from the send transaction.
 * @return The predicted chain state, or std::nullopt if homomorphic
 *         subtraction fails.
 */
std::optional<ConfidentialSendChainState>
computeNextSendChainState(
    std::uint64_t currentSpending,
    Slice const& currentEncSpending,
    std::uint32_t currentVersion,
    std::uint64_t sendAmt,
    Slice const& senderEncAmt);

/**
 * @brief Test helper for creating, mutating, and asserting MPT and confidential
 * MPT ledger state.
 */
class MPTTester
{
    Env& env_;
    Account const issuer_;
    std::unordered_map<std::string, Account> const holders_;
    std::optional<Account> const auditor_;
    std::optional<MPTID> id_;
    bool close_;
    std::unordered_map<AccountID, Buffer> pubKeys_;
    std::unordered_map<AccountID, Buffer> privKeys_;

public:
    enum class EncryptedBalanceType {
        IssuerEncryptedBalance,
        HolderEncryptedInbox,
        HolderEncryptedSpending,
        AuditorEncryptedBalance,
    };

    static constexpr auto issuerEncryptedBalance = EncryptedBalanceType::IssuerEncryptedBalance;
    static constexpr auto holderEncryptedInbox = EncryptedBalanceType::HolderEncryptedInbox;
    static constexpr auto holderEncryptedSpending = EncryptedBalanceType::HolderEncryptedSpending;
    static constexpr auto auditorEncryptedBalance = EncryptedBalanceType::AuditorEncryptedBalance;

    MPTTester(Env& env, Account issuer, MPTInit const& constr = {});
    MPTTester(MPTInitDef const& constr);
    MPTTester(
        Env& env,
        Account issuer,
        MPTID const& id,
        std::vector<Account> const& holders = {},
        bool close = true);
    operator MPT() const;

    void
    create(MPTCreate const& arg = MPTCreate{});

    static json::Value
    createJV(MPTCreate const& arg = MPTCreate{});

    void
    destroy(MPTDestroy const& arg = MPTDestroy{});

    static json::Value
    destroyJV(MPTDestroy const& arg = MPTDestroy{});

    void
    authorize(MPTAuthorize const& arg = MPTAuthorize{});

    static json::Value
    authorizeJV(MPTAuthorize const& arg = MPTAuthorize{});

    void
    authorizeHolders(Holders const& holders);

    void
    set(MPTSet const& set = {});

    static json::Value
    setJV(MPTSet const& set = {});

    void
    convert(MPTConvert const& arg = MPTConvert{});

    /**
     * @brief Build a confidential convert JV without submitting it.
     *
     * @param arg Transaction builder arguments.
     * @param seq Inner transaction sequence used in the Schnorr proof context
     *            hash.
     * @return The transaction JSON object.
     */
    json::Value
    convertJV(MPTConvert const& arg, std::uint32_t seq);

    void
    mergeInbox(MPTMergeInbox const& arg = MPTMergeInbox{});

    [[nodiscard]] json::Value
    mergeInboxJV(MPTMergeInbox const& arg = MPTMergeInbox{}) const;

    void
    send(MPTConfidentialSend const& arg = MPTConfidentialSend{});

    /**
     * @brief Build a confidential send JV.
     *
     * When chain is provided, the sender's proof parameters are taken from it
     * instead of the ledger, enabling proof generation for a second or later
     * send from the same account inside a single batch.
     *
     * @param arg Transaction builder arguments.
     * @param seq Inner transaction sequence used in the proof context hash.
     * @param chain Optional predicted sender state from a previous batched
     *              send.
     * @return The transaction JSON object.
     */
    json::Value
    sendJV(
        MPTConfidentialSend const& arg,
        std::uint32_t seq,
        std::optional<ConfidentialSendChainState> chain = std::nullopt);

    /**
     * @brief Compute the projected sender state after a confidential send in a
     * batch.
     *
     * Each confidential send requires a ZK proof that the sender's spending
     * balance covers the transfer. In a batch, the second and later sends from
     * the same sender need proofs built against the updated spending balance.
     *
     * @param sender The sender whose post-send state is being predicted.
     * @param sendAmt The plaintext amount sent by the transaction.
     * @param jv The confidential send transaction JSON object.
     * @return The predicted sender state after applying the send.
     */
    [[nodiscard]] ConfidentialSendChainState
    chainAfterSend(Account const& sender, std::uint64_t sendAmt, json::Value const& jv) const;

    void
    convertBack(MPTConvertBack const& arg = MPTConvertBack{});

    /**
     * @brief Build a confidential convertBack JV without submitting it.
     *
     * Reads the current encrypted spending balance and version from the ledger,
     * so call this before the batch is submitted.
     *
     * @param arg Transaction builder arguments.
     * @param seq Inner transaction sequence used in the proof context hash.
     * @return The transaction JSON object.
     */
    json::Value
    convertBackJV(MPTConvertBack const& arg, std::uint32_t seq);

    void
    confidentialClaw(MPTConfidentialClawback const& arg = MPTConfidentialClawback{});

    [[nodiscard]] bool
    checkDomainID(std::optional<uint256> expected) const;

    [[nodiscard]] bool
    checkMPTokenAmount(Account const& holder, std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkMPTokenOutstandingAmount(std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkIssuanceConfidentialBalance(std::int64_t expectedAmount) const;

    [[nodiscard]] bool
    checkFlags(uint32_t const expectedFlags, std::optional<Account> const& holder = std::nullopt)
        const;

    [[nodiscard]] bool
    checkMetadata(std::string const& metadata) const;

    [[nodiscard]] bool
    isMetadataPresent() const;

    [[nodiscard]] bool
    checkTransferFee(std::uint16_t transferFee) const;

    [[nodiscard]] bool
    isTransferFeePresent() const;

    [[nodiscard]] Account const&
    issuer() const
    {
        return issuer_;
    }
    [[nodiscard]] Account const&
    holder(std::string const& h) const;

    void
    pay(Account const& src,
        Account const& dest,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt,
        std::optional<std::vector<std::string>> credentials = std::nullopt);

    void
    claw(
        Account const& issuer,
        Account const& holder,
        std::int64_t amount,
        std::optional<TER> err = std::nullopt);

    [[nodiscard]] PrettyAmount
    mpt(std::int64_t amount) const;

    [[nodiscard]] MPTID const&
    issuanceID() const
    {
        if (!env_.test.BEAST_EXPECT(id_))
            Throw<std::logic_error>("Uninitialized issuanceID");
        return *id_;  // NOLINT(bugprone-unchecked-optional-access)
    }

    [[nodiscard]] std::int64_t
    getBalance(Account const& account) const;

    [[nodiscard]] std::int64_t
    getIssuanceConfidentialBalance() const;

    [[nodiscard]] std::optional<Buffer>
    getEncryptedBalance(Account const& account, EncryptedBalanceType option = holderEncryptedInbox)
        const;

    MPT
    operator[](std::string const& name) const;

    PrettyAmount
    operator()(std::int64_t amount) const;

    operator Asset() const;

    void
    generateKeyPair(Account const& account);

    [[nodiscard]] std::optional<Buffer>
    getPubKey(Account const& account) const;

    [[nodiscard]] std::optional<Buffer>
    getPrivKey(Account const& account) const;

    [[nodiscard]] Buffer
    encryptAmount(Account const& account, uint64_t const amt, Buffer const& blindingFactor) const;

    [[nodiscard]] std::optional<uint64_t>
    decryptAmount(Account const& account, Buffer const& amt) const;

    [[nodiscard]] std::optional<uint64_t>
    getDecryptedBalance(Account const& account, EncryptedBalanceType balanceType) const;

    [[nodiscard]] std::optional<std::int64_t>
    getIssuanceOutstandingBalance() const;

    [[nodiscard]] std::optional<Buffer>
    getClawbackProof(
        Account const& holder,
        std::uint64_t amount,
        Buffer const& privateKey,
        uint256 const& txHash) const;

    [[nodiscard]] std::optional<Buffer>
    getSchnorrProof(Account const& account, uint256 const& ctxHash) const;

    [[nodiscard]] std::optional<Buffer>
    getConfidentialSendProof(
        Account const& sender,
        std::uint64_t const amount,
        std::vector<ConfidentialRecipient> const& recipients,
        Slice const& blindingFactor,
        uint256 const& contextHash,
        PedersenProofParams const& amountParams,
        PedersenProofParams const& balanceParams) const;

    [[nodiscard]] Buffer
    getConvertBackProof(
        Account const& holder,
        std::uint64_t const amount,
        uint256 const& contextHash,
        PedersenProofParams const& pcParams) const;

    [[nodiscard]] std::uint32_t
    getMPTokenVersion(Account const account) const;

    static Buffer
    getPedersenCommitment(std::uint64_t const amount, Buffer const& pedersenBlindingFactor);

    friend BookSpec
    operator~(MPTTester const& mpt)
    {
        return ~static_cast<MPT>(mpt);
    }

private:
    using SLEP = SLE::const_pointer;
    bool
    forObject(
        std::function<bool(SLEP const& sle)> const& cb,
        std::optional<Account> const& holder = std::nullopt) const;

    template <typename A>
    TER
    submit(A const& arg, json::Value jv)
    {
        auto const expectedFlags = Txflags(arg.flags.value_or(0));
        auto const expectedTer = Ter(arg.err.value_or(tesSUCCESS));

        if constexpr (requires { arg.fee; })
        {
            if (arg.fee)
                jv[jss::Fee] = to_string(*arg.fee);
        }

        std::optional<std::uint32_t> ticketSeq;
        if constexpr (requires { arg.ticketSeq; })
            ticketSeq = arg.ticketSeq;

        std::optional<Account> delegateAcct;
        if constexpr (requires { arg.delegate; })
            delegateAcct = arg.delegate;

        std::optional<std::uint32_t> dstTag;
        if constexpr (requires { arg.destinationTag; })
            dstTag = arg.destinationTag;

        if (ticketSeq && delegateAcct)
        {
            env_(
                jv,
                expectedFlags,
                expectedTer,
                ticket::Use(*ticketSeq),
                delegate::As(*delegateAcct));
        }
        else if (ticketSeq)
        {
            env_(jv, expectedFlags, expectedTer, ticket::Use(*ticketSeq));
        }
        else if (delegateAcct)
        {
            env_(jv, expectedFlags, expectedTer, delegate::As(*delegateAcct));
        }
        else if (dstTag)
        {
            env_(jv, expectedFlags, expectedTer, Dtag(*dstTag));
        }
        else
        {
            env_(jv, expectedFlags, expectedTer);
        }
        auto const err = env_.ter();
        if (close_)
            env_.close();
        if (arg.ownerCount)
            env_.require(Owners(issuer_, *arg.ownerCount));
        if (arg.holderCount)
        {
            for (auto const& it : holders_)
                env_.require(Owners(it.second, *arg.holderCount));
        }
        return err;
    }

    static std::unordered_map<std::string, Account>
    makeHolders(std::vector<Account> const& holders);

    [[nodiscard]] std::uint32_t
    getFlags(std::optional<Account> const& holder) const;

    template <typename T>
    void
    fillConversionCiphertexts(
        T const& arg,
        json::Value& jv,
        Buffer& holderCiphertext,
        Buffer& issuerCiphertext,
        std::optional<Buffer>& auditorCiphertext,
        Buffer& blindingFactor) const;
};

}  // namespace xrpl::test::jtx
