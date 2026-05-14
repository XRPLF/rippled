#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Arguments for the core MPToken authorization helper.
 *
 *  Bundles the inputs required by `authorizeMPToken()` (declared in
 *  `MPTokenHelpers.h`) so that the helper can be called from contexts outside
 *  the `MPTokenAuthorize` transactor — for example, `VaultCreate` and
 *  `VaultDeposit` reuse it to implicitly authorize vault pseudo-accounts
 *  without executing a full transaction.
 *
 *  @see authorizeMPToken
 */
struct MPTAuthorizeArgs
{
    /** Submitting account's XRP balance before fee deduction; used for the
     *  reserve check when creating a new `MPToken` object. */
    XRPAmount const& priorBalance;

    /** 192-bit identifier of the target MPT issuance. */
    MPTID const& mptIssuanceID;

    /** Account that submitted the transaction (holder when `holderID` is
     *  absent; issuer when `holderID` is present). */
    AccountID const& account;

    /** Transaction flags; `tfMPTUnauthorize` selects the
     *  delete/deauthorize path. */
    std::uint32_t flags{};

    /** When set, `account` is the issuer acting on behalf of this holder;
     *  when absent, `account` itself is the holder. */
    std::optional<AccountID> holderID;
};

/** Transactor for the `MPTokenAuthorize` transaction (ttMPTOKEN_AUTHORIZE, opcode 57).
 *
 *  Manages the bidirectional authorization handshake that governs who may hold a
 *  given Multi-Purpose Token (MPT) issuance. A single transaction type, differentiated
 *  by the presence of the optional `sfHolder` field, handles two distinct roles:
 *
 *  - **Holder path** (`sfHolder` absent): the submitting account opts in to hold the
 *    issuance (creates an `MPToken` SLE), or opts out (deletes it via `tfMPTUnauthorize`).
 *  - **Issuer path** (`sfHolder` present): the issuer grants or revokes the
 *    `lsfMPTAuthorized` flag on the named holder's existing `MPToken` SLE. Only
 *    meaningful when the issuance carries `lsfMPTRequireAuth`.
 *
 *  The protocol enforces a holder-first, issuer-second handshake: a holder must opt in
 *  before the issuer can authorize them. Gated behind the `featureMPTokensV1` amendment.
 *  Delegable to a credentialed delegate via the `mustAuthorizeMPT` privilege.
 *
 *  @note `ConsequencesFactory` is `Normal` — a well-formed transaction that fails
 *      in preclaim still consumes its sequence number and fee.
 */
class MPTokenAuthorize : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit MPTokenAuthorize(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Return the bitmask of valid transaction flags for this type.
     *
     *  Reports `tfMPTokenAuthorizeMask` to the `preflight1()` framework so that
     *  unexpected flag bits are rejected before field validation begins.
     *
     *  @param ctx Preflight context (unused; present for interface uniformity).
     *  @return The set of flag bits valid for `MPTokenAuthorize` transactions.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /** Stateless field validation — rejects degenerate self-authorization.
     *
     *  The only check performed here is that `sfAccount != sfHolder`; an account
     *  authorizing itself is a client bug with no valid interpretation. All
     *  amendment gating is handled upstream by `invokePreflight<T>()` via the
     *  `Permission` registry, so this method does not touch ledger or amendment state.
     *
     *  @param ctx Preflight context providing the transaction and rules.
     *  @return `tesSUCCESS` or `temMALFORMED` if `sfAccount == sfHolder`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Ledger-state validation against a read-only view.
     *
     *  Branches on the presence of `sfHolder`:
     *
     *  **Holder path** (`sfHolder` absent — submitter is the holder):
     *  - With `tfMPTUnauthorize`: confirms the `MPToken` SLE exists, that both
     *    `sfMPTAmount` and `sfLockedAmount` are zero (`tecHAS_OBLIGATIONS` if not),
     *    and (when `featureSingleAssetVault` is active) that `lsfMPTLocked` is not
     *    set on the token (`tecNO_PERMISSION`).
     *  - Without `tfMPTUnauthorize`: confirms the issuance exists, that the submitter
     *    is not the issuer (`tecNO_PERMISSION`), and that no `MPToken` SLE already
     *    exists (`tecDUPLICATE`).
     *
     *  **Issuer path** (`sfHolder` present — submitter is the issuer):
     *  - Confirms the holder account exists (`tecNO_DST`).
     *  - Confirms the issuance exists (`tecOBJECT_NOT_FOUND`).
     *  - Confirms the submitter is the issuance's issuer (`tecNO_PERMISSION`).
     *  - Confirms `lsfMPTRequireAuth` is set on the issuance (`tecNO_AUTH`);
     *    granting auth on a non-auth issuance is meaningless.
     *  - Confirms the holder has already created their `MPToken` entry (`tecOBJECT_NOT_FOUND`),
     *    enforcing the holder-first handshake.
     *  - Rejects pseudo-accounts (vault and loan broker) as holders (`tecNO_PERMISSION`),
     *    because pseudo-accounts are implicitly always authorized by the protocol.
     *
     *  @param ctx Preclaim context providing the read-only ledger view and transaction.
     *  @return `tesSUCCESS`, or one of the `tec`/`tef` codes described above.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Apply the authorization change to the ledger.
     *
     *  Delegates entirely to `authorizeMPToken()` from `MPTokenHelpers.h`, passing
     *  `preFeeBalance_` (the XRP balance snapshot taken by the base class before fee
     *  deduction) along with the transaction fields. All ledger mutations — creating
     *  or deleting the `MPToken` SLE, adjusting owner directory entries, and toggling
     *  `lsfMPTAuthorized` — are performed inside that helper.
     *
     *  @return `tesSUCCESS`, `tecINSUFFICIENT_RESERVE`, or a `tef` code on
     *      invariant violations inside `authorizeMPToken`.
     */
    TER
    doApply() override;

    /** Accumulate per-entry data for transaction-specific invariant checks.
     *
     *  Currently a no-op stub; no transaction-specific invariants are defined yet.
     *  The method exists to satisfy the pure-virtual interface on `Transactor` and
     *  serves as the extension point for future post-apply checks.
     *
     *  @param isDelete True if the entry was erased.
     *  @param before   Entry state before the transaction (nullptr for new entries).
     *  @param after    Entry state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalize transaction-specific invariant checks.
     *
     *  Currently a no-op stub that always returns `true`; no transaction-specific
     *  invariants are defined yet. Satisfies the pure-virtual interface on `Transactor`.
     *
     *  @param tx     The transaction being applied.
     *  @param result The tentative TER result.
     *  @param fee    The fee consumed.
     *  @param view   Read-only ledger view after the transaction.
     *  @param j      Logging sink.
     *  @return Always `true` (no invariants to fail).
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
