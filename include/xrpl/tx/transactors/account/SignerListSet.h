#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <vector>

namespace xrpl {

/** Manages an account's multi-signature signer list on the XRP Ledger.
 *
 *  Handles three operations determined entirely by the transaction fields:
 *  creating or replacing a signer list (`sfSignerQuorum` non-zero +
 *  `sfSignerEntries` present), or destroying the existing list (`sfSignerQuorum`
 *  zero + `sfSignerEntries` absent). Any other combination is `temMALFORMED`.
 *
 *  `kCONSEQUENCES_FACTORY` is `Blocker`: a queued `SignerListSet` prevents
 *  later transactions from the same account from being applied until this one
 *  resolves, because changing signing authority has security implications that
 *  require serialized processing.
 *
 *  @note Signer accounts are not required to exist in the ledger; XRPL
 *      permits "phantom accounts" as signers.
 *  @see AccountDelete, which calls `removeFromLedger()` during account cleanup.
 */
class SignerListSet : public Transactor
{
private:
    /** Identifies which ledger mutation `doApply()` will perform. */
    enum class Operation {
        Unknown, /**< Malformed transaction; `doApply()` will not be reached. */
        Set,     /**< Create or replace the signer list. */
        Destroy  /**< Remove the signer list entirely. */
    };

    /** Cached operation type, populated by `preCompute()`. */
    Operation do_{Operation::Unknown};

    /** Cached quorum value from the transaction, populated by `preCompute()`. */
    std::uint32_t quorum_{0};

    /** Cached deserialized signer entries, sorted for duplicate detection,
     *  populated by `preCompute()`. */
    std::vector<SignerEntries::SignerEntry> signers_;

public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit SignerListSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Returns the valid transaction flags mask.
     *
     *  Returns `tfUniversalMask` when `fixInvalidTxFlags` is active so that
     *  unknown flag bits are rejected. Returns `0` (allow any flags) on older
     *  rule sets to preserve backward compatibility.
     *
     *  @param ctx Preflight context carrying the current rule set.
     *  @return The flags mask to enforce, or `0` to allow all flags.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless validation of the transaction fields.
     *
     *  Determines the intended operation via `determineOperation()` and, for
     *  `Set` operations, delegates to `validateQuorumAndSignerEntries()` to
     *  enforce signer count bounds, uniqueness, self-reference, positive
     *  weights, and quorum reachability. No ledger access is performed.
     *
     *  @param ctx Preflight context.
     *  @return `tesSUCCESS` if the transaction is well-formed; `temMALFORMED`
     *      for an invalid quorum/entries combination; `temBAD_SIGNER`,
     *      `temBAD_WEIGHT`, or `temBAD_QUORUM` for specific signer list
     *      violations.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Executes the signer list operation against the ledger.
     *
     *  Dispatches to `replaceSignerList()` or `destroySignerList()` based on
     *  the `do_` value set by `preCompute()`. The `Unknown` branch is
     *  unreachable in practice because `preflight()` already rejected it.
     *
     *  @return `tesSUCCESS` on success, or a `tec`/`tef` code on failure.
     */
    TER
    doApply() override;

    /** Parses and caches operation parameters before `doApply()` runs.
     *
     *  Calls `determineOperation()` and stores the resulting `quorum_`,
     *  `signers_`, and `do_` for use in `doApply()`, avoiding a second parse
     *  of the transaction fields. Asserts that the operation is well-formed
     *  (preflight must have already validated it).
     */
    void
    preCompute() override;

    /** No-op: no transaction-specific invariant entries to visit yet. */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** No-op: no transaction-specific invariants to finalize yet.
     *
     *  @return Always `true`.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

    /** Removes the account's signer list from the ledger without constructing
     *  a `SignerListSet` instance.
     *
     *  Called by `AccountDelete` during account deletion to clean up the
     *  owned `ltSIGNER_LIST` entry. Adjusts the owner count to match the
     *  pre- or post-`MultiSignReserve` accounting based on the `lsfOneOwnerCount`
     *  flag on the existing SLE. If no signer list exists, returns `tesSUCCESS`
     *  immediately.
     *
     *  @param registry Service registry used to obtain a journal.
     *  @param view     Mutable ledger view.
     *  @param account  The account whose signer list is to be removed.
     *  @param j        Journal for diagnostic logging.
     *  @return `tesSUCCESS` on success; `tefBAD_LEDGER` if the owner directory
     *      entry cannot be removed (indicates ledger corruption).
     */
    static TER
    removeFromLedger(
        ServiceRegistry& registry,
        ApplyView& view,
        AccountID const& account,
        beast::Journal j);

private:
    /** Parses the transaction to determine the intended operation.
     *
     *  A non-zero `sfSignerQuorum` combined with `sfSignerEntries` present
     *  maps to `Operation::Set`; zero quorum with no entries maps to
     *  `Operation::Destroy`; any other combination leaves the operation as
     *  `Operation::Unknown`. The returned signer list is sorted to enable
     *  O(n) duplicate detection via `std::adjacent_find`.
     *
     *  @param tx    The transaction to inspect.
     *  @param flags Apply-phase flags.
     *  @param j     Journal for diagnostic logging.
     *  @return A tuple of `(NotTEC, quorum, signers, Operation)`. The `NotTEC`
     *      is non-success only if `SignerEntries::deserialize` fails.
     */
    static std::tuple<NotTEC, std::uint32_t, std::vector<SignerEntries::SignerEntry>, Operation>
    determineOperation(STTx const& tx, ApplyFlags flags, beast::Journal j);

    /** Validates signer list semantics for a `Set` operation.
     *
     *  Enforces: signer count in `[kMIN_MULTI_SIGNERS, kMAX_MULTI_SIGNERS]`
     *  (currently 1–32); no duplicates (requires `signers` to be sorted);
     *  no self-referencing signer; every weight positive; and total weight
     *  reachable by at least one combination (sum ≥ quorum).
     *
     *  @param quorum  The quorum threshold from `sfSignerQuorum`.
     *  @param signers The sorted, deserialized signer list.
     *  @param account The submitting account; no signer may reference it.
     *  @param j       Journal for diagnostic logging.
     *  @param rules   Current ledger rules.
     *  @return `tesSUCCESS`, `temMALFORMED`, `temBAD_SIGNER`, `temBAD_WEIGHT`,
     *      or `temBAD_QUORUM`.
     *  @note Signer accounts are not checked for ledger existence; phantom
     *      accounts are intentionally permitted.
     */
    static NotTEC
    validateQuorumAndSignerEntries(
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry> const& signers,
        AccountID const& account,
        beast::Journal j,
        Rules const&);

    /** Creates or replaces the signer list in the ledger.
     *
     *  Removes any existing `ltSIGNER_LIST` first (which may lower the owner
     *  count and ease the subsequent reserve check), then inserts the new
     *  entry with `lsfOneOwnerCount` set, consuming exactly 1 owner-count
     *  unit under the `MultiSignReserve` amendment. Reserve is checked against
     *  `preFeeBalance_` to allow fee payment from reserve.
     *
     *  @return `tesSUCCESS`, `tecINSUFFICIENT_RESERVE`, or `tecDIR_FULL`.
     */
    TER
    replaceSignerList();

    /** Removes the signer list from the ledger.
     *
     *  Rejects with `tecNO_ALTERNATIVE_KEY` if the master key is disabled
     *  (`lsfDisableMaster`) and no `sfRegularKey` is set, which would leave
     *  the account with no signing mechanism.
     *
     *  @return `tesSUCCESS`, `tecNO_ALTERNATIVE_KEY`, or `tefBAD_LEDGER`.
     */
    TER
    destroySignerList();

    /** Serializes the cached signer list into a ledger SLE.
     *
     *  Writes `sfSignerQuorum`, `sfSignerListID` (always 0), and `sfFlags`.
     *  Conditionally writes `sfOwner` only when `fixIncludeKeyletFields` is
     *  active. Each signer entry's `sfWalletLocator` (tag) is written only
     *  when a tag is present, ensuring no spurious tag field is ever committed
     *  to the ledger.
     *
     *  @param ledgerEntry The SLE being populated.
     *  @param flags       `sfFlags` value to write; skipped if zero.
     */
    void
    writeSignersToSLE(SLE::pointer const& ledgerEntry, std::uint32_t flags) const;
};

}  // namespace xrpl
