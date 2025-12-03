#ifndef XRPL_PROTOCOL_PROTOCOL_H_INCLUDED
#define XRPL_PROTOCOL_PROTOCOL_H_INCLUDED

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>

namespace ripple {

/** Protocol specific constants.

    This information is, implicitly, part of the protocol.

    @note Changing these values without adding code to the
          server to detect "pre-change" and "post-change"
          will result in a hard fork.

    @ingroup protocol
*/
/** Smallest legal byte size of a transaction. */
std::size_t constexpr txMinSizeBytes = 32;

/** Largest legal byte size of a transaction. */
std::size_t constexpr txMaxSizeBytes = megabytes(1);

/** The maximum number of unfunded offers to delete at once */
std::size_t constexpr unfundedOfferRemoveLimit = 1000;

/** The maximum number of expired offers to delete at once */
std::size_t constexpr expiredOfferRemoveLimit = 256;

/** The maximum number of metadata entries allowed in one transaction */
std::size_t constexpr oversizeMetaDataCap = 5200;

/** The maximum number of entries per directory page */
std::size_t constexpr dirNodeMaxEntries = 32;

/** The maximum number of pages allowed in a directory

    Made obsolete by fixDirectoryLimit amendment.
*/
std::uint64_t constexpr dirNodeMaxPages = 262144;

/** The maximum number of items in an NFT page */
std::size_t constexpr dirMaxTokensPerPage = 32;

/** The maximum number of owner directory entries for account to be deletable */
std::size_t constexpr maxDeletableDirEntries = 1000;

/** The maximum number of token offers that can be canceled at once */
std::size_t constexpr maxTokenOfferCancelCount = 500;

/** The maximum number of offers in an offer directory for NFT to be burnable */
std::size_t constexpr maxDeletableTokenOfferEntries = 500;

/** The maximum token transfer fee allowed.

    Token transfer fees can range from 0% to 50% and are specified in tenths of
    a basis point; that is a value of 1000 represents a transfer fee of 1% and
    a value of 10000 represents a transfer fee of 10%.

    Note that for extremely low transfer fees values, it is possible that the
    calculated fee will be 0.
 */
std::uint16_t constexpr maxTransferFee = 50000;

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
Bips32 constexpr bipsPerUnity(100 * 100);
static_assert(bipsPerUnity == Bips32{10'000});
TenthBips32 constexpr tenthBipsPerUnity(bipsPerUnity.value() * 10);
static_assert(tenthBipsPerUnity == TenthBips32(100'000));

constexpr Bips32
percentageToBips(std::uint32_t percentage)
{
    return Bips32(percentage * bipsPerUnity.value() / 100);
}
constexpr TenthBips32
percentageToTenthBips(std::uint32_t percentage)
{
    return TenthBips32(percentage * tenthBipsPerUnity.value() / 100);
}
template <typename T, class TBips>
constexpr T
bipsOfValue(T value, Bips<TBips> bips)
{
    return value * bips.value() / bipsPerUnity.value();
}
template <typename T, class TBips>
constexpr T
tenthBipsOfValue(T value, TenthBips<TBips> bips)
{
    return value * bips.value() / tenthBipsPerUnity.value();
}

namespace Lending {
/** The maximum management fee rate allowed by a loan broker in 1/10 bips.

    Valid values are between 0 and 10% inclusive.
*/
TenthBips16 constexpr maxManagementFeeRate(
    unsafe_cast<std::uint16_t>(percentageToTenthBips(10).value()));
static_assert(maxManagementFeeRate == TenthBips16(std::uint16_t(10'000u)));

/** The maximum coverage rate required of a loan broker in 1/10 bips.

    Valid values are between 0 and 100% inclusive.
*/
TenthBips32 constexpr maxCoverRate = percentageToTenthBips(100);
static_assert(maxCoverRate == TenthBips32(100'000u));

/** The maximum overpayment fee on a loan in 1/10 bips.
*
    Valid values are between 0 and 100% inclusive.
*/
TenthBips32 constexpr maxOverpaymentFee = percentageToTenthBips(100);
static_assert(maxOverpaymentFee == TenthBips32(100'000u));

/** Annualized interest rate of the Loan in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr maxInterestRate = percentageToTenthBips(100);
static_assert(maxInterestRate == TenthBips32(100'000u));

/** The maximum premium added to the interest rate for late payments on a loan
 * in 1/10 bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr maxLateInterestRate = percentageToTenthBips(100);
static_assert(maxLateInterestRate == TenthBips32(100'000u));

/** The maximum close interest rate charged for repaying a loan early in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr maxCloseInterestRate = percentageToTenthBips(100);
static_assert(maxCloseInterestRate == TenthBips32(100'000u));

/** The maximum overpayment interest rate charged on loan overpayments in 1/10
 * bips.
 *
 * Valid values are between 0 and 100% inclusive.
 */
TenthBips32 constexpr maxOverpaymentInterestRate = percentageToTenthBips(100);
static_assert(maxOverpaymentInterestRate == TenthBips32(100'000u));

/** LoanPay transaction cost will be one base fee per X combined payments
 *
 * The number of payments is estimated based on the Amount paid and the Loan's
 * Fixed Payment size. Overpayments (indicated with the tfLoanOverpayment flag)
 * count as one more payment.
 *
 * This number was chosen arbitrarily, but should not be changed once released
 * without an amendment
 */
static constexpr int loanPaymentsPerFeeIncrement = 5;

/** Maximum number of combined payments that a LoanPay transaction will process
 *
 * This limit is enforced during the loan payment process, and thus is not
 * estimated. If the limit is hit, no further payments or overpayments will be
 * processed, no matter how much of the transation Amount is left, but the
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
static constexpr int loanMaximumPaymentsPerTransaction = 100;
}  // namespace Lending

/** The maximum length of a URI inside an NFT */
std::size_t constexpr maxTokenURILength = 256;

/** The maximum length of a Data element inside a DID */
std::size_t constexpr maxDIDDocumentLength = 256;

/** The maximum length of a URI inside a DID */
std::size_t constexpr maxDIDURILength = 256;

/** The maximum length of an Attestation inside a DID */
std::size_t constexpr maxDIDAttestationLength = 256;

/** The maximum length of a domain */
std::size_t constexpr maxDomainLength = 256;

/** The maximum length of a URI inside a Credential */
std::size_t constexpr maxCredentialURILength = 256;

/** The maximum length of a CredentialType inside a Credential */
std::size_t constexpr maxCredentialTypeLength = 64;

/** The maximum number of credentials can be passed in array */
std::size_t constexpr maxCredentialsArraySize = 8;

/** The maximum number of credentials can be passed in array for permissioned
 * domain */
std::size_t constexpr maxPermissionedDomainCredentialsArraySize = 10;

/** The maximum length of MPTokenMetadata */
std::size_t constexpr maxMPTokenMetadataLength = 1024;

/** The maximum amount of MPTokenIssuance */
std::uint64_t constexpr maxMPTokenAmount = 0x7FFF'FFFF'FFFF'FFFFull;

/** The maximum length of Data payload */
std::size_t constexpr maxDataPayloadLength = 256;

/** Vault withdrawal policies */
std::uint8_t constexpr vaultStrategyFirstComeFirstServe = 1;

/** Default IOU scale factor for a Vault */
std::uint8_t constexpr vaultDefaultIOUScale = 6;
/** Maximum scale factor for a Vault. The number is chosen to ensure that
1 IOU can be always converted to shares.
10^19 > maxMPTokenAmount (2^64-1) > 10^18 */
std::uint8_t constexpr vaultMaximumIOUScale = 18;

/** Maximum recursion depth for vault shares being put as an asset inside
 * another vault; counted from 0 */
std::uint8_t constexpr maxAssetCheckDepth = 5;

/** A ledger index. */
using LedgerIndex = std::uint32_t;

/** A transaction identifier.
    The value is computed as the hash of the
    canonicalized, serialized transaction object.
*/
using TxID = uint256;

/** The maximum number of trustlines to delete as part of AMM account
 * deletion cleanup.
 */
std::uint16_t constexpr maxDeletableAMMTrustLines = 512;

/** The maximum length of a URI inside an Oracle */
std::size_t constexpr maxOracleURI = 256;

/** The maximum length of a Provider inside an Oracle */
std::size_t constexpr maxOracleProvider = 256;

/** The maximum size of a data series array inside an Oracle */
std::size_t constexpr maxOracleDataSeries = 10;

/** The maximum length of a SymbolClass inside an Oracle */
std::size_t constexpr maxOracleSymbolClass = 16;

/** The maximum allowed time difference between lastUpdateTime and the time
    of the last closed ledger
*/
std::size_t constexpr maxLastUpdateTimeDelta = 300;

/** The maximum price scaling factor
 */
std::size_t constexpr maxPriceScale = 20;

/** The maximum percentage of outliers to trim
 */
std::size_t constexpr maxTrim = 25;

/** The maximum number of delegate permissions an account can grant
 */
std::size_t constexpr permissionMaxSize = 10;

/** The maximum number of transactions that can be in a batch. */
std::size_t constexpr maxBatchTxCount = 8;

}  // namespace ripple

#endif
