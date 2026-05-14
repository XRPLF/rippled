/** @file
 *  Declares all eight transactors implementing the XRPL cross-chain bridge
 *  protocol (XLS-38d).
 *
 *  The bridge protocol enables value transfers between two independent XRPL
 *  ledgers via a three-phase pipeline common to all XRPL transactors:
 *  stateless `preflight` validation, read-only `preclaim` ledger checks, and
 *  state-mutating `doApply` execution.
 *
 *  @see XChainAttestations.h for the cryptographic witness structures consumed
 *      by `XChainAddClaimAttestation` and `XChainAddAccountCreateAttestation`.
 */

#pragma once

#include <xrpl/protocol/XChainAttestations.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Maximum number of pending account-creation claims in the destination-chain
 *  queue.
 *
 *  Account-create commits are processed strictly in source-chain sequence order.
 *  This cap bounds the queue depth — and therefore the maximum processing lag —
 *  for pending account-creation attestations. If the queue is full, new
 *  `XChainCreateAccountCommit` transactions are rejected until earlier entries
 *  are finalized.
 */
constexpr size_t kXBRIDGE_MAX_ACCOUNT_CREATE_CLAIMS = 128;

/** Attaches a new bridge definition to a "door account" on one side of a
 *  cross-chain bridge, establishing it as the custody point for locked assets.
 *
 *  This is a one-time setup transaction. After it succeeds, the door account
 *  anchors all subsequent cross-chain activity on its chain: `XChainCommit`
 *  locks funds into it, witness servers attest to events on its behalf, and
 *  `XChainClaim` releases funds from it. Both a locking-chain door and an
 *  issuing-chain door must be configured (on their respective chains) before
 *  transfers can flow.
 *
 *  @note `preflight` enforces that the transaction sender must be one of the
 *      two door accounts specified in the bridge definition, that both sides
 *      carry the same asset type (XRP or a specific IOU), and that the reward
 *      amount is a non-negative XRP value. `preclaim` rejects if a bridge
 *      already exists on this account or, for IOU bridges, if the issuer
 *      account does not exist or has clawback enabled.
 */
class XChainCreateBridge : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit XChainCreateBridge(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the bridge specification before any ledger access.
     *
     *  Checks that: the transaction sender is one of the two declared door
     *  accounts; the two door accounts are distinct; both sides of the bridge
     *  carry the same asset type; and the reward amount is a non-negative XRP
     *  value.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Checks ledger state before committing the bridge creation.
     *
     *  Verifies that no bridge object already exists for this account, that
     *  the submitting account has sufficient reserve for the new ledger entry,
     *  and — for IOU bridges — that the issuer account exists and has not
     *  enabled clawback.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the ledger state permits creation; a `tec*`
     *      code otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Creates the bridge ledger object and adds it to the owner directory.
     *
     *  Initialises claim ID and account-creation counters to zero, inserts the
     *  new `ltBRIDGE` SLE, and increments the door account's owner count.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code if state has changed
     *      since preclaim (e.g., reserve no longer satisfied).
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

/** Modifies parameters of an existing bridge object owned by a door account.
 *
 *  At least one of the following must be specified: a new witness-reward
 *  amount, a new `MinAccountCreateAmount`, or the `tfClearAccountCreateAmount`
 *  flag (which removes the field and disables `XChainCreateAccountCommit` on
 *  that bridge). Setting and clearing `MinAccountCreateAmount` in the same
 *  transaction is rejected.
 *
 *  Unlike most transactors, `BridgeModify` overrides `getFlagsMask()` because
 *  bridge modification supports flag-driven parameter toggles that require a
 *  custom mask (`tfXChainModifyBridgeMask`).
 */
class BridgeModify : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit BridgeModify(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Returns the set of transaction flags valid for bridge modification.
     *
     *  Overrides the base-class default to return `tfXChainModifyBridgeMask`,
     *  which includes the `tfClearAccountCreateAmount` toggle flag not present
     *  in the common flag set.
     *
     *  @param ctx  Preflight context (unused; present for interface conformance).
     *  @return Bitmask of permitted transaction flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Validates modification parameters before any ledger access.
     *
     *  Rejects if: no modifiable field is present; both `sfMinAccountCreateAmount`
     *  and `tfClearAccountCreateAmount` are specified simultaneously; the
     *  transaction sender is not one of the bridge's door accounts; or any
     *  supplied amount is invalid.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Checks that the target bridge exists before applying modifications.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the bridge ledger entry exists; `tecNO_ENTRY`
     *      otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Applies the requested parameter changes to the bridge ledger object.
     *
     *  Updates `sfSignatureReward` and/or `sfMinAccountCreateAmount` in-place,
     *  or removes `sfMinAccountCreateAmount` when `tfClearAccountCreateAmount`
     *  is set.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code if the bridge no longer
     *      exists when doApply runs.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

/** Alias for `BridgeModify` using the canonical XRPL transaction-type name. */
using XChainModifyBridge = BridgeModify;

//------------------------------------------------------------------------------

/** Releases funds on the destination chain that were locked by `XChainCommit`.
 *
 *  This is step 4 of the normal cross-chain transfer sequence. It is required
 *  when no destination account was embedded in the original `XChainCommit`, or
 *  when the embedded destination failed (e.g., deposit auth rejected). It can
 *  only execute after a quorum of witness-server attestations has been
 *  accumulated against the referenced `XChainClaimID`.
 *
 *  On success the `XChainClaimID` ledger object is destroyed, preventing
 *  replay. On failure the claim ID survives, allowing the transaction to be
 *  resubmitted with corrected parameters (e.g., a different destination or
 *  amount). This recovery path distinguishes `XChainClaim` from the
 *  account-creation flow, which has no undo mechanism.
 *
 *  `kCONSEQUENCES_FACTORY` is `Blocker` because whether attestations have
 *  already reached quorum — and therefore whether this transaction will move
 *  funds — cannot be determined at fee-calculation time.
 */
class XChainClaim : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit XChainClaim(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the claim request before any ledger access.
     *
     *  Checks that the claimed amount is positive and matches one of the
     *  bridge's configured asset types.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state required for a successful claim.
     *
     *  Verifies that: the referenced bridge exists; the destination account
     *  exists; the claimed amount matches the bridge's expected issue for this
     *  chain; and the `XChainClaimID` exists and is owned by the transaction
     *  sender.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all preconditions are met; a `tec*` code
     *      otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Executes the fund release on the destination chain.
     *
     *  Retrieves the door account's signer list and quorum, checks that
     *  attestations on the claim ID meet quorum, transfers funds to the
     *  destination (bypassing deposit auth for the claim owner), distributes
     *  witness rewards, and destroys the `XChainClaimID` on success.
     *
     *  @return `tesSUCCESS` on successful fund transfer; a `tec*` code if
     *      quorum is not yet reached, the destination rejects the payment, or
     *      the claim ID is no longer valid. On failure the claim ID is
     *      preserved for retry.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

/** Locks assets on the source chain to initiate a cross-chain transfer.
 *
 *  This is step 2 of the normal cross-chain transfer sequence. On the locking
 *  chain it places XRP or IOU assets into the door account's custody. On the
 *  issuing chain it returns wrapped assets to the door account so they can be
 *  released on the locking chain. The transaction must reference a claim ID
 *  previously created on the *destination* chain via `XChainCreateClaimID`.
 *
 *  `kCONSEQUENCES_FACTORY` is `Custom` because the committed amount must be
 *  precisely reflected in consequence calculations: committing XRP effectively
 *  removes it from the sender's spendable balance, which the `Normal` factory
 *  cannot model.
 *
 *  @note The transaction sender must not be the door account; a door account
 *      cannot commit funds to itself. The committed amount must match the
 *      bridge's configured asset for the source chain.
 */
class XChainCommit : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    /** Computes transaction consequences reflecting the committed amount.
     *
     *  Returns the XRP value of the committed funds as the maximum spend so
     *  that the transaction queue can accurately model the sender's balance
     *  impact. Returns zero for non-XRP asset commits.
     *
     *  @param ctx  Preflight context providing the transaction fields.
     *  @return A `TxConsequences` object encoding the XRP spend (or zero for
     *      IOU commits).
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    explicit XChainCommit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the commit parameters before any ledger access.
     *
     *  Checks that the committed amount is positive, legal, and matches one of
     *  the bridge's configured asset types.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state required for a successful commit.
     *
     *  Verifies that: the referenced bridge exists; the sender is not the door
     *  account; and the committed amount matches the bridge's expected asset
     *  for this chain direction.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all preconditions are met; a `tec*` code
     *      otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Transfers the committed assets to the door account.
     *
     *  Moves funds from the sender to the door account via `transferHelper`,
     *  which enforces deposit-auth and destination-tag checks. The sender is
     *  permitted to dip into reserve to cover the transaction fee.
     *
     *  @return `tesSUCCESS` on successful transfer; a `tec*` code (e.g.,
     *      `tecUNFUNDED_PAYMENT`) if the sender has insufficient balance.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

/** Reserves a claim ID on the destination chain — the first step in a normal
 *  cross-chain transfer.
 *
 *  The claim ID must be created on the *destination* chain before
 *  `XChainCommit` can be submitted on the source chain, because the commit
 *  transaction must reference this monotonically-increasing sequence number.
 *  The account that will later send `XChainCommit` on the source chain must be
 *  bound here via `sfOtherChainSource`; this binding is the primary anti-replay
 *  mechanism and prevents an attacker from substituting a different sender
 *  after funds are locked.
 *
 *  The actual sequence number assigned to the new claim ID must be retrieved
 *  from a validated ledger after this transaction closes.
 *
 *  @note `preflight` validates only that `sfSignatureReward` is a non-negative
 *      XRP amount. `preclaim` additionally checks that the reward matches the
 *      bridge's configured reward and that the creator has sufficient reserve
 *      for the new `XChainClaimID` ledger object.
 */
class XChainCreateClaimID : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit XChainCreateClaimID(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the claim ID creation request before any ledger access.
     *
     *  Checks that `sfSignatureReward` is a non-negative XRP amount.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state required to create a claim ID.
     *
     *  Verifies that the referenced bridge exists, that the specified signature
     *  reward matches the bridge's configured reward, and that the creating
     *  account has sufficient reserve for the new ledger entry.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all preconditions are met; `tecNO_ENTRY` if the
     *      bridge does not exist; `tecXCHAIN_REWARD_MISMATCH` if the reward
     *      does not match; `tecINSUFFICIENT_RESERVE` if reserve is too low.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Creates the `XChainClaimID` ledger object and adds it to the owner
     *  directory.
     *
     *  Atomically increments the bridge's claim ID counter, initialises the
     *  new SLE with an empty attestations array and the bound source account,
     *  and increments the creator's owner count.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code if the bridge no longer
     *      exists or reserve has changed since preclaim.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

/** Submits a witness-server attestation for a regular cross-chain claim.
 *
 *  This is step 3 of the normal cross-chain transfer sequence. Each off-chain
 *  witness server calls this transaction once to add its cryptographic
 *  signature to the `XChainClaimID` on the destination chain. Attestations
 *  accumulate until the door account's signer-list quorum is reached, at which
 *  point funds are automatically transferred and rewards are distributed.
 *
 *  Signatures must originate from a key on the door account's signer list at
 *  the time the attestation is submitted. If the signer list is updated between
 *  submission and quorum, the new list governs: stale attestations may be
 *  removed and only attesters whose keys are on the *current* list receive
 *  rewards.
 *
 *  `kCONSEQUENCES_FACTORY` is `Blocker` because it is not determinable at
 *  fee-calculation time whether this attestation will push the running count
 *  past quorum and trigger an immediate fund transfer.
 */
class XChainAddClaimAttestation : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit XChainAddClaimAttestation(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the attestation before any ledger access.
     *
     *  Verifies that the embedded `AttestationClaim` is well-formed: the
     *  public key format is valid, the signature over the claim data is
     *  cryptographically correct, and the attested amounts are legal.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid attestation; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates that the attesting key is authorised by the bridge.
     *
     *  Confirms the bridge exists and that the attesting public key and its
     *  corresponding account appear in the door account's signer list.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the key is authorised; `tecXCHAIN_NO_SIGNERS_LIST`
     *      if the door account has no multi-sign list; `tecNO_ENTRY` if the
     *      bridge does not exist.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Adds the attestation to the claim ID and triggers transfer if quorum
     *  is reached.
     *
     *  Loads the `XChainClaimID` SLE, appends the new `AttestationClaim`,
     *  reconciles against the current signer list (removing any stale entries),
     *  and — if the updated set meets quorum — calls `finalizeClaimHelper` to
     *  transfer funds and distribute rewards. Destroys the claim ID on
     *  successful transfer.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code if the claim ID does
     *      not exist, the amount mismatches, or the destination account cannot
     *      receive funds.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

/** Submits a witness-server attestation for a cross-chain account-creation
 *  claim.
 *
 *  Mirrors `XChainAddClaimAttestation` but operates on the account-creation
 *  flow governed by `XChainCreateAccountCommit`. Attestations for account
 *  creation are processed in strict source-chain sequence order (by
 *  `createCount`); an attestation is only eligible to trigger execution once
 *  its sequence position is the next expected one.
 *
 *  When quorum is reached and the sequence position is current, the attested
 *  XRP is credited to the destination account (creating it if it does not yet
 *  exist). The claim entry is then removed via `OnTransferFail::RemoveClaim`
 *  to prevent a failed or stale attestation from permanently blocking all
 *  subsequent account-creation claims.
 *
 *  `kCONSEQUENCES_FACTORY` is `Blocker` for the same reason as
 *  `XChainAddClaimAttestation`: the impact of crossing quorum cannot be
 *  predicted at fee-calculation time.
 */
class XChainAddAccountCreateAttestation : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit XChainAddAccountCreateAttestation(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the account-creation attestation before any ledger access.
     *
     *  Verifies that the embedded `AttestationCreateAccount` is well-formed:
     *  the public key format is valid and the signature over the account-
     *  creation event data is cryptographically correct.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid attestation; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates that the attesting key is authorised by the bridge.
     *
     *  Same checks as `XChainAddClaimAttestation::preclaim`: bridge must exist
     *  and the attesting key/account pair must be in the door account's signer
     *  list.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if authorised; a `tec*` code otherwise.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Adds the attestation and executes account creation if quorum is reached
     *  at the expected sequence position.
     *
     *  Loads or creates the `XChainOwnedCreateAccountClaimID` SLE, appends the
     *  new `AttestationCreateAccount`, and — if quorum is met and this entry's
     *  `createCount` matches the bridge's current counter — transfers the XRP
     *  amount to the destination account (creating it if necessary) and
     *  distributes rewards. The claim entry is always removed after quorum,
     *  regardless of transfer outcome, to unblock subsequent sequence entries.
     *
     *  @return `tesSUCCESS` on success; a `tec*` code if the attestation is
     *      malformed, the bridge does not exist, or the transfer itself fails.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

/** Commits XRP on the source chain to bootstrap account creation on the
 *  destination chain.
 *
 *  Addresses the bootstrapping problem: a user who has no destination-chain
 *  account cannot call `XChainCreateClaimID`, because that requires an
 *  existing account. This transaction substitutes ordering-based replay
 *  prevention: commits are processed on the destination chain in strict
 *  source-chain sequence (enforced by the `createCount` counter), rather than
 *  via single-use claim ID objects.
 *
 *  **Restricted to XRP-to-XRP bridges only.** The bridge's
 *  `sfMinAccountCreateAmount` field both sets a minimum commit size and acts
 *  as a feature gate: if the field is absent, this transaction type is
 *  disabled on that bridge.
 *
 *  **⚠ Operational hazard — no error recovery.** If any attestation in the
 *  ordered sequence is not delivered to the destination chain, all subsequent
 *  account-creation claims are permanently blocked. There is no retry
 *  mechanism. The global cap `kXBRIDGE_MAX_ACCOUNT_CREATE_CLAIMS` limits
 *  queue depth but does not prevent this blockage.
 *
 *  **⚠ Permanent loss on failure.** Unlike `XChainCommit` (whose claim ID
 *  survives a failed claim for retry), if the destination-chain finalisation
 *  fails the committed XRP is irrecoverably lost. This transaction should be
 *  used solely as an account-creation primitive, even if the destination
 *  account already exists.
 *
 *  If the destination account already exists, the XRP is transferred to it
 *  rather than creating a new account.
 */
class XChainCreateAccountCommit : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit XChainCreateAccountCommit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates the account-creation commit before any ledger access.
     *
     *  Checks that both the committed amount and the witness reward are
     *  positive native XRP values and use the same asset type.
     *
     *  @param ctx  Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` on valid input; a `tem*` code otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state required for the account-creation commit.
     *
     *  Verifies that: the bridge exists; the specified reward matches the
     *  bridge's configured reward; `sfMinAccountCreateAmount` is set on the
     *  bridge (otherwise this transaction type is disabled); the committed
     *  amount is at least `sfMinAccountCreateAmount`; the bridge is XRP-to-XRP;
     *  and the sender is not the door account.
     *
     *  @param ctx  Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if all preconditions are met; `tecXCHAIN_SELF_COMMIT`
     *      if the sender is the door account; `tecXCHAIN_INSUFF_CREATE_AMOUNT`
     *      if the amount is below the minimum; `tecNO_ENTRY` if the bridge does
     *      not exist or account creation is not enabled on it.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Transfers the committed XRP to the door account and increments the
     *  bridge's account-creation counter.
     *
     *  Moves the committed amount plus the witness reward to the door account
     *  via `transferHelper`. The sender is permitted to dip into reserve to
     *  cover the transaction fee. Atomically increments `XChainAccountCreateCount`
     *  on the bridge SLE, establishing this commit's position in the
     *  destination-chain processing sequence.
     *
     *  @return `tesSUCCESS` on success; `tecUNFUNDED_PAYMENT` if the sender
     *      has insufficient balance.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

/** Alias for `XChainCreateAccountCommit` using the canonical XRPL
 *  transaction-type name.
 */
using XChainAccountCreateCommit = XChainCreateAccountCommit;

//------------------------------------------------------------------------------

}  // namespace xrpl
