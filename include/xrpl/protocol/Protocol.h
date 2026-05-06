#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>

namespace xrpl {

/** Protocol specific constants.

    This information is, implicitly, part of the protocol.

    @note Changing these values without adding code to the
          server to detect "pre-change" and "post-change"
          will result in a hard fork.

    @ingroup protocol
*/
/** Smallest legal byte size of a transaction. */
std::size_t constexpr kTX_MIN_SIZE_BYTES = 32;

/** Largest legal byte size of a transaction. */
std::size_t constexpr kTX_MAX_SIZE_BYTES = megabytes(1);

/** The maximum number of unfunded offers to delete at once */
std::size_t constexpr kUNFUNDED_OFFER_REMOVE_LIMIT = 1000;

/** The maximum number of expired offers to delete at once */
std::size_t constexpr kEXPIRED_OFFER_REMOVE_LIMIT = 256;

/** The maximum number of metadata entries allowed in one transaction */
std::size_t constexpr kOVERSIZE_META_DATA_CAP = 5200;

/** The maximum number of entries per directory page */
std::size_t constexpr kDIR_NODE_MAX_ENTRIES = 32;

/** The maximum number of pages allowed in a directory

    Made obsolete by fixDirectoryLimit amendment.
*/
std::uint64_t constexpr kDIR_NODE_MAX_PAGES = 262144;

/** The maximum number of items in an NFT page */
std::size_t constexpr kDIR_MAX_TOKENS_PER_PAGE = 32;

/** The maximum number of owner directory entries for account to be deletable */
std::size_t constexpr kMAX_DELETABLE_DIR_ENTRIES = 1000;

/** The maximum number of token offers that can be canceled at once */
std::size_t constexpr kMAX_TOKEN_OFFER_CANCEL_COUNT = 500;

/** The maximum number of offers in an offer directory for NFT to be burnable */
std::size_t constexpr kMAX_DELETABLE_TOKEN_OFFER_ENTRIES = 500;

/** The maximum token transfer fee allowed.

    Token transfer fees can range from 0% to 50% and are specified in tenths of
    a basis point; that is a value of 1000 represents a transfer fee of 1% and
    a value of 10000 represents a transfer fee of 10%.

    Note that for extremely low transfer fees values, it is possible that the
    calculated fee will be 0.
 */
std::uint16_t constexpr kMAX_TRANSFER_FEE = 50000;

/** There are 10,000 basis points (bips) in 100%.
 *
 * Basis points represent 0.01%.
 *
 * Given a value X, to find the amount for B bps,
 * use X * B / bipsPerUnity
 *
 * Example: If a loan broker has 999 XRP of debt, and must maintain 1,000 bps of
 * that debt as cover (10%), then the minimum cover amount is 999,000,000 drops
 * * 1000 / bipsPerUnity = 99,900,00 drops or 99.9 XRP.
 *
 * Given a percentage P, to find the number of bps that percentage represents,
 * use P * bipsPerUnity.
 *
 * Example: 50% is 0.50 * bipsPerUnity = 5,000 bps.
 */
Bips32 constexpr kBIPS_PER_UNITY(100 * 100);
static_assert(kBIPS_PER_UNITY == Bips32{10'000});
TenthBips32 constexpr kTENTH_BIPS_PER_UNITY(kBIPS_PER_UNITY.value() * 10);
static_assert(kTENTH_BIPS_PER_UNITY == TenthBips32(100'000));

constexpr Bips32
percentageToBips(std::uint32_t percentage)
{
    return Bips32(percentage * kBIPS_PER_UNITY.value() / 100);
}
constexpr TenthBips32
percentageToTenthBips(std::uint32_t percentage)
{
    return TenthBips32(percentage * kTENTH_BIPS_PER_UNITY.value() / 100);
}
template <typename T, class TBips>
constexpr T
bipsOfValue(T value, Bips<TBips> bips)
{
    return value * bips.value() / kBIPS_PER_UNITY.value();
}
template <typename T, class TBips>
constexpr T
tenthBipsOfValue(T value, TenthBips<TBips> bips)
{
    return value * bips.value() / kTENTH_BIPS_PER_UNITY.value();
}

namespace Lending {
/** The maximum management fee rate allowed by a loan broker in 1/10 bips.

    Valid values are between 0 and 10% inclusive.
*/
TenthBips16 constexpr kMAX_MANAGEMENT_FEE_RATE(
    unsafeCast<std::uint16_t>(percentageToTenthBips(10).value()));
static_assert(kMAX_MANAGEMENT_FEE_RATE == TenthBips16(std::uint16_t(10'000u)));

/** The maximum coverage rate required of a loan broker in 1/10 bips.

    Valid values are between 0 and 100% inclusive.
*/
TenthBips32 constexpr kMAX_COVER_RATE = percentageToTenthBips(100);
static_assert(kMAX_COVER_RATE == TenthBips32(100'000u));

/** The maximum overpayment fee on a loan in 1/10 bips.
*
    Valid values are between 0 and 100% inclusive.
*/
TenthBips32 constexpr kMAX_OVERPAYMENT_FEE = percentageToTenthBips(100);
static_assert(kMAX_OVERPAYMENT_FEE == TenthBips32(100'000u));

/** Annualized interest rate of the Loan in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr kMAX_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_INTEREST_RATE == TenthBips32(100'000u));

/** The maximum premium added to the interest rate for late payments on a loan
 * in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr kMAX_LATE_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_LATE_INTEREST_RATE == TenthBips32(100'000u));

/** The maximum close interest rate charged for repaying a loan early in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr kMAX_CLOSE_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_CLOSE_INTEREST_RATE == TenthBips32(100'000u));

/** The maximum overpayment interest rate charged on loan overpayments in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr kMAX_OVERPAYMENT_INTEREST_RATE = percentageToTenthBips(100);
static_assert(kMAX_OVERPAYMENT_INTEREST_RATE == TenthBips32(100'000u));

/** LoanPay transaction cost will be one base fee per X combined payments
 *
 * The number of payments is estimated based on the Amount paid and the Loan's
 * Fixed Payment size. Overpayments (indicated with the tfLoanOverpayment flag)
 * count as one more payment.
 *
 * This number was chosen arbitrarily, but should not be changed once released
 * without an amendment
 */
static constexpr int kLOAN_PAYMENTS_PER_FEE_INCREMENT = 5;

/** Maximum number of combined payments that a LoanPay transaction will process
 *
 * This limit is enforced during the loan payment process, and thus is not
 * estimated. If the limit is hit, no further payments or overpayments will be
 * processed, no matter how much of the transaction Amount is left, but the
 * transaction will succeed with the payments that have been processed up to
 * that point.
 *
 * This limit is independent of loanPaymentsPerFeeIncrement, so a transaction
 * could potentially be charged for many more payments than actually get
 * processed. Users should take care not to submit a transaction paying more
 * than loanMaximumPaymentsPerTransaction * Loan.PeriodicPayment. Because
 * overpayments are charged as a payment, if submitting
 * loanMaximumPaymentsPerTransaction * Loan.PeriodicPayment, users should not
 * set the tfLoanOverpayment flag.
 *
 * Even though they're independent, loanMaximumPaymentsPerTransaction should be
 * a multiple of loanPaymentsPerFeeIncrement.
 *
 * This number was chosen arbitrarily, but should not be changed once released
 * without an amendment
 */
static constexpr int kLOAN_MAXIMUM_PAYMENTS_PER_TRANSACTION = 100;
}  // namespace Lending

/** The maximum length of a URI inside an NFT */
std::size_t constexpr kMAX_TOKEN_URI_LENGTH = 256;

/** The maximum length of a Data element inside a DID */
std::size_t constexpr kMAX_DID_DOCUMENT_LENGTH = 256;

/** The maximum length of a URI inside a DID */
std::size_t constexpr kMAX_DIDURI_LENGTH = 256;

/** The maximum length of an Attestation inside a DID */
std::size_t constexpr kMAX_DID_DATA_LENGTH = 256;

/** The maximum length of a domain */
std::size_t constexpr kMAX_DOMAIN_LENGTH = 256;

/** The maximum length of a URI inside a Credential */
std::size_t constexpr kMAX_CREDENTIAL_URI_LENGTH = 256;

/** The maximum length of a CredentialType inside a Credential */
std::size_t constexpr kMAX_CREDENTIAL_TYPE_LENGTH = 64;

/** The maximum number of credentials can be passed in array */
std::size_t constexpr kMAX_CREDENTIALS_ARRAY_SIZE = 8;

/** The maximum number of credentials can be passed in array for permissioned
 * domain */
std::size_t constexpr kMAX_PERMISSIONED_DOMAIN_CREDENTIALS_ARRAY_SIZE = 10;

/** The maximum length of MPTokenMetadata */
std::size_t constexpr kMAX_MP_TOKEN_METADATA_LENGTH = 1024;

/** The maximum amount of MPTokenIssuance */
std::uint64_t constexpr kMAX_MP_TOKEN_AMOUNT = 0x7FFF'FFFF'FFFF'FFFFull;
static_assert(Number::kMAX_REP >= kMAX_MP_TOKEN_AMOUNT);

/** The maximum length of Data payload */
std::size_t constexpr kMAX_DATA_PAYLOAD_LENGTH = 256;

/** Vault withdrawal policies */
std::uint8_t constexpr kVAULT_STRATEGY_FIRST_COME_FIRST_SERVE = 1;

/** Default IOU scale factor for a Vault */
std::uint8_t constexpr kVAULT_DEFAULT_IOU_SCALE = 6;
/** Maximum scale factor for a Vault. The number is chosen to ensure that
1 IOU can be always converted to shares.
10^19 > maxMPTokenAmount (2^64-1) > 10^18 */
std::uint8_t constexpr kVAULT_MAXIMUM_IOU_SCALE = 18;

/** Maximum recursion depth for vault shares being put as an asset inside
 * another vault; counted from 0 */
std::uint8_t constexpr kMAX_ASSET_CHECK_DEPTH = 5;

/** A ledger index. */
using LedgerIndex = std::uint32_t;

std::uint32_t constexpr kFLAG_LEDGER_INTERVAL = 256;

/** Returns true if the given ledgerIndex is a voting ledgerIndex */
bool
isVotingLedger(LedgerIndex seq);

/** Returns true if the given ledgerIndex is a flag ledgerIndex */
bool
isFlagLedger(LedgerIndex seq);

/** A transaction identifier.
    The value is computed as the hash of the
    canonicalized, serialized transaction object.
*/
using TxID = uint256;

/** The maximum number of trustlines to delete as part of AMM account
 * deletion cleanup.
 */
std::uint16_t constexpr kMAX_DELETABLE_AMM_TRUST_LINES = 512;

/** The maximum length of a URI inside an Oracle */
std::size_t constexpr kMAX_ORACLE_URI = 256;

/** The maximum length of a Provider inside an Oracle */
std::size_t constexpr kMAX_ORACLE_PROVIDER = 256;

/** The maximum size of a data series array inside an Oracle */
std::size_t constexpr kMAX_ORACLE_DATA_SERIES = 10;

/** The maximum length of a SymbolClass inside an Oracle */
std::size_t constexpr kMAX_ORACLE_SYMBOL_CLASS = 16;

/** The maximum allowed time difference between lastUpdateTime and the time
    of the last closed ledger
*/
std::size_t constexpr kMAX_LAST_UPDATE_TIME_DELTA = 300;

/** The maximum price scaling factor
 */
std::size_t constexpr kMAX_PRICE_SCALE = 20;

/** The maximum percentage of outliers to trim
 */
std::size_t constexpr kMAX_TRIM = 25;

/** The maximum number of delegate permissions an account can grant
 */
std::size_t constexpr kPERMISSION_MAX_SIZE = 10;

/** The maximum number of transactions that can be in a batch. */
std::size_t constexpr kMAX_BATCH_TX_COUNT = 8;

}  // namespace xrpl
