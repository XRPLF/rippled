#pragma once

/** @file
 *  Single authoritative source for all tunable constants in the XRPL RPC
 *  subsystem.
 *
 *  Rather than scattering magic numbers through handler code, every
 *  paginated command, pathfinding throttle, and request-size gate
 *  references a named constant defined here, making capacity planning
 *  visible and changes safe. All constants live in
 *  `xrpl::RPC::Tuning`.
 */

/** Tuned constants. */
/** @{ */
namespace xrpl::RPC::Tuning {

/** Pagination contract for a single RPC command.
 *
 *  Encodes three values that `readLimitField()` in `RPCHelpers.cpp` uses to
 *  normalise a caller-supplied `limit` field:
 *
 *  - `rmin` — hard floor; the resolved limit is never below this value for
 *    non-unlimited connections.
 *  - `rDefault` — used when the caller omits the `limit` field entirely.
 *  - `rmax` — hard ceiling for non-unlimited connections.
 *
 *  Connections that carry an unlimited role (admin or trusted local) bypass
 *  both `rmin` and `rmax` and receive exactly the limit they requested.
 *
 *  @see readLimitField
 */
struct LimitRange
{
    unsigned int rmin, rDefault, rmax;
};

/** Pagination limits for the `account_lines` command.
 *
 *  Governs iteration over an account's trust lines. The uniform
 *  `{10, 200, 400}` range is shared by all account-scoped ledger-object
 *  commands, reflecting their comparable per-page iteration cost.
 */
static LimitRange constexpr kACCOUNT_LINES = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Pagination limits for the `account_channels` command.
 *
 *  Governs iteration over payment channels owned by an account.
 *  Shares the `{10, 200, 400}` range with other account-level commands.
 */
static LimitRange constexpr kACCOUNT_CHANNELS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Pagination limits for the `account_objects` command.
 *
 *  Governs iteration over all ledger objects owned by an account (offers,
 *  channels, escrows, etc.). Shares the `{10, 200, 400}` range with other
 *  account-level commands.
 */
static LimitRange constexpr kACCOUNT_OBJECTS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Pagination limits for the `account_offers` command.
 *
 *  Governs iteration over open DEX offers placed by an account. Shares the
 *  `{10, 200, 400}` range with other account-level commands.
 */
static LimitRange constexpr kACCOUNT_OFFERS = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Pagination limits for the `account_tx` command.
 *
 *  Governs iteration over an account's transaction history. Shares the
 *  `{10, 200, 400}` range with other account-level commands.
 */
static LimitRange constexpr kACCOUNT_TX = {.rmin = 10, .rDefault = 200, .rmax = 400};

/** Pagination limits for the `book_offers` command.
 *
 *  Lower default and ceiling than account-level commands, reflecting the
 *  denser per-offer serialisation cost. `rmin = 1` rather than 10; note
 *  that `readLimitField()` rejects a caller-supplied value of zero as
 *  invalid, so the practical floor for external callers is 1.
 */
static LimitRange constexpr kBOOK_OFFERS = {.rmin = 1, .rDefault = 60, .rmax = 100};

/** Pagination limits for the `no_ripple_check` command.
 *
 *  Higher default (300) than most account commands because the no-ripple
 *  audit is typically invoked to scan a large trust-line set in a single
 *  pass. Ceiling matches the standard account-level maximum.
 */
static LimitRange constexpr kNO_RIPPLE_CHECK = {.rmin = 10, .rDefault = 300, .rmax = 400};

/** Pagination limits for the `account_nftokens` command, in pages.
 *
 *  Higher floor (20) than most account commands. NFT page iteration has a
 *  coarser granularity than trust-line or offer iteration, so a minimum
 *  of 20 keeps responses useful. Default and ceiling match standard
 *  account-level values.
 */
static LimitRange constexpr kACCOUNT_NF_TOKENS = {.rmin = 20, .rDefault = 100, .rmax = 400};

/** Pagination limits for the `nft_buy_offers` and `nft_sell_offers` commands.
 *
 *  Higher default (250) and ceiling (500) than account-level commands.
 *  NFT offer books can legitimately accumulate hundreds of bids, so the
 *  ceiling is raised to reduce the number of pagination round-trips required
 *  by typical clients.
 */
static LimitRange constexpr kNFT_OFFERS = {.rmin = 50, .rDefault = 250, .rmax = 500};

/** Default fee multiplier for auto-filled transaction fees.
 *
 *  Applied by `TransactionSign.cpp` when no explicit fee is provided.
 *  The effective fee = (network base fee) × `kDEFAULT_AUTO_FILL_FEE_MULTIPLIER`
 *  / `kDEFAULT_AUTO_FILL_FEE_DIVISOR`. The 10× factor provides headroom
 *  to clear the network under typical load without user intervention.
 *  Can be overridden per-request via `fee_mult_max`.
 *
 *  @see kDEFAULT_AUTO_FILL_FEE_DIVISOR
 */
static int constexpr kDEFAULT_AUTO_FILL_FEE_MULTIPLIER = 10;

/** Default fee divisor for auto-filled transaction fees.
 *
 *  Combined with `kDEFAULT_AUTO_FILL_FEE_MULTIPLIER`, produces the
 *  auto-fill ratio: 10/1 = 10×. A divisor of 1 means no downward
 *  adjustment is applied to the multiplied fee.
 *
 *  @see kDEFAULT_AUTO_FILL_FEE_MULTIPLIER
 */
static int constexpr kDEFAULT_AUTO_FILL_FEE_DIVISOR = 1;

/** Maximum number of concurrent synchronous path-find operations.
 *
 *  Enforced in `LegacyPathFind.cpp` via a lock-free CAS loop on a global
 *  atomic counter. If two `ripple_path_find` operations are already running,
 *  new non-admin requests are rejected immediately with `rpcTOO_BUSY` rather
 *  than queued. The low limit (2) reflects the high per-request CPU cost of
 *  path-finding relative to all other RPC work. Admin connections bypass
 *  this guard.
 *
 *  @see kMAX_PATHFIND_JOB_COUNT
 */
static int constexpr kMAX_PATHFINDS_IN_PROGRESS = 2;

/** Job-queue depth threshold for rejecting path-find requests.
 *
 *  Checked in `LegacyPathFind.cpp` before submitting a path-find job. If
 *  the client job queue already contains more than this many pending jobs,
 *  or if local fee load is elevated, new non-admin path-find requests are
 *  rejected with `rpcTOO_BUSY`. Prevents pathfinding from starving other
 *  work during bursts. Admin connections bypass this guard.
 *
 *  @see kMAX_PATHFINDS_IN_PROGRESS
 */
static int constexpr kMAX_PATHFIND_JOB_COUNT = 50;

/** Maximum number of pending client RPC jobs before shedding load.
 *
 *  Checked in `RPCHandler.cpp` before dispatching any non-admin RPC
 *  command. If the client job queue depth exceeds this value, the request
 *  is rejected with `rpcTOO_BUSY` without further processing. Admin and
 *  unlimited connections bypass this check entirely.
 */
static int constexpr kMAX_JOB_QUEUE_CLIENTS = 500;

/** Maximum age of the most-recently validated ledger for transaction signing.
 *
 *  Enforced by `TransactionSign.cpp` for `sign` and `submit` requests. If
 *  the server's most-recently validated ledger is older than this threshold
 *  (and the node is not in standalone mode), signing is refused with
 *  `rpcNO_CURRENT` (API v1) or `rpcNOT_SYNCED` (API v2). A stale validated
 *  ledger implies the node may be partitioned or behind, making its fee
 *  levels and account states unreliable.
 */
auto constexpr kMAX_VALIDATED_LEDGER_AGE = std::chrono::minutes{2};

/** Maximum allowed byte size of an incoming RPC request payload.
 *
 *  Enforced in `ServerHandler.cpp` before any JSON parsing begins.
 *  Requests that exceed 1 MB are rejected cheaply without allocating
 *  parser memory, preventing amplification attacks from maliciously large
 *  payloads. Applied to both HTTP and WebSocket transports.
 */
static int constexpr kMAX_REQUEST_SIZE = 1000000;

/** Maximum number of ledger entries per page for binary `ledger_data` responses.
 *
 *  Binary (XRPL canonical serialisation) encoding is roughly an order of
 *  magnitude more compact than JSON, so the binary page ceiling is set 8×
 *  higher than `kJSON_PAGE_LENGTH` to keep wire payload and memory pressure
 *  comparable across both formats.
 *
 *  @see kJSON_PAGE_LENGTH
 *  @see pageLength
 */
static int constexpr kBINARY_PAGE_LENGTH = 2048;

/** Maximum number of ledger entries per page for JSON `ledger_data` responses.
 *
 *  JSON-encoded ledger objects carry field names, string-encoded amounts,
 *  and human-readable type tags; the same data in binary is far more
 *  compact. Capping JSON pages at 256 objects keeps the wire payload
 *  comparable to a binary page of 2048 objects.
 *
 *  @see kBINARY_PAGE_LENGTH
 *  @see pageLength
 */
static int constexpr kJSON_PAGE_LENGTH = 256;

/** Select the appropriate `ledger_data` page-length ceiling by encoding format.
 *
 *  Returns `kBINARY_PAGE_LENGTH` (2048) when `isBinary` is true, and
 *  `kJSON_PAGE_LENGTH` (256) otherwise. Used by `LedgerData.cpp` to clamp
 *  caller-requested limits without repeating the format-dispatch logic.
 *
 *  @param isBinary True if the response will use binary (XRPL canonical)
 *      encoding; false for JSON.
 *  @return The maximum number of ledger entries that should be returned in
 *      a single response page for the requested encoding format.
 */
int constexpr pageLength(bool isBinary)
{
    return isBinary ? kBINARY_PAGE_LENGTH : kJSON_PAGE_LENGTH;
}

/** Maximum number of explicitly-specified source currencies in a path-find request.
 *
 *  If the caller's `source_currencies` array in a `ripple_path_find` or
 *  `path_find` request contains more than this many entries, the request is
 *  rejected as malformed. The low limit (18) bounds the combinatorial
 *  expansion the pathfinder must explore when evaluating user-specified
 *  source assets.
 *
 *  @see kMAX_AUTO_SRC_CUR
 */
static int constexpr kMAX_SRC_CUR = 18;

/** Maximum number of automatically-discovered source currencies in a path-find request.
 *
 *  When no `source_currencies` are provided, the pathfinder enumerates
 *  the sender's balances automatically. Discovery stops once this many
 *  candidate assets have been accumulated. The higher limit (88 vs. 18 for
 *  explicit currencies) reflects that server-generated candidates can be
 *  budgeted more accurately than arbitrary user input.
 *
 *  @see kMAX_SRC_CUR
 */
static int constexpr kMAX_AUTO_SRC_CUR = 88;

}  // namespace xrpl::RPC::Tuning
/** @} */
