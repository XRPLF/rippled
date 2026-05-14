/** @file
 *  Canonical source of XRPL protocol constants and boundary predicates.
 *
 *  Every hard-coded numeric limit that, if changed silently, would cause a
 *  **hard fork** — a ledger-state disagreement between nodes running different
 *  software versions — is defined here.  All constants are `constexpr` and
 *  therefore available at compile time with zero runtime overhead.
 *
 *  @note Changing any value in this file without pairing the change with an
 *      amendment-gated detection mechanism will split the network.
 *
 *  @ingroup protocol
 */
#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>

namespace xrpl {

/** Smallest legal serialized size of a transaction, in bytes.
 *
 *  Transactions below this threshold are trivially malformed and are rejected
 *  before deserialization begins.
 */
std::size_t constexpr kTX_MIN_SIZE_BYTES = 32;

/** Largest legal serialized size of a transaction, in bytes.
 *
 *  The 1 MB cap protects node memory and network bandwidth.  Transactions
 *  exceeding this limit are rejected on receipt without further processing.
 */
std::size_t constexpr kTX_MAX_SIZE_BYTES = megabytes(1);

/** Maximum number of unfunded offers that may be removed in a single
 *  transaction pass.
 *
 *  Unfunded-offer cleanup is opportunistic: stale offers are removed as a
 *  side-effect of offer placement.  Capping the count keeps the worst-case
 *  execution time of a single transaction predictable.
 *
 *  @note The asymmetry with `kEXPIRED_OFFER_REMOVE_LIMIT` (1000 vs 256)
 *      reflects that unfunded-offer removal was designed to handle larger
 *      batches; expired offers are discovered through a different, narrower
 *      path.
 */
std::size_t constexpr kUNFUNDED_OFFER_REMOVE_LIMIT = 1000;

/** Maximum number of expired offers that may be removed in a single
 *  transaction pass.
 *
 *  @see kUNFUNDED_OFFER_REMOVE_LIMIT for the rationale behind the asymmetric
 *      cap.
 */
std::size_t constexpr kEXPIRED_OFFER_REMOVE_LIMIT = 256;

/** Maximum number of metadata entries a single transaction may produce.
 *
 *  When a transaction would exceed this cap the transactor returns
 *  `tecOVERSIZE`, triggering a controlled teardown that applies the fee
 *  and rolls back ledger mutations rather than allowing unbounded metadata
 *  growth.
 */
std::size_t constexpr kOVERSIZE_META_DATA_CAP = 5200;

/** Maximum number of entries per owner-directory or offer-directory page.
 *
 *  Keeping pages small bounds the work required to traverse a directory:
 *  each page hop visits at most 32 entries.
 */
std::size_t constexpr kDIR_NODE_MAX_ENTRIES = 32;

/** Historical maximum number of pages in a single directory.
 *
 *  This limit was enforced before the `fixDirectoryLimit` amendment.
 *  Post-amendment, directories may grow beyond 262 144 pages; this
 *  constant is retained for pre-amendment replay correctness.
 *
 *  @note Pre-amendment code returns `tecDIR_FULL` when this limit is
 *      reached.  Post-amendment, only unsigned-integer overflow can
 *      produce a null page index.
 */
std::uint64_t constexpr kDIR_NODE_MAX_PAGES = 262144;

/** Maximum number of NFToken entries per NFT directory page. */
std::size_t constexpr kDIR_MAX_TOKENS_PER_PAGE = 32;

/** Maximum number of owner-directory entries an account may hold and still
 *  be eligible for deletion via `AccountDelete`.
 *
 *  Accounts with more than 1000 directory entries cannot be deleted; this
 *  protects against unbounded cleanup work within a single transaction.
 */
std::size_t constexpr kMAX_DELETABLE_DIR_ENTRIES = 1000;

/** Maximum number of NFToken offers that may be cancelled in a single
 *  `NFTokenCancelOffer` transaction.
 */
std::size_t constexpr kMAX_TOKEN_OFFER_CANCEL_COUNT = 500;

/** Maximum number of NFToken offers that must be cleaned up before an NFT
 *  can be burned.
 *
 *  An NFT with more than 500 live offers cannot be burned until the excess
 *  offers are cancelled first.
 */
std::size_t constexpr kMAX_DELETABLE_TOKEN_OFFER_ENTRIES = 500;

/** Maximum NFToken transfer fee, expressed in tenths of a basis point.
 *
 *  Transfer fees range from 0% to 50%.  A value of 1 000 represents 1% and
 *  a value of 50 000 represents 50%.  For very low fee values the computed
 *  fee amount may round down to zero drops.
 */
std::uint16_t constexpr kMAX_TRANSFER_FEE = 50000;

/** Number of basis points (bips) in 100% (unity).
 *
 *  One basis point equals 0.01%.  To compute the share of a value `X`
 *  corresponding to `B` bips, use `X * B / kBIPS_PER_UNITY`.  To convert
 *  a whole-percentage `P` to bips, use `P * kBIPS_PER_UNITY / 100`
 *  (or simply call `percentageToBips(P)`).
 *
 *  Example: 10% coverage on 999 XRP (999 000 000 drops) =
 *  `999'000'000 * 1'000 / 10'000` = 99 900 000 drops.
 *
 *  All ledger fee and rate arithmetic uses integer bips to guarantee
 *  bit-identical results across all validator platforms.
 */
Bips32 constexpr kBIPS_PER_UNITY(100 * 100);
static_assert(kBIPS_PER_UNITY == Bips32{10'000});

/** Number of tenth-basis-points in 100% (unity).
 *
 *  One tenth-basis-point equals 0.001%.  Use `percentageToTenthBips(P)`
 *  to convert a whole percentage, or `tenthBipsOfValue(value, rate)` to
 *  apply a rate to a value.
 */
TenthBips32 constexpr kTENTH_BIPS_PER_UNITY(kBIPS_PER_UNITY.value() * 10);
static_assert(kTENTH_BIPS_PER_UNITY == TenthBips32(100'000));

/** Convert a whole-percentage value to a strongly-typed `Bips32`.
 *
 *  Uses integer division; fractional basis points are truncated.
 *
 *  @param percentage  An integer percentage in [0, 100].
 *  @return The equivalent number of basis points as a `Bips32`.
 */
constexpr Bips32
percentageToBips(std::uint32_t percentage)
{
    return Bips32(percentage * kBIPS_PER_UNITY.value() / 100);
}

/** Convert a whole-percentage value to a strongly-typed `TenthBips32`.
 *
 *  Uses integer division; fractional tenth-bips are truncated.
 *
 *  @param percentage  An integer percentage in [0, 100].
 *  @return The equivalent number of tenth-basis-points as a `TenthBips32`.
 */
constexpr TenthBips32
percentageToTenthBips(std::uint32_t percentage)
{
    return TenthBips32(percentage * kTENTH_BIPS_PER_UNITY.value() / 100);
}

/** Compute the basis-point share of a value using integer arithmetic.
 *
 *  Calculates `value * bips / kBIPS_PER_UNITY` without floating point,
 *  guaranteeing deterministic results on all platforms.
 *
 *  @tparam T      Numeric type of the value (must support `*` and `/`).
 *  @tparam TBips  Underlying storage type of the `Bips` wrapper.
 *  @param value   The base amount to take a share of.
 *  @param bips    The rate in basis points.
 *  @return The share of `value` at the given rate, truncated toward zero.
 */
template <typename T, class TBips>
constexpr T
bipsOfValue(T value, Bips<TBips> bips)
{
    return value * bips.value() / kBIPS_PER_UNITY.value();
}

/** Compute the tenth-basis-point share of a value using integer arithmetic.
 *
 *  Calculates `value * bips / kTENTH_BIPS_PER_UNITY` without floating
 *  point, guaranteeing deterministic results on all platforms.
 *
 *  @tparam T      Numeric type of the value (must support `*` and `/`).
 *  @tparam TBips  Underlying storage type of the `TenthBips` wrapper.
 *  @param value   The base amount to take a share of.
 *  @param bips    The rate in tenth-basis-points.
 *  @return The share of `value` at the given rate, truncated toward zero.
 */
template <typename T, class TBips>
constexpr T
tenthBipsOfValue(T value, TenthBips<TBips> bips)
{
    return value * bips.value() / kTENTH_BIPS_PER_UNITY.value();
}

/** Rate and limit constants specific to the on-ledger lending protocol. */
namespace Lending {

/** Maximum management fee a LoanBroker may charge, in tenth-basis-points.
 *
 *  Valid values are in [0, 10%].  Stored as `TenthBips16` (fits in
 *  `uint16_t`) because 10 000 < 65 535.
 */
TenthBips16 constexpr kMAX_MANAGEMENT_FEE_RATE(
    unsafeCast<std::uint16_t>(percentageToTenthBips(10).value()));
static_assert(kMAX_MANAGEMENT_FEE_RATE == TenthBips16(std::uint16_t(10'000u)));

/** Maximum coverage rate a LoanBroker must maintain, in tenth-basis-points.
 *
 *  The coverage rate specifies the minimum fraction of outstanding loan
 *  debt that the broker must hold as collateral.  Valid values are in
 *  [0, 100%].
 */
TenthBips32 constexpr kMAX_COVER_RATE = percentageToTenthBips(100);
static_assert(kMAX_COVER_RATE == TenthBips32(100'000u));

/** Maximum overpayment fee on a loan, in tenth-basis-points.
 *
 *  Applied when a borrower pays more than the scheduled amount.  Valid
 *  values are in [0, 100%].
 */
TenthBips32 constexpr kMAX_OVERPAYMENT_FEE = percentageToTenthBips(100);
static_assert(kMAX_OVERPAYMENT_FEE == TenthBips32(100'000u));

/** Maximum annualized interest rate on a Loan, in tenth-basis-points.
 *
 *  Valid values are in [0, 100%].
 */
TenthBips32 constexpr kMAX_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_INTEREST_RATE == TenthBips32(100'000u));

/** Maximum late-payment interest premium on a Loan, in tenth-basis-points.
 *
 *  This rate is added to the base interest rate when payments are overdue.
 *  Valid values are in [0, 100%].
 */
TenthBips32 constexpr kMAX_LATE_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_LATE_INTEREST_RATE == TenthBips32(100'000u));

/** Maximum early-repayment (close) interest rate on a Loan, in
 *  tenth-basis-points.
 *
 *  Charged when a borrower repays a loan ahead of schedule.  Valid values
 *  are in [0, 100%].
 */
TenthBips32 constexpr kMAX_CLOSE_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_CLOSE_INTEREST_RATE == TenthBips32(100'000u));

/** Maximum overpayment interest rate charged on loan overpayments, in
 *  tenth-basis-points.
 *
 *  Valid values are in [0, 100%].
 */
TenthBips32 constexpr kMAX_OVERPAYMENT_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_OVERPAYMENT_INTEREST_RATE == TenthBips32(100'000u));

/** Number of loan payments per base-fee increment charged by `LoanPay`.
 *
 *  The fee is estimated from the transaction `Amount` divided by the
 *  loan's fixed payment size.  Overpayments (flagged with
 *  `tfLoanOverpayment`) count as one additional payment in the estimate.
 *  One base fee unit is charged for every 5 estimated payments.
 *
 *  @note This value was chosen arbitrarily and is amendment-locked once
 *      released: changing it without an amendment would alter the fee
 *      schedule for existing `LoanPay` transactions.
 *  @see kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION
 */
static constexpr int kLOAN_PAYMENTS_PER_FEE_INCREMENT = 5;

/** Hard cap on the number of combined payments processed by one `LoanPay`.
 *
 *  This limit is enforced during execution, not during fee estimation.
 *  When the cap is reached the transaction succeeds with the payments
 *  processed so far; any remaining `Amount` is not applied.
 *
 *  Because the fee is based on the *estimated* payment count (derived from
 *  `Amount / PeriodicPayment`) and the cap is enforced on the *actual*
 *  count, a transaction can be charged for more payments than it processes.
 *  Submitters should not exceed
 *  `kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION * Loan.PeriodicPayment` in
 *  `Amount`, and should omit `tfLoanOverpayment` if paying exactly that
 *  much.
 *
 *  @note `kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION` must remain a multiple
 *      of `kLOAN_PAYMENTS_PER_FEE_INCREMENT`; this invariant is checked
 *      at startup via `static_assert` in LoanPay.cpp.  Both values are
 *      amendment-locked once released.
 */
static constexpr int kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION = 100;
}  // namespace Lending

/** Maximum byte length of a URI stored in an NFToken. */
std::size_t constexpr kMAX_TOKEN_URI_LENGTH = 256;

/** Maximum byte length of the `Data` field (DID document) in a DID object. */
std::size_t constexpr kMAX_DID_DOCUMENT_LENGTH = 256;

/** Maximum byte length of the `URI` field in a DID object. */
std::size_t constexpr kMAX_DIDURI_LENGTH = 256;

/** Maximum byte length of the `Attestation` field in a DID object. */
std::size_t constexpr kMAX_DID_DATA_LENGTH = 256;

/** Maximum byte length of an account `Domain` field. */
std::size_t constexpr kMAX_DOMAIN_LENGTH = 256;

/** Maximum byte length of the `URI` field in a Credential object. */
std::size_t constexpr kMAX_CREDENTIAL_URI_LENGTH = 256;

/** Maximum byte length of the `CredentialType` field in a Credential object.
 *
 *  Narrower than the 256-byte default to keep credential-type strings
 *  human-readable and prevent abuse of the type field as an arbitrary blob.
 */
std::size_t constexpr kMAX_CREDENTIAL_TYPE_LENGTH = 64;

/** Maximum number of credentials that may appear in a transaction's
 *  `Credentials` array.
 */
std::size_t constexpr kMAX_CREDENTIALS_ARRAY_SIZE = 8;

/** Maximum number of credentials that a permissioned domain may reference. */
std::size_t constexpr kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE = 10;

/** Maximum byte length of the `MPTokenMetadata` field on an MPTokenIssuance. */
std::size_t constexpr kMAX_MP_TOKEN_METADATA_LENGTH = 1024;

/** Maximum quantity representable by an MPToken amount field.
 *
 *  Equal to `INT64_MAX` (2^63 − 1).  The `static_assert` below guarantees
 *  that the XRPL `Number` type can represent every valid MPToken quantity
 *  without overflow.
 */
std::uint64_t constexpr kMAX_MP_TOKEN_AMOUNT = 0x7FFF'FFFF'FFFF'FFFFull;
static_assert(Number::kMAX_REP >= kMAX_MP_TOKEN_AMOUNT);

/** Maximum byte length of the `Data` payload field. */
std::size_t constexpr kMAX_DATA_PAYLOAD_LENGTH = 256;

/** Vault withdrawal policy: first-come, first-served.
 *
 *  The numeric value 1 is the wire-stable identifier for this strategy;
 *  it must not change once released.
 */
std::uint8_t constexpr kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE = 1;

/** Default IOU-to-share scale exponent for a Vault.
 *
 *  When no explicit scale is specified at Vault creation the scale
 *  defaults to 6, meaning one IOU unit maps to 10^6 shares.  This
 *  applies only to IOU-backed vaults; native-asset and MPT vaults always
 *  use scale 0.
 */
std::uint8_t constexpr kVAULT_DEFAULT_IOU_SCALE = 6;

/** Maximum IOU-to-share scale exponent for a Vault.
 *
 *  Chosen so that exactly one IOU unit can always be converted to at
 *  least one share: 10^19 > `kMAX_MP_TOKEN_AMOUNT` (≈ 2^63) > 10^18.
 *  Preflight rejects any `VaultCreate` that specifies a scale above this
 *  value with `temMALFORMED`.  Applies only to IOU-backed vaults.
 */
std::uint8_t constexpr kVAULT_MAXIMUM_IOU_SCALE = 18;

/** Maximum recursion depth when checking whether a vault's asset is itself
 *  backed by another vault.
 *
 *  Counted from 0, so a depth of 5 permits at most 6 levels of nesting.
 *  This prevents pathological chains from consuming unbounded stack space
 *  during asset-validation traversal.
 */
std::uint8_t constexpr kMAX_ASSET_CHECK_DEPTH = 5;

/** Ledger sequence number type.
 *
 *  A named alias for `uint32_t` that makes function signatures
 *  self-documenting wherever ledger positions are passed.
 */
using LedgerIndex = std::uint32_t;

/** Number of ledgers between consecutive flag-ledger boundaries.
 *
 *  Every 256 ledgers the network applies accumulated validator votes for
 *  fee adjustments, reserve requirements, amendment activation, and
 *  Negative UNL reliability scoring.  Both `isFlagLedger()` and
 *  `isVotingLedger()` test `seq % kFLAG_LEDGER_INTERVAL == 0`; the
 *  semantic distinction between the two predicates is resolved by callers
 *  via a `+1` offset on the sequence number they pass.
 *
 *  @note This constant is an implicit part of the wire protocol.  Changing
 *      it without an amendment-gated migration path will cause a hard fork.
 */
std::uint32_t constexpr kFLAG_LEDGER_INTERVAL = 256;

/** Return `true` if @p seq is a voting ledger.
 *
 *  Semantically, this asks: "will the ledger built *on top of* `seq`
 *  be a flag ledger?"  Callers therefore pass `seq + 1` (the sequence of
 *  the ledger currently being assembled).  `RCLConsensus` uses this
 *  predicate to decide whether to inject Negative UNL pseudo-transactions
 *  for the new consensus round.
 *
 *  The arithmetic is identical to `isFlagLedger`; the two names exist to
 *  make the `+1` offset explicit at each call site without embedding it
 *  inside these functions.
 *
 *  @param seq  The ledger index to test (typically the previous ledger's
 *      sequence plus one).
 *  @return `true` if `seq % kFLAG_LEDGER_INTERVAL == 0`.
 *  @see isFlagLedger
 */
bool
isVotingLedger(LedgerIndex seq);

/** Return `true` if @p seq is a flag ledger.
 *
 *  A flag ledger is any ledger whose sequence number is an exact multiple
 *  of `kFLAG_LEDGER_INTERVAL` (256).  It is the ledger in which fee-vote
 *  and amendment pseudo-transactions are applied, and in which Negative
 *  UNL reliability updates take effect.  `Change::doApply` and
 *  `FeeVoteImpl` gate their parameter-update logic on this predicate.
 *
 *  Callers pass the ledger's **own** sequence number to ask "has this
 *  ledger already crossed the boundary?", as opposed to `isVotingLedger`,
 *  which is called with `seq + 1`.
 *
 *  @param seq  The ledger index to test.
 *  @return `true` if `seq % kFLAG_LEDGER_INTERVAL == 0`.
 *  @see isVotingLedger
 */
bool
isFlagLedger(LedgerIndex seq);

/** Transaction identifier type.
 *
 *  A 256-bit hash computed over the canonicalized, serialized transaction
 *  object using `HashPrefix::transactionID` as the domain separator.
 */
using TxID = uint256;

/** Maximum number of AMM trust lines that may be deleted as part of an
 *  AMM account-deletion cleanup pass.
 */
std::uint16_t constexpr kMAX_DELETABLE_AMM_TRUST_LINES = 512;

/** Maximum byte length of the `URI` field in an Oracle object. */
std::size_t constexpr kMAX_ORACLE_URI = 256;

/** Maximum byte length of the `Provider` field in an Oracle object. */
std::size_t constexpr kMAX_ORACLE_PROVIDER = 256;

/** Maximum number of price data-series entries in an Oracle object. */
std::size_t constexpr kMAX_ORACLE_DATA_SERIES = 10;

/** Maximum byte length of the `SymbolClass` field in an Oracle object. */
std::size_t constexpr kMAX_ORACLE_SYMBOL_CLASS = 16;

/** Maximum allowed age of an Oracle price update, in seconds.
 *
 *  `OracleSet` rejects updates whose `LastUpdateTime` differs from the
 *  last-closed-ledger close time by more than 300 seconds (5 minutes).
 */
std::size_t constexpr kMAX_LAST_UPDATE_TIME_DELTA = 300;

/** Maximum price-scaling exponent accepted in an Oracle object. */
std::size_t constexpr kMAX_PRICE_SCALE = 20;

/** Maximum percentage of outlier data points to trim in Oracle price
 *  aggregation.
 */
std::size_t constexpr kMAX_TRIM = 25;

/** Maximum number of granular delegate permissions an account may grant. */
std::size_t constexpr kPERMISSION_MAX_SIZE = 10;

/** Maximum number of inner transactions in a single Batch transaction.
 *
 *  Enforced during preflight; batches exceeding this count are rejected.
 *  The limit directly bounds the worst-case compute cost for batch
 *  signature validation and fee calculation.
 */
std::size_t constexpr kMAX_BATCH_TX_COUNT = 8;

}  // namespace xrpl
