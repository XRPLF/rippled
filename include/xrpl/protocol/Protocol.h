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
constexpr std::size_t kTxMinSizeBytes = 32;

/** Largest legal byte size of a transaction. */
constexpr std::size_t kTxMaxSizeBytes = megabytes(1);

/** The maximum number of unfunded offers to delete at once */
constexpr std::size_t kUnfundedOfferRemoveLimit = 1000;

/** The maximum number of expired offers to delete at once */
constexpr std::size_t kExpiredOfferRemoveLimit = 256;

/** The maximum number of metadata entries allowed in one transaction */
constexpr std::size_t kOversizeMetaDataCap = 5200;

/** The maximum number of entries per directory page */
constexpr std::size_t kDirNodeMaxEntries = 32;

/** The maximum number of pages allowed in a directory

    Made obsolete by fixDirectoryLimit amendment.
*/
constexpr std::uint64_t kDirNodeMaxPages = 262144;

/** The maximum number of items in an NFT page */
constexpr std::size_t kDirMaxTokensPerPage = 32;

/** The maximum number of owner directory entries for account to be deletable */
constexpr std::size_t kMaxDeletableDirEntries = 1000;

/** The maximum number of token offers that can be canceled at once */
constexpr std::size_t kMaxTokenOfferCancelCount = 500;

/** The maximum number of offers in an offer directory for NFT to be burnable */
constexpr std::size_t kMaxDeletableTokenOfferEntries = 500;

/** The maximum token transfer fee allowed.

    Token transfer fees can range from 0% to 50% and are specified in tenths of
    a basis point; that is a value of 1000 represents a transfer fee of 1% and
    a value of 10000 represents a transfer fee of 10%.

    Note that for extremely low transfer fees values, it is possible that the
    calculated fee will be 0.
 */
constexpr std::uint16_t kMaxTransferFee = 50000;

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
constexpr Bips32 kBipsPerUnity(100 * 100);
static_assert(kBipsPerUnity == Bips32{10'000});
constexpr TenthBips32 kTenthBipsPerUnity(kBipsPerUnity.value() * 10);
static_assert(kTenthBipsPerUnity == TenthBips32(100'000));

constexpr Bips32
percentageToBips(std::uint32_t percentage)
{
    return Bips32(percentage * kBipsPerUnity.value() / 100);
}
constexpr TenthBips32
percentageToTenthBips(std::uint32_t percentage)
{
    return TenthBips32(percentage * kTenthBipsPerUnity.value() / 100);
}
template <typename T, class TBips>
constexpr T
bipsOfValue(T value, Bips<TBips> bips)
{
    return value * bips.value() / kBipsPerUnity.value();
}
template <typename T, class TBips>
constexpr T
tenthBipsOfValue(T value, TenthBips<TBips> bips)
{
    return value * bips.value() / kTenthBipsPerUnity.value();
}

namespace Lending {
/** The maximum management fee rate allowed by a loan broker in 1/10 bips.

    Valid values are between 0 and 10% inclusive.
*/
constexpr TenthBips16 kMaxManagementFeeRate(
    unsafeCast<std::uint16_t>(percentageToTenthBips(10).value()));
static_assert(kMaxManagementFeeRate == TenthBips16(std::uint16_t(10'000u)));

/** The maximum coverage rate required of a loan broker in 1/10 bips.

    Valid values are between 0 and 100% inclusive.
*/
constexpr TenthBips32 kMaxCoverRate = percentageToTenthBips(100);
static_assert(kMaxCoverRate == TenthBips32(100'000u));

/** The maximum overpayment fee on a loan in 1/10 bips.
*
    Valid values are between 0 and 100% inclusive.
*/
constexpr TenthBips32 kMaxOverpaymentFee = percentageToTenthBips(100);
static_assert(kMaxOverpaymentFee == TenthBips32(100'000u));

/** Annualized interest rate of the Loan in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
constexpr TenthBips32 kMaxInterestRate = percentageToTenthBips(100);
static_assert(kMaxInterestRate == TenthBips32(100'000u));

/** The maximum premium added to the interest rate for late payments on a loan
 * in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
constexpr TenthBips32 kMaxLateInterestRate = percentageToTenthBips(100);
static_assert(kMaxLateInterestRate == TenthBips32(100'000u));

/** The maximum close interest rate charged for repaying a loan early in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
constexpr TenthBips32 kMaxCloseInterestRate = percentageToTenthBips(100);
static_assert(kMaxCloseInterestRate == TenthBips32(100'000u));

/** The maximum overpayment interest rate charged on loan overpayments in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
constexpr TenthBips32 kMaxOverpaymentInterestRate = percentageToTenthBips(100);
static_assert(kMaxOverpaymentInterestRate == TenthBips32(100'000u));

/** LoanPay transaction cost will be one base fee per X combined payments
 *
 * The number of payments is estimated based on the Amount paid and the Loan's
 * Fixed Payment size. Overpayments (indicated with the tfLoanOverpayment flag)
 * count as one more payment.
 *
 * This number was chosen arbitrarily, but should not be changed once released
 * without an amendment
 */
static constexpr int kLoanPaymentsPerFeeIncrement = 5;

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
static constexpr int kLoanMaximumPaymentsPerTransaction = 100;
}  // namespace Lending

/** The maximum length of a URI inside an NFT */
constexpr std::size_t kMaxTokenUriLength = 256;

/** The maximum length of a Data element inside a DID */
constexpr std::size_t kMaxDidDocumentLength = 256;

/** The maximum length of a URI inside a DID */
constexpr std::size_t kMaxDidUriLength = 256;

/** The maximum length of an Attestation inside a DID */
constexpr std::size_t kMaxDidDataLength = 256;

/** The maximum length of a domain */
constexpr std::size_t kMaxDomainLength = 256;

/** The maximum length of a URI inside a Credential */
constexpr std::size_t kMaxCredentialUriLength = 256;

/** The maximum length of a CredentialType inside a Credential */
constexpr std::size_t kMaxCredentialTypeLength = 64;

/** The maximum number of credentials can be passed in array */
constexpr std::size_t kMaxCredentialsArraySize = 8;

/** The maximum number of credentials can be passed in array for permissioned
 * domain */
constexpr std::size_t kMaxPermissionedDomainCredentialsArraySize = 10;

/** The maximum length of MPTokenMetadata */
constexpr std::size_t kMaxMpTokenMetadataLength = 1024;

/** The maximum amount of MPTokenIssuance */
constexpr std::uint64_t kMaxMpTokenAmount = 0x7FFF'FFFF'FFFF'FFFFull;
static_assert(Number::kMaxRep >= kMaxMpTokenAmount);

/** The maximum length of Data payload */
constexpr std::size_t kMaxDataPayloadLength = 256;

/** Vault withdrawal policies */
constexpr std::uint8_t kVaultStrategyFirstComeFirstServe = 1;

/** Default IOU scale factor for a Vault */
constexpr std::uint8_t kVaultDefaultIouScale = 6;
/** Maximum scale factor for a Vault. The number is chosen to ensure that
1 IOU can be always converted to shares.
10^19 > maxMPTokenAmount (2^64-1) > 10^18 */
constexpr std::uint8_t kVaultMaximumIouScale = 18;

/** Maximum recursion depth for vault shares being put as an asset inside
 * another vault; counted from 0 */
constexpr std::uint8_t kMaxAssetCheckDepth = 5;

/** A ledger index. */
using LedgerIndex = std::uint32_t;

constexpr std::uint32_t kFlagLedgerInterval = 256;

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
constexpr std::uint16_t kMaxDeletableAmmTrustLines = 512;

/** The maximum length of a URI inside an Oracle */
constexpr std::size_t kMaxOracleUri = 256;

/** The maximum length of a Provider inside an Oracle */
constexpr std::size_t kMaxOracleProvider = 256;

/** The maximum size of a data series array inside an Oracle */
constexpr std::size_t kMaxOracleDataSeries = 10;

/** The maximum length of a SymbolClass inside an Oracle */
constexpr std::size_t kMaxOracleSymbolClass = 16;

/** The maximum allowed time difference between lastUpdateTime and the time
    of the last closed ledger
*/
constexpr std::size_t kMaxLastUpdateTimeDelta = 300;

/** The maximum price scaling factor
 */
constexpr std::size_t kMaxPriceScale = 20;

/** The maximum percentage of outliers to trim
 */
constexpr std::size_t kMaxTrim = 25;

/** The maximum number of delegate permissions an account can grant
 */
constexpr std::size_t kPermissionMaxSize = 10;

/** The maximum number of transactions that can be in a batch. */
constexpr std::size_t kMaxBatchTxCount = 8;

}  // namespace xrpl
