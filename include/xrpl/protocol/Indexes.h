/** @file
 *  Single authoritative source for computing the 256-bit ledger-state addresses
 *  of every object type in the XRP Ledger.
 *
 *  All key derivations use "tagged hashing": a `sha512Half` over a type-specific
 *  `LedgerNameSpace` discriminator prepended to the object's identifying
 *  parameters.  This prevents cross-type key collisions even when two object
 *  types share identical parameter values.  The namespace discriminators are
 *  protocol-immutable; changing them constitutes a hard fork.
 *
 *  The primary API is the `xrpl::keylet` namespace, whose functions return
 *  `Keylet` values pairing a 256-bit key with its expected `LedgerEntryType`.
 *  Free functions below the namespace (`getBookBase`, `getQuality`, etc.) are
 *  deprecated predecessors retained for backward compatibility.
 */
#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <set>

namespace xrpl {

class SeqProxy;

/** Keylet computation functions for every XRPL ledger object type.
 *
 *  Entries in the ledger are located using 256-bit keys derived by hashing
 *  object-specific parameters under a type-specific namespace discriminator.
 *  Each function in this namespace returns a `Keylet` — a pair of the derived
 *  key and the expected `LedgerEntryType` — enabling type-safe ledger lookups
 *  that catch category errors at retrieval time via `Keylet::check()`.
 *
 *  @note All namespace discriminator values are part of the consensus protocol
 *      and must never be changed.  Adding a new keylet function requires
 *      assigning a new, previously unused discriminator character.
 */
namespace keylet {

/** Return the keylet for an AccountRoot ledger entry.
 *
 *  @param id  The account address.
 *  @return Keylet typed `ltACCOUNT_ROOT`.
 */
Keylet
account(AccountID const& id) noexcept;

/** Return the keylet for the singleton amendments table.
 *
 *  The amendments object has no parameters; its key is computed once and
 *  returned as a reference to a function-local static (Meyers singleton).
 *
 *  @return Reference to a static `Keylet` typed `ltAMENDMENTS`.
 */
Keylet const&
amendments() noexcept;

/** Return a wildcard keylet for any item that can appear in an owner directory.
 *
 *  Uses `ltCHILD` so that `Keylet::check()` accepts any entry type —
 *  useful when iterating a directory without knowing the contained type.
 *
 *  @param key  Raw 256-bit ledger key of the directory child entry.
 *  @return Keylet typed `ltCHILD`.
 */
Keylet
child(uint256 const& key) noexcept;

/** Return the keylet for the "short" ledger-hash skip list.
 *
 *  The short skip list is a singleton object holding the hashes of ledgers
 *  since the last flag ledger (at most 256 entries).  Its key is computed
 *  once and returned as a reference to a function-local static.
 *
 *  @return Reference to a static `Keylet` typed `ltLEDGER_HASHES`.
 */
Keylet const&
skip() noexcept;

/** Return the keylet for a "long" ledger-hash skip list page.
 *
 *  Each long skip list page stores hashes of up to 256 flag ledgers within
 *  a 65536-ledger range.  Together with the short skip list, any historical
 *  ledger can be located in at most two hops: one to the long skip list for
 *  the target range, one to the short skip list around the target ledger.
 *
 *  @param ledger  Any ledger index within the desired 65536-ledger range;
 *      only the upper 16 bits determine the page key.
 *  @return Keylet typed `ltLEDGER_HASHES` for the corresponding skip-list page.
 */
Keylet
skip(LedgerIndex ledger) noexcept;

/** Return the keylet for the singleton fee-settings object.
 *
 *  Its key is computed once and returned as a reference to a function-local
 *  static (Meyers singleton).
 *
 *  @return Reference to a static `Keylet` typed `ltFEE_SETTINGS`.
 */
Keylet const&
fees() noexcept;

/** Return the keylet for the singleton negative-UNL object.
 *
 *  Its key is computed once and returned as a reference to a function-local
 *  static (Meyers singleton).
 *
 *  @return Reference to a static `Keylet` typed `ltNEGATIVE_UNL`.
 */
Keylet const&
negativeUNL() noexcept;

/** Functor that returns the root keylet for an order book directory.
 *
 *  The returned keylet encodes quality 0 in the last 8 bytes of the key,
 *  making it the floor of the book's range in the SHAMap.  Use `kBOOK`
 *  (the pre-constructed singleton instance) rather than constructing directly.
 *
 *  @see keylet::quality
 */
struct BookT
{
    explicit BookT() = default;

    /** Return the keylet for the root directory page of @p b.
     *
     *  @param b  Order book specifying the in/out asset pair and optional domain.
     *  @return Keylet typed `ltDIR_NODE` with quality 0 in the last 8 bytes.
     */
    Keylet
    operator()(Book const& b) const;
};
static BookT const kBOOK{};

/** Return the keylet for a trust line (RippleState) between two accounts.
 *
 *  A trust line is a bilateral ledger object shared by both accounts.  The
 *  two account IDs are sorted before hashing so that `line(Alice, Bob, USD)`
 *  and `line(Bob, Alice, USD)` produce the same key.
 *
 *  @note `id0 == id1` is permitted (TrustSet may look up and delete malformed
 *      self-trust lines); the absence of a strict inequality assert is intentional.
 *
 *  @param id0       One account on the trust line.
 *  @param id1       The other account on the trust line.
 *  @param currency  Currency of the trust line.
 *  @return Keylet typed `ltRIPPLE_STATE`.
 */
/** @{ */
Keylet
line(AccountID const& id0, AccountID const& id1, Currency const& currency) noexcept;

/** Return the keylet for the trust line between @p id and the issuer of @p issue.
 *
 *  @param id     One of the two accounts on the trust line.
 *  @param issue  Issue whose account and currency identify the trust line.
 *  @return Keylet typed `ltRIPPLE_STATE`.
 */
inline Keylet
line(AccountID const& id, Issue const& issue) noexcept
{
    return line(id, issue.account, issue.currency);
}
/** @} */

/** Return the keylet for an offer placed by an account.
 *
 *  @param id   Account that placed the offer.
 *  @param seq  Sequence number of the OfferCreate transaction.
 *  @return Keylet typed `ltOFFER`.
 */
/** @{ */
Keylet
offer(AccountID const& id, std::uint32_t seq) noexcept;

/** Return a typed keylet for an offer from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit offer key.
 *  @return Keylet typed `ltOFFER`.
 */
inline Keylet
offer(uint256 const& key) noexcept
{
    return {ltOFFER, key};
}
/** @} */

/** Return the keylet for an order-book directory page at a specific quality.
 *
 *  Writes @p q as a big-endian 64-bit value into the last 8 bytes of the
 *  book's base key.  Because `uint256` keys sort as big-endian integers in
 *  the SHAMap, adjacent quality levels occupy adjacent addresses, enabling
 *  O(1) price-level iteration without a secondary index.
 *
 *  @param k  Base keylet for the order book (must be `ltDIR_NODE`).
 *  @param q  64-bit quality value (inverted exchange rate) to embed.
 *  @return Keylet typed `ltDIR_NODE` with @p q encoded in the last 8 bytes.
 */
Keylet
quality(Keylet const& k, std::uint64_t q) noexcept;

/** Functor that advances a book-directory keylet to the next quality level.
 *
 *  Adds a unit to the 64-bit quality field embedded in the last 8 bytes of
 *  the key, stepping to the directory for the next higher quality tier in
 *  the same order book.  Use `kNEXT` (the pre-constructed singleton instance)
 *  rather than constructing directly.
 *
 *  @see keylet::quality
 */
struct NextT
{
    explicit NextT() = default;

    /** Return the keylet for the next quality tier above @p k.
     *
     *  @param k  A directory keylet (must be `ltDIR_NODE`) whose last 8 bytes
     *      encode a quality value.
     *  @return Keylet typed `ltDIR_NODE` with quality incremented by 1.
     */
    Keylet
    operator()(Keylet const& k) const;
};
static NextT const kNEXT{};

/** Functor that computes ticket keylets.
 *
 *  Use `kTICKET` (the pre-constructed singleton instance) rather than
 *  constructing directly.
 */
struct TicketT
{
    explicit TicketT() = default;

    /** Return the keylet for a ticket owned by @p id.
     *
     *  @param id         Owner of the ticket.
     *  @param ticketSeq  Sequence number consumed when the ticket was created.
     *  @return Keylet typed `ltTICKET`.
     */
    Keylet
    operator()(AccountID const& id, std::uint32_t ticketSeq) const;

    /** Return the keylet for a ticket owned by @p id, resolved via a SeqProxy.
     *
     *  @param id         Owner of the ticket.
     *  @param ticketSeq  SeqProxy in ticket mode; asserts if it represents a
     *      plain sequence number.
     *  @return Keylet typed `ltTICKET`.
     */
    Keylet
    operator()(AccountID const& id, SeqProxy ticketSeq) const;

    /** Return a typed keylet for a ticket from its pre-computed key.
     *
     *  @param key  Pre-computed 256-bit ticket key.
     *  @return Keylet typed `ltTICKET`.
     */
    Keylet
    operator()(uint256 const& key) const
    {
        return {ltTICKET, key};
    }
};
static TicketT const kTICKET{};

/** Return the keylet for an account's multi-signature signer list.
 *
 *  @param account  Account whose signer list is being addressed.
 *  @return Keylet typed `ltSIGNER_LIST` for page 0 (the only allocated page).
 */
Keylet
signers(AccountID const& account) noexcept;

/** Return the keylet for a Check issued by an account.
 *
 *  @param id   Account that created the check (via CheckCreate).
 *  @param seq  Sequence number of the CheckCreate transaction.
 *  @return Keylet typed `ltCHECK`.
 */
/** @{ */
Keylet
check(AccountID const& id, std::uint32_t seq) noexcept;

/** Return a typed keylet for a check from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit check key.
 *  @return Keylet typed `ltCHECK`.
 */
inline Keylet
check(uint256 const& key) noexcept
{
    return {ltCHECK, key};
}
/** @} */

/** Return the keylet for a deposit pre-authorization record.
 *
 *  Two overloads exist for the two pre-authorization modes — account-to-account
 *  and credential-set — which hash under distinct namespace discriminators to
 *  prevent key collisions even when the `owner` is identical.
 */
/** @{ */
/** Return the keylet for a single-account deposit pre-authorization.
 *
 *  @param owner          Account granting the pre-authorization.
 *  @param preauthorized  Account being pre-authorized to deposit.
 *  @return Keylet typed `ltDEPOSIT_PREAUTH`.
 */
Keylet
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept;

/** Return the keylet for a credential-set deposit pre-authorization.
 *
 *  Each credential in @p authCreds is hashed individually as
 *  `sha512Half(issuer, credentialType)`; the resulting hashes are then passed
 *  to the outer hash under the `DepositPreauthCredentials` namespace, which
 *  is distinct from the account-to-account `DepositPreauth` namespace.
 *  Because `authCreds` is a `std::set`, iteration order is deterministic and
 *  the key is stable regardless of insertion order.
 *
 *  @param owner      Account granting the pre-authorization.
 *  @param authCreds  Sorted set of (issuer AccountID, credentialType) pairs.
 *  @return Keylet typed `ltDEPOSIT_PREAUTH`.
 */
Keylet
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept;

/** Return a typed keylet for a deposit pre-auth entry from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit deposit-preauth key.
 *  @return Keylet typed `ltDEPOSIT_PREAUTH`.
 */
inline Keylet
depositPreauth(uint256 const& key) noexcept
{
    return {ltDEPOSIT_PREAUTH, key};
}
/** @} */

//------------------------------------------------------------------------------

/** Return a keylet for any ledger entry without type enforcement.
 *
 *  Uses `ltANY` so `Keylet::check()` accepts any entry type.  Intended for
 *  low-level read paths that need to fetch an entry before its type is known.
 *
 *  @param key  Raw 256-bit ledger key.
 *  @return Keylet typed `ltANY`.
 */
Keylet
unchecked(uint256 const& key) noexcept;

/** Return the keylet for the root page of an account's owner directory.
 *
 *  The owner directory lists all objects owned by the account (offers, trust
 *  lines, escrows, etc.).  Subsequent pages beyond page 0 are keyed via
 *  `keylet::page`.
 *
 *  @param id  Account whose owner directory is being addressed.
 *  @return Keylet typed `ltDIR_NODE`.
 */
Keylet
ownerDir(AccountID const& id) noexcept;

/** Return the keylet for a specific page within a directory.
 *
 *  Page 0 is stored at the root key itself; pages 1+ are stored at keys
 *  derived by hashing the root key with the page index.
 *
 *  @param root   256-bit key of the directory's root page.
 *  @param index  Zero-based page index; 0 returns the root key unchanged.
 *  @return Keylet typed `ltDIR_NODE`.
 */
/** @{ */
Keylet
page(uint256 const& root, std::uint64_t index = 0) noexcept;

/** Return the keylet for a specific page within a directory, from a root keylet.
 *
 *  @param root   Keylet of the directory's root page (must be `ltDIR_NODE`).
 *  @param index  Zero-based page index.
 *  @return Keylet typed `ltDIR_NODE`.
 */
inline Keylet
page(Keylet const& root, std::uint64_t index = 0) noexcept
{
    XRPL_ASSERT(root.type == ltDIR_NODE, "xrpl::keylet::page : valid root type");
    return page(root.key, index);
}
/** @} */

/** Return the keylet for an escrow conditional payment.
 *
 *  @param src  Account that created the escrow.
 *  @param seq  Sequence number of the EscrowCreate transaction.
 *  @return Keylet typed `ltESCROW`.
 */
Keylet
escrow(AccountID const& src, std::uint32_t seq) noexcept;

/** Return the keylet for an XRP payment channel.
 *
 *  @param src  Funding (source) account.
 *  @param dst  Receiving (destination) account.
 *  @param seq  Sequence number of the PaymentChannelCreate transaction.
 *  @return Keylet typed `ltPAYCHAN`.
 */
Keylet
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept;

/** NFT page keylets.
 *
 *  Unlike other ledger objects whose keys are produced by hashing, NFT page
 *  keys are composite values: the high 160 bits hold the owner's `AccountID`
 *  and the low 96 bits are a range tag derived from an NFToken ID.  This
 *  composite structure enables bounded range scans over all of an owner's NFT
 *  pages in the SHAMap without a linked-list traversal.
 */
/** @{ */
/** Return the keylet for the owner's lowest possible NFT page (low 96 bits = 0).
 *
 *  This is the floor of the owner's page range.  It is normally impossible to
 *  create an actual NFT page at this key, but it is used in invariant tests
 *  to exercise the full page range.
 *
 *  @param owner  Account that owns the NFT collection.
 *  @return Keylet typed `ltNFTOKEN_PAGE` with low 96 bits all zero.
 */
Keylet
nftpageMin(AccountID const& owner);

/** Return the keylet for the owner's highest possible NFT page (low 96 bits = all ones).
 *
 *  Together with `nftpageMin`, this defines the closed interval covering every
 *  NFT page belonging to this owner.
 *
 *  @param owner  Account that owns the NFT collection.
 *  @return Keylet typed `ltNFTOKEN_PAGE` with low 96 bits all one.
 */
Keylet
nftpageMax(AccountID const& owner);

/** Return the keylet for the NFT page that should contain @p token.
 *
 *  Preserves the owner prefix from @p k (high 160 bits) and replaces the
 *  range tag (low 96 bits) with the corresponding bits of @p token masked
 *  by `nft::pageMask`.
 *
 *  @param k      An NFT page keylet for the same owner (must be `ltNFTOKEN_PAGE`).
 *  @param token  256-bit NFToken ID whose low 96 bits determine the target page.
 *  @return Keylet typed `ltNFTOKEN_PAGE` for the page whose range covers @p token.
 */
Keylet
nftpage(Keylet const& k, uint256 const& token);
/** @} */

/** Return the keylet for an NFToken buy or sell offer.
 *
 *  @param owner  Account that created the offer.
 *  @param seq    Sequence number of the NFTokenCreateOffer transaction.
 *  @return Keylet typed `ltNFTOKEN_OFFER`.
 */
/** @{ */
Keylet
nftoffer(AccountID const& owner, std::uint32_t seq);

/** Return a typed keylet for an NFToken offer from its pre-computed key.
 *
 *  @param offer  Pre-computed 256-bit NFToken offer key.
 *  @return Keylet typed `ltNFTOKEN_OFFER`.
 */
inline Keylet
nftoffer(uint256 const& offer)
{
    return {ltNFTOKEN_OFFER, offer};
}
/** @} */

/** Return the keylet for the directory of buy offers for an NFToken.
 *
 *  @param id  256-bit NFToken ID.
 *  @return Keylet typed `ltDIR_NODE` for the buy-offer directory.
 */
Keylet
nftBuys(uint256 const& id) noexcept;

/** Return the keylet for the directory of sell offers for an NFToken.
 *
 *  @param id  256-bit NFToken ID.
 *  @return Keylet typed `ltDIR_NODE` for the sell-offer directory.
 */
Keylet
nftSells(uint256 const& id) noexcept;

/** Return the keylet for an AMM pool, keyed by its two pooled assets.
 *
 *  The two assets are sorted via `std::minmax` before hashing, so
 *  `amm(A, B)` and `amm(B, A)` always produce the same keylet.
 *  All four combinations of `Issue`/`MPTIssue` asset pairs are supported.
 *
 *  @param issue1  One of the two pooled assets.
 *  @param issue2  The other pooled asset.
 *  @return Keylet typed `ltAMM`.
 */
/** @{ */
Keylet
amm(Asset const& issue1, Asset const& issue2) noexcept;

/** Return the keylet for an AMM pool from a pre-computed 256-bit AMM ID.
 *
 *  Use this overload when the AMM ID is already available (e.g. stored in
 *  `sfAMMID` on another SLE) to avoid redundant hashing.
 *
 *  @param amm  Pre-computed 256-bit AMM identifier.
 *  @return Keylet typed `ltAMM`.
 */
Keylet
amm(uint256 const& amm) noexcept;
/** @} */

/** Return the keylet for a delegation grant from one account to another.
 *
 *  @param account            Account granting delegated authority.
 *  @param authorizedAccount  Account receiving the delegated authority.
 *  @return Keylet typed `ltDELEGATE`.
 */
Keylet
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept;

/** Return the keylet for a cross-chain bridge object.
 *
 *  A door account may host multiple bridges.  The key encodes the door
 *  account and the currency appropriate to @p chainType, ensuring at most
 *  one bridge per currency per side.
 *
 *  @param bridge     Bridge descriptor containing door accounts and issues.
 *  @param chainType  Selects whether to key on the locking or issuing chain side.
 *  @return Keylet typed `ltBRIDGE`.
 */
Keylet
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType);

/** Return the keylet for a cross-chain claim ID.
 *
 *  The key encodes the full bridge identity plus the sequential claim ID
 *  stored as `sfXChainClaimID` in the object.
 *
 *  @param bridge  Bridge descriptor.
 *  @param seq     Sequential claim ID (`sfXChainClaimID`).
 *  @return Keylet typed `ltXCHAIN_OWNED_CLAIM_ID`.
 */
Keylet
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq);

/** Return the keylet for a cross-chain create-account claim ID.
 *
 *  Analogous to `xChainClaimID` but for the create-account workflow.  The
 *  sequential counter is stored as `sfXChainAccountCreateCount` in the object.
 *
 *  @param bridge  Bridge descriptor.
 *  @param seq     Sequential create-account claim ID (`sfXChainAccountCreateCount`).
 *  @return Keylet typed `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID`.
 */
Keylet
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq);

/** Return the keylet for an account's DID (Decentralized Identifier) document.
 *
 *  @param account  Account that owns the DID.
 *  @return Keylet typed `ltDID`.
 */
Keylet
did(AccountID const& account) noexcept;

/** Return the keylet for a price oracle owned by an account.
 *
 *  An account may own multiple oracles distinguished by unique document IDs.
 *
 *  @param account     Account that owns the oracle.
 *  @param documentID  Application-defined identifier distinguishing oracles
 *      within the same account (`sfOracleDocumentID`).
 *  @return Keylet typed `ltORACLE`.
 */
Keylet
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept;

/** Return the keylet for a verifiable credential.
 *
 *  @param subject   Account the credential was issued to.
 *  @param issuer    Account that issued the credential.
 *  @param credType  Application-defined credential type byte string.
 *  @return Keylet typed `ltCREDENTIAL`.
 */
/** @{ */
Keylet
credential(AccountID const& subject, AccountID const& issuer, Slice const& credType) noexcept;

/** Return a typed keylet for a credential from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit credential key.
 *  @return Keylet typed `ltCREDENTIAL`.
 */
inline Keylet
credential(uint256 const& key) noexcept
{
    return {ltCREDENTIAL, key};
}
/** @} */

/** Return the keylet for an MPT issuance, identified by sequence and issuer.
 *
 *  Constructs the `MPTID` from @p seq and @p issuer via `makeMptID`, then
 *  delegates to the `MPTID` overload.
 *
 *  @param seq     Issuer's account sequence number at the time of issuance creation.
 *  @param issuer  Account that created the issuance.
 *  @return Keylet typed `ltMPTOKEN_ISSUANCE`.
 */
/** @{ */
Keylet
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept;

/** Return the keylet for an MPT issuance from a pre-built MPTID.
 *
 *  @param issuanceID  192-bit MPT issuance identifier (see `makeMptID`).
 *  @return Keylet typed `ltMPTOKEN_ISSUANCE`.
 */
Keylet
mptIssuance(MPTID const& issuanceID) noexcept;

/** Return a typed keylet for an MPT issuance from its pre-computed key.
 *
 *  @param issuanceKey  Pre-computed 256-bit issuance key.
 *  @return Keylet typed `ltMPTOKEN_ISSUANCE`.
 */
inline Keylet
mptIssuance(uint256 const& issuanceKey)
{
    return {ltMPTOKEN_ISSUANCE, issuanceKey};
}
/** @} */

/** Return the keylet for a holder's MPToken balance entry.
 *
 *  MPToken entries are keyed under the `MPToken` namespace by hashing the
 *  issuance's 256-bit ledger key together with the holder's `AccountID`.
 *  This naturally groups all token balances under their issuance in the
 *  SHAMap hash space.
 *
 *  @param issuanceID  192-bit MPTID identifying the issuance.
 *  @param holder      Account holding the MPToken balance.
 *  @return Keylet typed `ltMPTOKEN`.
 */
/** @{ */
Keylet
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept;

/** Return a typed keylet for an MPToken entry from its pre-computed key.
 *
 *  @param mptokenKey  Pre-computed 256-bit MPToken key.
 *  @return Keylet typed `ltMPTOKEN`.
 */
inline Keylet
mptoken(uint256 const& mptokenKey)
{
    return {ltMPTOKEN, mptokenKey};
}

/** Return the keylet for a holder's MPToken balance entry, identified by issuance key.
 *
 *  Use this overload when the issuance's 256-bit ledger key is already available
 *  to avoid redundant hashing through `makeMptID` and `mptIssuance`.
 *
 *  @param issuanceKey  256-bit key of the `MPTokenIssuance` SLE.
 *  @param holder       Account holding the MPToken balance.
 *  @return Keylet typed `ltMPTOKEN`.
 */
Keylet
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept;
/** @} */

/** Return the keylet for a single-asset vault.
 *
 *  @param owner  Account that created the vault.
 *  @param seq    Sequence number of the VaultCreate transaction.
 *  @return Keylet typed `ltVAULT`.
 */
/** @{ */
Keylet
vault(AccountID const& owner, std::uint32_t seq) noexcept;

/** Return a typed keylet for a vault from its pre-computed key.
 *
 *  @param vaultKey  Pre-computed 256-bit vault key.
 *  @return Keylet typed `ltVAULT`.
 */
inline Keylet
vault(uint256 const& vaultKey)
{
    return {ltVAULT, vaultKey};
}
/** @} */

/** Return the keylet for a loan broker created by an account.
 *
 *  @param owner  Account that created the loan broker.
 *  @param seq    Sequence number of the LoanBrokerCreate transaction.
 *  @return Keylet typed `ltLOAN_BROKER`.
 */
/** @{ */
Keylet
loanbroker(AccountID const& owner, std::uint32_t seq) noexcept;

/** Return a typed keylet for a loan broker from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit loan broker key.
 *  @return Keylet typed `ltLOAN_BROKER`.
 */
inline Keylet
loanbroker(uint256 const& key)
{
    return {ltLOAN_BROKER, key};
}
/** @} */

/** Return the keylet for an individual loan issued by a loan broker.
 *
 *  @param loanBrokerID  256-bit key of the parent `LoanBroker` SLE.
 *  @param loanSeq       Sequential loan number assigned by the broker.
 *  @return Keylet typed `ltLOAN`.
 */
/** @{ */
Keylet
loan(uint256 const& loanBrokerID, std::uint32_t loanSeq) noexcept;

/** Return a typed keylet for a loan from its pre-computed key.
 *
 *  @param key  Pre-computed 256-bit loan key.
 *  @return Keylet typed `ltLOAN`.
 */
inline Keylet
loan(uint256 const& key)
{
    return {ltLOAN, key};
}
/** @} */

/** Return the keylet for a permissioned domain owned by an account.
 *
 *  @param account  Account that created the permissioned domain.
 *  @param seq      Sequence number of the PermissionedDomainSet transaction.
 *  @return Keylet typed `ltPERMISSIONED_DOMAIN`.
 */
/** @{ */
Keylet
permissionedDomain(AccountID const& account, std::uint32_t seq) noexcept;

/** Return the keylet for a permissioned domain from its pre-computed ID.
 *
 *  Use this overload when the domain ID is already known (e.g. stored in
 *  `sfDomainID` on another SLE) to avoid recomputing the hash.
 *
 *  @param domainID  Pre-computed 256-bit domain key.
 *  @return Keylet typed `ltPERMISSIONED_DOMAIN`.
 */
Keylet
permissionedDomain(uint256 const& domainID) noexcept;
/** @} */

}  // namespace keylet

// Everything below is deprecated and should be removed in favor of keylets:

/** Return the base 256-bit key for an order book directory (deprecated).
 *
 *  The returned key has quality 0 embedded in its last 8 bytes.  Prefer
 *  `keylet::kBOOK(book)` for new code, which wraps this result in a typed keylet.
 *
 *  @param book  Order book identifying the in/out asset pair and optional domain.
 *  @return Raw 256-bit key for the book's root directory.
 *  @deprecated Use `keylet::kBOOK(book)` instead.
 */
uint256
getBookBase(Book const& book);

/** Advance a book-directory key to the next quality level (deprecated).
 *
 *  Adds a unit to the 64-bit quality field embedded in the last 8 bytes of
 *  @p uBase.  Prefer `keylet::kNEXT(k)` for new code.
 *
 *  @param uBase  A book-directory key, typically from `getBookBase`.
 *  @return Key with quality incremented by 1.
 *  @deprecated Use `keylet::kNEXT(k)` instead.
 */
uint256
getQualityNext(uint256 const& uBase);

/** Extract the 64-bit quality value from a book-directory key (deprecated).
 *
 *  Reads the last 8 bytes of @p uBase as a big-endian `uint64_t`, exploiting
 *  `base_uint`'s big-endian internal layout.
 *
 *  @param uBase  A book-directory key produced by `getBookBase` or `keylet::quality`.
 *  @return The 64-bit quality (inverted exchange rate) embedded in the key.
 *  @deprecated Callers should use `keylet::quality` for construction instead.
 */
// VFALCO This name could be better
std::uint64_t
getQuality(uint256 const& uBase);

/** Return the 256-bit ledger key for a ticket (deprecated).
 *
 *  @param account    Owner of the ticket.
 *  @param uSequence  Sequence number consumed when the ticket was created.
 *  @return Raw key under the `Ticket` namespace.
 *  @deprecated Use `keylet::kTICKET(account, seq)` instead.
 */
uint256
getTicketIndex(AccountID const& account, std::uint32_t uSequence);

/** Return the 256-bit ledger key for a ticket from a SeqProxy (deprecated).
 *
 *  @param account    Owner of the ticket.
 *  @param ticketSeq  SeqProxy in ticket mode.
 *  @return Raw key under the `Ticket` namespace.
 *  @deprecated Use `keylet::kTICKET(account, ticketSeq)` instead.
 */
uint256
getTicketIndex(AccountID const& account, SeqProxy ticketSeq);

/** Descriptor binding a keylet factory to its expected ledger-entry type name.
 *
 *  Used exclusively by invariant tests (`Invariants_test.cpp`) to enumerate
 *  keylet functions and verify that the objects they address have the correct
 *  ledger entry type.  Not part of the production ledger-access API.
 *
 *  @tparam KeyletParams  Parameter types of the wrapped keylet factory function.
 */
template <class... KeyletParams>
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct KeyletDesc
{
    /** Keylet factory function for one ledger object type. */
    std::function<Keylet(KeyletParams...)> function;
    /** Expected `LedgerEntryType` name as a JSON static string, used to
     *  validate the type of the SLE retrieved at the computed key. */
    json::StaticString expectedLEName;
    /** Whether to include this keylet in invariant test coverage. */
    bool includeInTests{};
};

/** All keylet functions that accept a single `AccountID` parameter.
 *
 *  This array drives invariant tests that verify the ledger-entry type of
 *  the object addressed by each keylet function.  When adding a new single-
 *  `AccountID` keylet, add an entry here so invariant tests automatically
 *  exercise it.
 *
 *  @note `nftpageMin` is listed even though creating an actual NFT page at
 *      that key is normally impossible — the invariant checker tests for it
 *      regardless.
 */
std::array<KeyletDesc<AccountID const&>, 6> const kDIRECT_ACCOUNT_KEYLETS{
    {{.function = &keylet::account, .expectedLEName = jss::AccountRoot, .includeInTests = false},
     {.function = &keylet::ownerDir, .expectedLEName = jss::DirectoryNode, .includeInTests = true},
     {.function = &keylet::signers, .expectedLEName = jss::SignerList, .includeInTests = true},
     // It's normally impossible to create an item at nftpage_min, but
     // test it anyway, since the invariant checks for it.
     {.function = &keylet::nftpageMin, .expectedLEName = jss::NFTokenPage, .includeInTests = true},
     {.function = &keylet::nftpageMax, .expectedLEName = jss::NFTokenPage, .includeInTests = true},
     {.function = &keylet::did, .expectedLEName = jss::DID, .includeInTests = true}}};

/** Construct a 192-bit MPT issuance identifier from a sequence number and issuer.
 *
 *  Packs a big-endian 32-bit @p sequence into the first 4 bytes of the `MPTID`,
 *  followed by the 20-byte @p account.  The explicit endian conversion ensures
 *  canonical byte order for on-ledger storage and byte-by-byte comparison.
 *
 *  @param sequence  The issuer's account sequence number at issuance creation
 *      (stored as `sfSequence` in the `MPTokenIssuance` SLE).
 *  @param account   The issuing account.
 *  @return 192-bit `MPTID` uniquely addressing this MPT issuance.
 */
MPTID
makeMptID(std::uint32_t sequence, AccountID const& account);

}  // namespace xrpl
