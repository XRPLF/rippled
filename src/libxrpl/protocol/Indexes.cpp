/** @file
 *  Implements all ledger-index (keylet) factory functions for the XRP Ledger.
 *
 *  Every object stored in the ledger state map is addressed by a 256-bit key.
 *  This file is the single authoritative source for deriving those keys.
 *  All derivations use "tagged hashing" via `indexHash`: a `sha512Half` over
 *  a type-specific `LedgerNameSpace` discriminator followed by the object's
 *  identifying parameters, preventing cross-type key collisions even when
 *  two object types happen to share the same parameter values.
 *
 *  The public surface is the `keylet::` namespace, whose functions return
 *  `Keylet` values pairing a key with its expected `LedgerEntryType`.
 *  Free functions (`getBookBase`, `getQuality`, etc.) are deprecated
 *  lower-level utilities retained for compatibility.
 */
#include <xrpl/protocol/Indexes.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/nftPageMask.h>

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl {

/** Type-specific prefix for calculating ledger indices.

    The identifier for a given object within the ledger is calculated based
    on some object-specific parameters. To ensure that different types of
    objects have different indices, even if they happen to use the same set
    of parameters, we use "tagged hashing" by adding a type-specific prefix.

    @note These values are part of the protocol and *CANNOT* be arbitrarily
          changed. If they were, on-ledger objects may no longer be able to
          be located or addressed.

          Additions to this list are OK, but changing existing entries to
          assign them a different values should never be needed.

          Entries that are removed should be moved to the bottom of the enum
          and marked as [[deprecated]] to prevent accidental reuse.
*/
enum class LedgerNameSpace : std::uint16_t {
    Account = 'a',             /**< AccountRoot entry. */
    DirNode = 'd',             /**< Interior directory page (index > 0). */
    TrustLine = 'r',           /**< RippleState (trust line) between two accounts. */
    Offer = 'o',               /**< Offer placed by an account. */
    OwnerDir = 'O',            /**< Root page of an account's owner directory. */
    BookDir = 'B',             /**< Order-book directory for a currency pair. */
    SkipList = 's',            /**< Ledger skip list (short or long form). */
    Escrow = 'u',              /**< Escrow conditional payment. */
    Amendments = 'f',          /**< Singleton amendments table. */
    FeeSettings = 'e',         /**< Singleton fee-settings object. */
    Ticket = 'T',              /**< Ticket (sequence placeholder). */
    SignerList = 'S',          /**< Multi-signature signer list. */
    XRPPaymentChannel = 'x',   /**< XRP payment channel. */
    Check = 'C',               /**< Check (deferred payment). */
    DepositPreauth = 'p',      /**< Single-account deposit pre-authorization. */
    DepositPreauthCredentials = 'P', /**< Credential-set deposit pre-authorization. */
    NegativeUnl = 'N',         /**< Singleton negative UNL object. */
    NftokenOffer = 'q',        /**< NFToken buy or sell offer. */
    NftokenBuyOffers = 'h',    /**< Directory of buy offers for a given NFToken. */
    NftokenSellOffers = 'i',   /**< Directory of sell offers for a given NFToken. */
    Amm = 'A',                 /**< Automated market maker pool. */
    Bridge = 'H',              /**< Cross-chain bridge. */
    XchainClaimId = 'Q',       /**< Cross-chain claim ID. */
    XchainCreateAccountClaimId = 'K', /**< Cross-chain create-account claim ID. */
    Did = 'I',                 /**< Decentralized identifier (DID) document. */
    Oracle = 'R',              /**< Price oracle. */
    MPTokenIssuance = '~',     /**< Multi-purpose token issuance. */
    MPToken = 't',             /**< Individual holder's MPToken balance entry. */
    Credential = 'D',          /**< Verifiable credential. */
    PermissionedDomain = 'm',  /**< Permissioned DEX domain. */
    Delegate = 'E',            /**< Account delegation grant. */
    Vault = 'V',               /**< Single-asset vault. */
    LoanBroker = 'l',          /**< Loan broker (lower-case L to distinguish from Loan). */
    Loan = 'L',                /**< Individual loan issued by a loan broker. */

    // No longer used or supported. Left here to reserve the space to avoid accidental reuse.
    Contract [[deprecated]] = 'c',   /**< @deprecated Legacy contract entry; reserved to prevent reuse. */
    Generator [[deprecated]] = 'g',  /**< @deprecated Legacy generator entry; reserved to prevent reuse. */
    Nickname [[deprecated]] = 'n',   /**< @deprecated Legacy nickname entry; reserved to prevent reuse. */
};

/** Derive a ledger-entry key using tagged hashing.
 *
 *  Prepends the `uint16_t` representation of @p space to @p args and returns
 *  `sha512Half` of the concatenation.  The namespace discriminator ensures
 *  that two different object types that share the same parameters still
 *  produce distinct keys.
 *
 *  @tparam Args  Serialisable argument types forwarded to `sha512Half`.
 *  @param space  Namespace tag identifying the ledger-entry type.
 *  @param args   Type-specific parameters that uniquely identify the object.
 *  @return 256-bit key for the ledger entry.
 */
template <class... Args>
static uint256
indexHash(LedgerNameSpace space, Args const&... args)
{
    return sha512Half(safeCast<std::uint16_t>(space), args...);
}

/** Compute the base key for an order book directory.
 *
 *  Dispatches over all four combinations of `Issue`/`MPTIssue` asset pairs
 *  using `if constexpr`, hashing the relevant currency and account/MPTID
 *  fields under the `BookDir` namespace.  If `book.domain` is set the domain
 *  ID is appended to the hash inputs, producing a permissioned-DEX book key.
 *  The returned key has quality 0 embedded in the last 8 bytes (see
 *  `keylet::quality`), making it the lowest possible key for that book.
 *
 *  @param book  Order book specifying the in/out asset pair and optional domain.
 *  @return 256-bit base key for the book's directory with quality field = 0.
 */
uint256
getBookBase(Book const& book)
{
    XRPL_ASSERT(isConsistent(book), "xrpl::getBookBase : input is consistent");

    auto getIndexHash = [&book]<typename... Args>(Args... args) {
        if (book.domain)
            return indexHash(std::forward<Args>(args)..., *book.domain);
        return indexHash(std::forward<Args>(args)...);
    };

    auto const index = std::visit(
        [&]<ValidIssueType TIn, ValidIssueType TOut>(TIn const& in, TOut const& out) {
            if constexpr (std::is_same_v<TIn, Issue> && std::is_same_v<TOut, Issue>)
            {
                return getIndexHash(
                    LedgerNameSpace::BookDir, in.currency, out.currency, in.account, out.account);
            }
            else if constexpr (std::is_same_v<TIn, Issue> && std::is_same_v<TOut, MPTIssue>)
            {
                return getIndexHash(
                    LedgerNameSpace::BookDir, in.currency, out.getMptID(), in.account);
            }
            else if constexpr (std::is_same_v<TIn, MPTIssue> && std::is_same_v<TOut, Issue>)
            {
                return getIndexHash(
                    LedgerNameSpace::BookDir, in.getMptID(), out.currency, out.account);
            }
            else
            {
                return getIndexHash(LedgerNameSpace::BookDir, in.getMptID(), out.getMptID());
            }
        },
        book.in.value(),
        book.out.value());

    // Return with quality 0.
    auto k = keylet::quality({ltDIR_NODE, index}, 0);

    return k.key;
}

/** Advance a book-directory key to the next quality level.
 *
 *  Adds a unit to the 64-bit quality field embedded in the last 8 bytes of
 *  @p uBase.  Because the quality is stored in big-endian byte order at the
 *  tail of the `uint256`, adding `kNEXT_QUALITY` (which is `1 << 64` in the
 *  256-bit space) correctly increments just that field without touching the
 *  book-identifying prefix.  The result is the smallest key belonging to the
 *  next quality tier in the same book.
 *
 *  @param uBase  A book-directory key, typically the return value of
 *      `getBookBase`.
 *  @return Key with quality incremented by 1.
 */
uint256
getQualityNext(uint256 const& uBase)
{
    static constexpr uint256 kNEXT_QUALITY(
        "0000000000000000000000000000000000000000000000010000000000000000");
    return uBase + kNEXT_QUALITY;
}

/** Extract the 64-bit quality value from a book-directory key.
 *
 *  Reads the last 8 bytes of @p uBase as a big-endian `uint64_t`.  This
 *  relies on `base_uint`'s internal big-endian byte layout, which puts the
 *  most-significant bytes first so that the tail of the array holds the
 *  least-significant (quality) bits of the full 256-bit value.
 *
 *  @param uBase  A book-directory key produced by `getBookBase` or
 *      `keylet::quality`.
 *  @return The 64-bit quality (exchange rate) embedded in the key.
 */
std::uint64_t
getQuality(uint256 const& uBase)
{
    // VFALCO [base_uint] This assumes a certain storage format
    return boost::endian::big_to_native(((std::uint64_t*)uBase.end())[-1]);
}

/** Compute the 256-bit key for a ticket ledger entry.
 *
 *  @param account    Owner of the ticket.
 *  @param ticketSeq  Sequence number that was consumed when the ticket was
 *      created.
 *  @return Key derived from account and sequence under the `Ticket` namespace.
 */
uint256
getTicketIndex(AccountID const& account, std::uint32_t ticketSeq)
{
    return indexHash(LedgerNameSpace::Ticket, account, ticketSeq);
}

/** Compute the 256-bit key for a ticket ledger entry from a `SeqProxy`.
 *
 *  @param account    Owner of the ticket.
 *  @param ticketSeq  A `SeqProxy` in ticket mode; asserts if it represents a
 *      sequence number.
 *  @return Key derived from account and the proxy's value under the `Ticket`
 *      namespace.
 */
uint256
getTicketIndex(AccountID const& account, SeqProxy ticketSeq)
{
    XRPL_ASSERT(ticketSeq.isTicket(), "xrpl::getTicketIndex : valid input");
    return getTicketIndex(account, ticketSeq.value());
}

/** Construct a 192-bit MPT issuance identifier from a sequence and account.
 *
 *  Packs a big-endian 32-bit sequence number into the first 4 bytes of the
 *  MPTID, followed immediately by the 20-byte `AccountID`.  The explicit
 *  `native_to_big` conversion ensures canonical byte order regardless of host
 *  endianness, which is required because the composite value is stored on-ledger
 *  and compared byte-by-byte.
 *
 *  @param sequence  The issuer's account sequence number at creation time
 *      (stored as `sfSequence` in the `MPTokenIssuance` SLE).
 *  @param account   The issuing account.
 *  @return 192-bit identifier uniquely addressing this MPT issuance.
 */
MPTID
makeMptID(std::uint32_t sequence, AccountID const& account)
{
    MPTID u;
    sequence = boost::endian::native_to_big(sequence);
    memcpy(u.data(), &sequence, sizeof(sequence));
    memcpy(u.data() + sizeof(sequence), account.data(), sizeof(account));
    return u;
}

//------------------------------------------------------------------------------

namespace keylet {

/** Return the keylet for an AccountRoot ledger entry.
 *
 *  @param id  The account address.
 *  @return Keylet typed `ltACCOUNT_ROOT` whose key is the account's ledger index.
 */
Keylet
account(AccountID const& id) noexcept
{
    return Keylet{ltACCOUNT_ROOT, indexHash(LedgerNameSpace::Account, id)};
}

/** Return a wildcard keylet for any entry that may appear in an owner directory.
 *
 *  Uses `ltCHILD` so that `Keylet::check()` accepts any entry type — useful
 *  when iterating a directory without knowing the entry type in advance.
 *
 *  @param key  Raw 256-bit ledger key.
 *  @return Keylet typed `ltCHILD` wrapping the provided key.
 */
Keylet
child(uint256 const& key) noexcept
{
    return {ltCHILD, key};
}

/** Return the keylet for the "short" ledger-hash skip list.
 *
 *  The short skip list is a singleton ledger object that holds the hashes of
 *  the most recent ledgers since the last flag ledger (at most 256 entries).
 *  Because it has no parameters its key is computed once and cached.
 *
 *  @return Reference to a static `Keylet` typed `ltLEDGER_HASHES`.
 */
Keylet const&
skip() noexcept
{
    static Keylet const kRET{ltLEDGER_HASHES, indexHash(LedgerNameSpace::SkipList)};
    return kRET;
}

/** Return the keylet for a "long" ledger-hash skip list page.
 *
 *  Each long skip list page covers a range of 65536 ledgers identified by
 *  the top 16 bits of the ledger sequence.  Together the short and long skip
 *  lists allow any historical ledger to be located in at most two hops.
 *
 *  @param ledger  Any ledger index within the desired 65536-ledger range;
 *      only the upper 16 bits are used to derive the key.
 *  @return Keylet typed `ltLEDGER_HASHES` for the corresponding skip-list page.
 */
Keylet
skip(LedgerIndex ledger) noexcept
{
    return {
        ltLEDGER_HASHES,
        indexHash(
            LedgerNameSpace::SkipList, std::uint32_t(static_cast<std::uint32_t>(ledger) >> 16))};
}

/** Return the keylet for the singleton amendments table.
 *
 *  The amendments object is globally unique; its key is computed once and
 *  cached.  The `const&` return type signals that this is a fixed address.
 *
 *  @return Reference to a static `Keylet` typed `ltAMENDMENTS`.
 */
Keylet const&
amendments() noexcept
{
    static Keylet const kRET{ltAMENDMENTS, indexHash(LedgerNameSpace::Amendments)};
    return kRET;
}

/** Return the keylet for the singleton fee-settings object.
 *
 *  The fee object is globally unique; its key is computed once and cached.
 *
 *  @return Reference to a static `Keylet` typed `ltFEE_SETTINGS`.
 */
Keylet const&
fees() noexcept
{
    static Keylet const kRET{ltFEE_SETTINGS, indexHash(LedgerNameSpace::FeeSettings)};
    return kRET;
}

/** Return the keylet for the singleton negative-UNL object.
 *
 *  The negative UNL is globally unique; its key is computed once and cached.
 *
 *  @return Reference to a static `Keylet` typed `ltNEGATIVE_UNL`.
 */
Keylet const&
negativeUNL() noexcept
{
    static Keylet const kRET{ltNEGATIVE_UNL, indexHash(LedgerNameSpace::NegativeUnl)};
    return kRET;
}

/** Return the keylet for the root directory page of an order book.
 *
 *  Delegates to `getBookBase` and wraps the result in an `ltDIR_NODE` keylet.
 *  The key encodes quality 0 in its last 8 bytes, placing it before all
 *  real offer-directory pages in the SHAMap.
 *
 *  @param b  The order book (in/out asset pair plus optional domain).
 *  @return Keylet typed `ltDIR_NODE` for the book's root directory.
 */
Keylet
BookT::operator()(Book const& b) const
{
    return {ltDIR_NODE, getBookBase(b)};
}

/** Return the keylet for a trust line (RippleState) between two accounts.
 *
 *  A trust line is a bilateral relationship; the same on-ledger object is
 *  referenced regardless of which account is treated as "issuer" or "holder".
 *  To achieve this, the two account IDs are sorted in ascending order before
 *  hashing, producing an order-independent key.
 *
 *  @note TrustSet may call this with `id0 == id1` to locate and delete
 *      malformed self-trust lines; the expected `id0 != id1` invariant is
 *      therefore not asserted here.
 *
 *  @param id0       One of the two accounts on the trust line.
 *  @param id1       The other account on the trust line.
 *  @param currency  The currency of the trust line.
 *  @return Keylet typed `ltRIPPLE_STATE`.
 */
Keylet
line(AccountID const& id0, AccountID const& id1, Currency const& currency) noexcept
{
    auto const accounts = std::minmax(id0, id1);

    return {
        ltRIPPLE_STATE,
        indexHash(LedgerNameSpace::TrustLine, accounts.first, accounts.second, currency)};
}

/** Return the keylet for a specific offer placed by an account.
 *
 *  @param id   Account that placed the offer.
 *  @param seq  Sequence number of the offer transaction.
 *  @return Keylet typed `ltOFFER`.
 */
Keylet
offer(AccountID const& id, std::uint32_t seq) noexcept
{
    return {ltOFFER, indexHash(LedgerNameSpace::Offer, id, seq)};
}

/** Return the keylet for an order-book directory page at a specific quality.
 *
 *  Embeds @p q as a big-endian 64-bit value in the last 8 bytes of the
 *  book's base key.  Because `uint256` values are stored most-significant-
 *  byte first, incrementing the embedded quality field (via `getQualityNext`)
 *  advances to the next directory page in natural SHAMap sort order.
 *
 *  @note The raw pointer arithmetic here relies on `base_uint`'s internal
 *      big-endian byte layout.
 *
 *  @param k  Base keylet for the order book (must be `ltDIR_NODE`).
 *  @param q  64-bit quality value (inverted exchange rate) to embed.
 *  @return Keylet typed `ltDIR_NODE` with quality encoded in the last 8 bytes.
 */
Keylet
quality(Keylet const& k, std::uint64_t q) noexcept
{
    XRPL_ASSERT(k.type == ltDIR_NODE, "xrpl::keylet::quality : valid input type");

    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first and the least significant bytes
    // represent adjacent entries. We place the quality, in big endian format,
    // in the 8 right most bytes; this way, incrementing goes to the next entry
    // for indexes.
    uint256 x = k.key;

    // FIXME This is ugly and we can and should do better...
    ((std::uint64_t*)x.end())[-1] = boost::endian::native_to_big(q);

    return {ltDIR_NODE, x};
}

/** Return the keylet for the next lower quality tier of an order-book directory.
 *
 *  @param k  A directory keylet (must be `ltDIR_NODE`) whose last 8 bytes
 *      encode a quality value.
 *  @return Keylet typed `ltDIR_NODE` with quality incremented by 1.
 */
Keylet
NextT::operator()(Keylet const& k) const
{
    XRPL_ASSERT(k.type == ltDIR_NODE, "xrpl::keylet::NextT::operator() : valid input type");
    return {ltDIR_NODE, getQualityNext(k.key)};
}

/** Return the keylet for a ticket owned by @p id with sequence @p ticketSeq.
 *
 *  @param id         Owner of the ticket.
 *  @param ticketSeq  Sequence number consumed when the ticket was created.
 *  @return Keylet typed `ltTICKET`.
 */
Keylet
TicketT::operator()(AccountID const& id, std::uint32_t ticketSeq) const
{
    return {ltTICKET, getTicketIndex(id, ticketSeq)};
}

/** Return the keylet for a ticket owned by @p id, resolved from a `SeqProxy`.
 *
 *  @param id         Owner of the ticket.
 *  @param ticketSeq  SeqProxy in ticket mode; asserts if it represents a plain sequence.
 *  @return Keylet typed `ltTICKET`.
 */
Keylet
TicketT::operator()(AccountID const& id, SeqProxy ticketSeq) const
{
    return {ltTICKET, getTicketIndex(id, ticketSeq)};
}

/** Return the keylet for a specific page of a signer list (internal helper).
 *
 *  Currently only page 0 is ever allocated.  This paginated overload is kept
 *  `static` as an architectural reservation: if signer-list pagination is
 *  ever supported, the infrastructure to derive per-page keylets is already
 *  in place, but the interface is intentionally not exposed until then.
 *
 *  @param account  Account whose signer list is being addressed.
 *  @param page     Page number (always 0 in current protocol).
 *  @return Keylet typed `ltSIGNER_LIST`.
 */
static Keylet
signers(AccountID const& account, std::uint32_t page) noexcept
{
    return {ltSIGNER_LIST, indexHash(LedgerNameSpace::SignerList, account, page)};
}

/** Return the keylet for an account's signer list.
 *
 *  @param account  Account whose signer list is being addressed.
 *  @return Keylet typed `ltSIGNER_LIST` for page 0.
 */
Keylet
signers(AccountID const& account) noexcept
{
    return signers(account, 0);
}

/** Return the keylet for a Check issued by an account.
 *
 *  @param id   Account that created the check.
 *  @param seq  Sequence number of the CheckCreate transaction.
 *  @return Keylet typed `ltCHECK`.
 */
Keylet
check(AccountID const& id, std::uint32_t seq) noexcept
{
    return {ltCHECK, indexHash(LedgerNameSpace::Check, id, seq)};
}

/** Return the keylet for a single-account deposit pre-authorization.
 *
 *  @param owner          Account granting the pre-authorization.
 *  @param preauthorized  Account being pre-authorized to deposit.
 *  @return Keylet typed `ltDEPOSIT_PREAUTH` under the `DepositPreauth`
 *      namespace (distinct from the credential-set overload).
 */
Keylet
depositPreauth(AccountID const& owner, AccountID const& preauthorized) noexcept
{
    return {ltDEPOSIT_PREAUTH, indexHash(LedgerNameSpace::DepositPreauth, owner, preauthorized)};
}

/** Return the keylet for a credential-set deposit pre-authorization.
 *
 *  Each credential is hashed individually as `sha512Half(issuer, credentialType)`,
 *  and the resulting hashes are passed as a vector to the outer `indexHash`.
 *  Because `authCreds` is a `std::set` the iteration order is deterministic,
 *  so the final hash is stable regardless of insertion order.
 *
 *  The `DepositPreauthCredentials` namespace is distinct from `DepositPreauth`,
 *  preventing key collisions with the single-account overload even when the
 *  `owner` parameter is identical.
 *
 *  @note Pass credentials pre-sorted via `credentials::makeSorted` to satisfy
 *      the `std::set` ordering requirement.
 *
 *  @param owner      Account granting the pre-authorization.
 *  @param authCreds  Sorted set of (issuer AccountID, credentialType) pairs.
 *  @return Keylet typed `ltDEPOSIT_PREAUTH` under the `DepositPreauthCredentials`
 *      namespace.
 */
Keylet
depositPreauth(
    AccountID const& owner,
    std::set<std::pair<AccountID, Slice>> const& authCreds) noexcept
{
    std::vector<uint256> hashes;
    hashes.reserve(authCreds.size());
    for (auto const& o : authCreds)
        hashes.emplace_back(sha512Half(o.first, o.second));

    return {
        ltDEPOSIT_PREAUTH, indexHash(LedgerNameSpace::DepositPreauthCredentials, owner, hashes)};
}

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
unchecked(uint256 const& key) noexcept
{
    return {ltANY, key};
}

/** Return the keylet for the root page of an account's owner directory.
 *
 *  The owner directory lists all ledger objects owned by the account
 *  (offers, trust lines, escrows, etc.).  Page 0 is keyed directly;
 *  subsequent pages are keyed via `keylet::page`.
 *
 *  @param id  Account whose owner directory is being addressed.
 *  @return Keylet typed `ltDIR_NODE`.
 */
Keylet
ownerDir(AccountID const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::OwnerDir, id)};
}

/** Return the keylet for a specific page within a directory.
 *
 *  Page 0 is stored at the root key itself; subsequent pages are stored at
 *  keys derived by hashing the root key with the page index under the
 *  `DirNode` namespace.
 *
 *  @param key    256-bit key of the directory's root page.
 *  @param index  Zero-based page index; 0 returns the root key unchanged.
 *  @return Keylet typed `ltDIR_NODE`.
 */
Keylet
page(uint256 const& key, std::uint64_t index) noexcept
{
    if (index == 0)
        return {ltDIR_NODE, key};

    return {ltDIR_NODE, indexHash(LedgerNameSpace::DirNode, key, index)};
}

/** Return the keylet for an escrow created by @p src.
 *
 *  @param src  Account that created the escrow.
 *  @param seq  Sequence number of the EscrowCreate transaction.
 *  @return Keylet typed `ltESCROW`.
 */
Keylet
escrow(AccountID const& src, std::uint32_t seq) noexcept
{
    return {ltESCROW, indexHash(LedgerNameSpace::Escrow, src, seq)};
}

/** Return the keylet for an XRP payment channel.
 *
 *  @param src  Funding (source) account.
 *  @param dst  Receiving (destination) account.
 *  @param seq  Sequence number of the PaymentChannelCreate transaction.
 *  @return Keylet typed `ltPAYCHAN`.
 */
Keylet
payChan(AccountID const& src, AccountID const& dst, std::uint32_t seq) noexcept
{
    return {ltPAYCHAN, indexHash(LedgerNameSpace::XRPPaymentChannel, src, dst, seq)};
}

/** Return the keylet for an owner's lowest possible NFT page.
 *
 *  NFT page keys are composite values: the high 160 bits hold the owner's
 *  `AccountID`; the low 96 bits are a range tag derived from an NFToken ID.
 *  The minimum keylet has the low 96 bits set to zero, making it the floor
 *  of the owner's page range in the SHAMap.
 *
 *  @note NFT pages are NOT located by hashing — the key is constructed
 *      directly as a composite, enabling bounded range scans over all of an
 *      owner's pages without a linked-list traversal.
 *
 *  @param owner  Account that owns the NFT collection.
 *  @return Keylet typed `ltNFTOKEN_PAGE` with low 96 bits all zero.
 */
Keylet
nftpageMin(AccountID const& owner)
{
    std::array<std::uint8_t, 32> buf{};
    std::memcpy(buf.data(), owner.data(), owner.size());
    return {ltNFTOKEN_PAGE, uint256{buf}};
}

/** Return the keylet for an owner's highest possible NFT page.
 *
 *  The maximum keylet has the low 96 bits set to all ones, making it the
 *  ceiling of the owner's page range.  Together with `nftpageMin` this
 *  defines the half-open interval `[min, max]` that covers every page
 *  belonging to this owner.
 *
 *  @param owner  Account that owns the NFT collection.
 *  @return Keylet typed `ltNFTOKEN_PAGE` with low 96 bits all one.
 */
Keylet
nftpageMax(AccountID const& owner)
{
    uint256 id = nft::kPAGE_MASK;
    std::memcpy(id.data(), owner.data(), owner.size());
    return {ltNFTOKEN_PAGE, id};
}

/** Return the keylet for the NFT page that should contain @p token.
 *
 *  Preserves the owner identity from @p k (high 160 bits) and replaces
 *  the range tag (low 96 bits) with the corresponding bits of @p token.
 *  The result is the key of the page whose range covers that token.
 *
 *  @param k      An NFT page keylet for the same owner (must be `ltNFTOKEN_PAGE`).
 *  @param token  256-bit NFToken ID whose low 96 bits determine the page.
 *  @return Keylet typed `ltNFTOKEN_PAGE` for the page covering @p token.
 */
Keylet
nftpage(Keylet const& k, uint256 const& token)
{
    XRPL_ASSERT(k.type == ltNFTOKEN_PAGE, "xrpl::keylet::nftpage : valid input type");
    return {ltNFTOKEN_PAGE, (k.key & ~nft::kPAGE_MASK) + (token & nft::kPAGE_MASK)};
}

/** Return the keylet for an NFToken buy or sell offer.
 *
 *  @param owner  Account that created the offer.
 *  @param seq    Sequence number of the NFTokenCreateOffer transaction.
 *  @return Keylet typed `ltNFTOKEN_OFFER`.
 */
Keylet
nftoffer(AccountID const& owner, std::uint32_t seq)
{
    return {ltNFTOKEN_OFFER, indexHash(LedgerNameSpace::NftokenOffer, owner, seq)};
}

/** Return the keylet for the directory of buy offers for an NFToken.
 *
 *  @param id  256-bit NFToken ID.
 *  @return Keylet typed `ltDIR_NODE` for the buy-offer directory.
 */
Keylet
nftBuys(uint256 const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::NftokenBuyOffers, id)};
}

/** Return the keylet for the directory of sell offers for an NFToken.
 *
 *  @param id  256-bit NFToken ID.
 *  @return Keylet typed `ltDIR_NODE` for the sell-offer directory.
 */
Keylet
nftSells(uint256 const& id) noexcept
{
    return {ltDIR_NODE, indexHash(LedgerNameSpace::NftokenSellOffers, id)};
}

/** Return the keylet for an AMM pool keyed by its two pooled assets.
 *
 *  The two assets are sorted via `std::minmax` before hashing to produce an
 *  order-independent key — `amm(A, B)` and `amm(B, A)` yield the same keylet.
 *  A `std::visit` then dispatches over all four `Issue`/`MPTIssue` combinations
 *  at compile time to extract the correct identifier fields for each asset type.
 *
 *  @param asset1  One of the two pooled assets.
 *  @param asset2  The other pooled asset.
 *  @return Keylet typed `ltAMM`.
 */
Keylet
amm(Asset const& asset1, Asset const& asset2) noexcept
{
    auto const& [minA, maxA] = std::minmax(asset1, asset2);
    return std::visit(
        []<ValidIssueType TIss1, ValidIssueType TIss2>(TIss1 const& issue1, TIss2 const& issue2) {
            if constexpr (std::is_same_v<TIss1, Issue> && std::is_same_v<TIss2, Issue>)
            {
                return amm(indexHash(
                    LedgerNameSpace::Amm,
                    issue1.account,
                    issue1.currency,
                    issue2.account,
                    issue2.currency));
            }
            else if constexpr (std::is_same_v<TIss1, Issue> && std::is_same_v<TIss2, MPTIssue>)
            {
                return amm(indexHash(
                    LedgerNameSpace::Amm, issue1.account, issue1.currency, issue2.getMptID()));
            }
            else if constexpr (std::is_same_v<TIss1, MPTIssue> && std::is_same_v<TIss2, Issue>)
            {
                return amm(indexHash(
                    LedgerNameSpace::Amm, issue1.getMptID(), issue2.account, issue2.currency));
            }
            else if constexpr (std::is_same_v<TIss1, MPTIssue> && std::is_same_v<TIss2, MPTIssue>)
            {
                return amm(indexHash(LedgerNameSpace::Amm, issue1.getMptID(), issue2.getMptID()));
            }
        },
        minA.value(),
        maxA.value());
}

/** Return the keylet for an AMM pool from a pre-computed 256-bit AMM ID.
 *
 *  Use this overload when the AMM ID has already been computed (e.g. it is
 *  stored in `sfAMMID` on another SLE) to avoid redundant hashing.
 *
 *  @param id  Pre-computed 256-bit AMM identifier.
 *  @return Keylet typed `ltAMM`.
 */
Keylet
amm(uint256 const& id) noexcept
{
    return {ltAMM, id};
}

/** Return the keylet for a Delegate grant from one account to another.
 *
 *  @param account             Account that is granting delegated authority.
 *  @param authorizedAccount   Account receiving the delegated authority.
 *  @return Keylet typed `ltDELEGATE`.
 */
Keylet
delegate(AccountID const& account, AccountID const& authorizedAccount) noexcept
{
    return {ltDELEGATE, indexHash(LedgerNameSpace::Delegate, account, authorizedAccount)};
}

/** Return the keylet for a cross-chain bridge object.
 *
 *  A door account may support multiple bridges.  On the locking chain, at
 *  most one bridge is permitted per locking-chain currency; on the issuing
 *  chain, at most one bridge per issuing-chain currency.  The key therefore
 *  encodes the door account and the currency appropriate to @p chainType.
 *
 *  @param bridge     Bridge descriptor containing door accounts and issues.
 *  @param chainType  Selects whether to key on the locking or issuing side.
 *  @return Keylet typed `ltBRIDGE`.
 */
Keylet
bridge(STXChainBridge const& bridge, STXChainBridge::ChainType chainType)
{
    auto const& issue = bridge.issue(chainType);
    return {ltBRIDGE, indexHash(LedgerNameSpace::Bridge, bridge.door(chainType), issue.currency)};
}

/** Return the keylet for a cross-chain claim ID.
 *
 *  The key encodes the full bridge identity (both door accounts and both
 *  issues) plus the sequential claim ID (`sfXChainClaimID`), ensuring
 *  uniqueness across all bridges and all claim IDs.
 *
 *  @param bridge  Bridge descriptor.
 *  @param seq     Sequential claim ID (`sfXChainClaimID` in the object).
 *  @return Keylet typed `ltXCHAIN_OWNED_CLAIM_ID`.
 */
Keylet
xChainClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return {
        ltXCHAIN_OWNED_CLAIM_ID,
        indexHash(
            LedgerNameSpace::XchainClaimId,
            bridge.lockingChainDoor(),
            bridge.lockingChainIssue(),
            bridge.issuingChainDoor(),
            bridge.issuingChainIssue(),
            seq)};
}

/** Return the keylet for a cross-chain create-account claim ID.
 *
 *  Analogous to `xChainClaimID` but for the create-account variant.  The
 *  sequential counter is `sfXChainAccountCreateCount` in the object.
 *
 *  @param bridge  Bridge descriptor.
 *  @param seq     Sequential create-account claim ID (`sfXChainAccountCreateCount`).
 *  @return Keylet typed `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID`.
 */
Keylet
xChainCreateAccountClaimID(STXChainBridge const& bridge, std::uint64_t seq)
{
    return {
        ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID,
        indexHash(
            LedgerNameSpace::XchainCreateAccountClaimId,
            bridge.lockingChainDoor(),
            bridge.lockingChainIssue(),
            bridge.issuingChainDoor(),
            bridge.issuingChainIssue(),
            seq)};
}

/** Return the keylet for an account's DID (Decentralized Identifier) document.
 *
 *  @param account  Account that owns the DID.
 *  @return Keylet typed `ltDID`.
 */
Keylet
did(AccountID const& account) noexcept
{
    return {ltDID, indexHash(LedgerNameSpace::Did, account)};
}

/** Return the keylet for a price oracle.
 *
 *  An account may own multiple oracles identified by distinct document IDs.
 *
 *  @param account     Account that owns the oracle.
 *  @param documentID  Application-defined identifier distinguishing oracles
 *      within the same account (`sfOracleDocumentID`).
 *  @return Keylet typed `ltORACLE`.
 */
Keylet
oracle(AccountID const& account, std::uint32_t const& documentID) noexcept
{
    return {ltORACLE, indexHash(LedgerNameSpace::Oracle, account, documentID)};
}

/** Return the keylet for an MPT issuance from the issuer's sequence and account.
 *
 *  Constructs the `MPTID` via `makeMptID` then delegates to the `MPTID`
 *  overload to avoid duplicating the hash logic.
 *
 *  @param seq     Issuer's account sequence at the time of issuance creation.
 *  @param issuer  Account that created the issuance.
 *  @return Keylet typed `ltMPTOKEN_ISSUANCE`.
 */
Keylet
mptIssuance(std::uint32_t seq, AccountID const& issuer) noexcept
{
    return mptIssuance(makeMptID(seq, issuer));
}

/** Return the keylet for an MPT issuance from a pre-built MPTID.
 *
 *  @param issuanceID  192-bit MPT issuance identifier (see `makeMptID`).
 *  @return Keylet typed `ltMPTOKEN_ISSUANCE`.
 */
Keylet
mptIssuance(MPTID const& issuanceID) noexcept
{
    return {ltMPTOKEN_ISSUANCE, indexHash(LedgerNameSpace::MPTokenIssuance, issuanceID)};
}

/** Return the keylet for a holder's MPToken balance entry, identified by MPTID.
 *
 *  Derives the issuance key first via `mptIssuance(issuanceID).key`, then
 *  delegates to the `uint256`-keyed overload.  Use the `uint256` overload
 *  directly when the issuance key is already available to avoid redundant
 *  hashing.
 *
 *  @param issuanceID  192-bit MPTID identifying the issuance.
 *  @param holder      Account holding the MPToken balance.
 *  @return Keylet typed `ltMPTOKEN`.
 */
Keylet
mptoken(MPTID const& issuanceID, AccountID const& holder) noexcept
{
    return mptoken(mptIssuance(issuanceID).key, holder);
}

/** Return the keylet for a holder's MPToken balance entry, identified by issuance key.
 *
 *  @param issuanceKey  256-bit key of the `MPTokenIssuance` SLE.
 *  @param holder       Account holding the MPToken balance.
 *  @return Keylet typed `ltMPTOKEN`.
 */
Keylet
mptoken(uint256 const& issuanceKey, AccountID const& holder) noexcept
{
    return {ltMPTOKEN, indexHash(LedgerNameSpace::MPToken, issuanceKey, holder)};
}

/** Return the keylet for a verifiable credential.
 *
 *  @param subject   Account the credential was issued to.
 *  @param issuer    Account that issued the credential.
 *  @param credType  Application-defined credential type byte string.
 *  @return Keylet typed `ltCREDENTIAL`.
 */
Keylet
credential(AccountID const& subject, AccountID const& issuer, Slice const& credType) noexcept
{
    return {ltCREDENTIAL, indexHash(LedgerNameSpace::Credential, subject, issuer, credType)};
}

/** Return the keylet for a single-asset vault created by @p owner.
 *
 *  Derives the raw key via `indexHash` then delegates to the `uint256`
 *  overload (which wraps it in the `ltVAULT` keylet), keeping the two-step
 *  pattern consistent with `loanbroker` and `loan`.
 *
 *  @param owner  Account that created the vault.
 *  @param seq    Sequence number of the VaultCreate transaction.
 *  @return Keylet typed `ltVAULT`.
 */
Keylet
vault(AccountID const& owner, std::uint32_t seq) noexcept
{
    return vault(indexHash(LedgerNameSpace::Vault, owner, seq));
}

/** Return the keylet for a loan broker created by @p owner.
 *
 *  @param owner  Account that created the loan broker.
 *  @param seq    Sequence number of the LoanBrokerCreate transaction.
 *  @return Keylet typed `ltLOAN_BROKER`.
 */
Keylet
loanbroker(AccountID const& owner, std::uint32_t seq) noexcept
{
    return loanbroker(indexHash(LedgerNameSpace::LoanBroker, owner, seq));
}

/** Return the keylet for an individual loan within a loan broker.
 *
 *  @param loanBrokerID  256-bit key of the parent `LoanBroker` SLE.
 *  @param loanSeq       Sequential loan number assigned by the broker.
 *  @return Keylet typed `ltLOAN`.
 */
Keylet
loan(uint256 const& loanBrokerID, std::uint32_t loanSeq) noexcept
{
    return loan(indexHash(LedgerNameSpace::Loan, loanBrokerID, loanSeq));
}

/** Return the keylet for a permissioned domain owned by @p account.
 *
 *  @param account  Account that created the permissioned domain.
 *  @param seq      Sequence number of the PermissionedDomainSet transaction.
 *  @return Keylet typed `ltPERMISSIONED_DOMAIN`.
 */
Keylet
permissionedDomain(AccountID const& account, std::uint32_t seq) noexcept
{
    return {ltPERMISSIONED_DOMAIN, indexHash(LedgerNameSpace::PermissionedDomain, account, seq)};
}

/** Return the keylet for a permissioned domain from a pre-computed domain ID.
 *
 *  Use this overload when the domain ID is already known (e.g. stored in
 *  `sfDomainID` on another SLE) to avoid recomputing the hash.
 *
 *  @param domainID  Pre-computed 256-bit domain key.
 *  @return Keylet typed `ltPERMISSIONED_DOMAIN`.
 */
Keylet
permissionedDomain(uint256 const& domainID) noexcept
{
    return {ltPERMISSIONED_DOMAIN, domainID};
}

}  // namespace keylet

}  // namespace xrpl
