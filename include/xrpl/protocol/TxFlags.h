#ifndef XRPL_PROTOCOL_TXFLAGS_H_INCLUDED
#define XRPL_PROTOCOL_TXFLAGS_H_INCLUDED

#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

namespace ripple {

/** Transaction flags.

    These flags are specified in a transaction's 'Flags' field and modify the
    behavior of that transaction.

    There are two types of flags:

        (1) Universal flags: these are flags which apply to, and are interpreted
                             the same way by, all transactions, except, perhaps,
                             to special pseudo-transactions.

        (2) Tx-Specific flags: these are flags which are interpreted according
                               to the type of the transaction being executed.
                               That is, the same numerical flag value may have
                               different effects, depending on the transaction
                               being executed.

    @note The universal transaction flags occupy the high-order 8 bits. The
          tx-specific flags occupy the remaining 24 bits.

    @warning Transaction flags form part of the protocol. **Changing them
             should be avoided because without special handling, this will
             result in a hard fork.**

    @ingroup protocol
*/

// Formatting equals sign aligned 4 spaces after longest prefix, except for
// wrapped lines
// clang-format off
// Universal Transaction flags:
constexpr std::uint32_t tfFullyCanonicalSig                = 0x80000000;
constexpr std::uint32_t tfInnerBatchTxn                    = 0x40000000;
constexpr std::uint32_t tfUniversal                        = tfFullyCanonicalSig | tfInnerBatchTxn;
constexpr std::uint32_t tfUniversalMask                    = ~tfUniversal;

// AccountSet flags:
constexpr std::uint32_t tfRequireDestTag                   = 0x00010000;
constexpr std::uint32_t tfOptionalDestTag                  = 0x00020000;
constexpr std::uint32_t tfRequireAuth                      = 0x00040000;
constexpr std::uint32_t tfOptionalAuth                     = 0x00080000;
constexpr std::uint32_t tfDisallowXRP                      = 0x00100000;
constexpr std::uint32_t tfAllowXRP                         = 0x00200000;
constexpr std::uint32_t tfAccountSetMask =
    ~(tfUniversal | tfRequireDestTag | tfOptionalDestTag | tfRequireAuth |
      tfOptionalAuth | tfDisallowXRP | tfAllowXRP);

// AccountSet SetFlag/ClearFlag values
constexpr std::uint32_t asfRequireDest                     =  1;
constexpr std::uint32_t asfRequireAuth                     =  2;
constexpr std::uint32_t asfDisallowXRP                     =  3;
constexpr std::uint32_t asfDisableMaster                   =  4;
constexpr std::uint32_t asfAccountTxnID                    =  5;
constexpr std::uint32_t asfNoFreeze                        =  6;
constexpr std::uint32_t asfGlobalFreeze                    =  7;
constexpr std::uint32_t asfDefaultRipple                   =  8;
constexpr std::uint32_t asfDepositAuth                     =  9;
constexpr std::uint32_t asfAuthorizedNFTokenMinter         = 10;
/*  // reserved for Hooks amendment
constexpr std::uint32_t asfTshCollect                      = 11;
*/
constexpr std::uint32_t asfDisallowIncomingNFTokenOffer    = 12;
constexpr std::uint32_t asfDisallowIncomingCheck           = 13;
constexpr std::uint32_t asfDisallowIncomingPayChan         = 14;
constexpr std::uint32_t asfDisallowIncomingTrustline       = 15;
constexpr std::uint32_t asfAllowTrustLineClawback          = 16;
constexpr std::uint32_t asfAllowTrustLineLocking           = 17;

// OfferCreate flags:
constexpr std::uint32_t tfPassive                          = 0x00010000;
constexpr std::uint32_t tfImmediateOrCancel                = 0x00020000;
constexpr std::uint32_t tfFillOrKill                       = 0x00040000;
constexpr std::uint32_t tfSell                             = 0x00080000;
constexpr std::uint32_t tfHybrid                           = 0x00100000;
constexpr std::uint32_t tfOfferCreateMask =
    ~(tfUniversal | tfPassive | tfImmediateOrCancel | tfFillOrKill | tfSell | tfHybrid);

// Payment flags:
constexpr std::uint32_t tfNoRippleDirect                   = 0x00010000;
constexpr std::uint32_t tfPartialPayment                   = 0x00020000;
constexpr std::uint32_t tfLimitQuality                     = 0x00040000;
constexpr std::uint32_t tfPaymentMask =
    ~(tfUniversal | tfPartialPayment | tfLimitQuality | tfNoRippleDirect);
constexpr std::uint32_t tfMPTPaymentMask = ~(tfUniversal | tfPartialPayment);

// TrustSet flags:
constexpr std::uint32_t tfSetfAuth                         = 0x00010000;
constexpr std::uint32_t tfSetNoRipple                      = 0x00020000;
constexpr std::uint32_t tfClearNoRipple                    = 0x00040000;
constexpr std::uint32_t tfSetFreeze                        = 0x00100000;
constexpr std::uint32_t tfClearFreeze                      = 0x00200000;
constexpr std::uint32_t tfSetDeepFreeze                    = 0x00400000;
constexpr std::uint32_t tfClearDeepFreeze                  = 0x00800000;
constexpr std::uint32_t tfTrustSetMask =
    ~(tfUniversal | tfSetfAuth | tfSetNoRipple | tfClearNoRipple | tfSetFreeze |
      tfClearFreeze | tfSetDeepFreeze | tfClearDeepFreeze);
constexpr std::uint32_t tfTrustSetPermissionMask = ~(tfUniversal | tfSetfAuth | tfSetFreeze | tfClearFreeze);

// EnableAmendment flags:
constexpr std::uint32_t tfGotMajority                      = 0x00010000;
constexpr std::uint32_t tfLostMajority                     = 0x00020000;
constexpr std::uint32_t tfChangeMask =
    ~( tfUniversal | tfGotMajority | tfLostMajority);

// PaymentChannelClaim flags:
constexpr std::uint32_t tfRenew                            = 0x00010000;
constexpr std::uint32_t tfClose                            = 0x00020000;
constexpr std::uint32_t tfPayChanClaimMask = ~(tfUniversal | tfRenew | tfClose);

// NFTokenMint flags:
constexpr std::uint32_t const tfBurnable                   = 0x00000001;
constexpr std::uint32_t const tfOnlyXRP                    = 0x00000002;
constexpr std::uint32_t const tfTrustLine                  = 0x00000004;
constexpr std::uint32_t const tfTransferable               = 0x00000008;
constexpr std::uint32_t const tfMutable                    = 0x00000010;

// MPTokenIssuanceCreate flags:
// Note: tf/lsfMPTLocked is intentionally omitted, since this transaction
// is not allowed to modify it.
constexpr std::uint32_t const tfMPTCanLock                 = lsfMPTCanLock;
constexpr std::uint32_t const tfMPTRequireAuth             = lsfMPTRequireAuth;
constexpr std::uint32_t const tfMPTCanEscrow               = lsfMPTCanEscrow;
constexpr std::uint32_t const tfMPTCanTrade                = lsfMPTCanTrade;
constexpr std::uint32_t const tfMPTCanTransfer             = lsfMPTCanTransfer;
constexpr std::uint32_t const tfMPTCanClawback             = lsfMPTCanClawback;
constexpr std::uint32_t const tfMPTokenIssuanceCreateMask  =
  ~(tfUniversal | tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow | tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback);

// MPTokenIssuanceCreate MutableFlags:
// Indicating specific fields or flags may be changed after issuance.
constexpr std::uint32_t const tmfMPTCanMutateCanLock = lsmfMPTCanMutateCanLock;
constexpr std::uint32_t const tmfMPTCanMutateRequireAuth = lsmfMPTCanMutateRequireAuth;
constexpr std::uint32_t const tmfMPTCanMutateCanEscrow = lsmfMPTCanMutateCanEscrow;
constexpr std::uint32_t const tmfMPTCanMutateCanTrade = lsmfMPTCanMutateCanTrade;
constexpr std::uint32_t const tmfMPTCanMutateCanTransfer = lsmfMPTCanMutateCanTransfer;
constexpr std::uint32_t const tmfMPTCanMutateCanClawback = lsmfMPTCanMutateCanClawback;
constexpr std::uint32_t const tmfMPTCanMutateMetadata = lsmfMPTCanMutateMetadata;
constexpr std::uint32_t const tmfMPTCanMutateTransferFee = lsmfMPTCanMutateTransferFee;
constexpr std::uint32_t const tmfMPTokenIssuanceCreateMutableMask =
  ~(tmfMPTCanMutateCanLock | tmfMPTCanMutateRequireAuth | tmfMPTCanMutateCanEscrow | tmfMPTCanMutateCanTrade
    | tmfMPTCanMutateCanTransfer | tmfMPTCanMutateCanClawback | tmfMPTCanMutateMetadata | tmfMPTCanMutateTransferFee);

// MPTokenAuthorize flags:
constexpr std::uint32_t const tfMPTUnauthorize             = 0x00000001;
constexpr std::uint32_t const tfMPTokenAuthorizeMask  = ~(tfUniversal | tfMPTUnauthorize);

// MPTokenIssuanceSet flags:
constexpr std::uint32_t const tfMPTLock                   = 0x00000001;
constexpr std::uint32_t const tfMPTUnlock                 = 0x00000002;
constexpr std::uint32_t const tfMPTokenIssuanceSetMask  = ~(tfUniversal | tfMPTLock | tfMPTUnlock);
constexpr std::uint32_t const tfMPTokenIssuanceSetPermissionMask = ~(tfUniversal | tfMPTLock | tfMPTUnlock);

// MPTokenIssuanceSet MutableFlags:
// Set or Clear flags.
constexpr std::uint32_t const tmfMPTSetCanLock             = 0x00000001;
constexpr std::uint32_t const tmfMPTClearCanLock           = 0x00000002;
constexpr std::uint32_t const tmfMPTSetRequireAuth         = 0x00000004;
constexpr std::uint32_t const tmfMPTClearRequireAuth       = 0x00000008;
constexpr std::uint32_t const tmfMPTSetCanEscrow           = 0x00000010;
constexpr std::uint32_t const tmfMPTClearCanEscrow         = 0x00000020;
constexpr std::uint32_t const tmfMPTSetCanTrade            = 0x00000040;
constexpr std::uint32_t const tmfMPTClearCanTrade          = 0x00000080;
constexpr std::uint32_t const tmfMPTSetCanTransfer         = 0x00000100;
constexpr std::uint32_t const tmfMPTClearCanTransfer       = 0x00000200;
constexpr std::uint32_t const tmfMPTSetCanClawback         = 0x00000400;
constexpr std::uint32_t const tmfMPTClearCanClawback       = 0x00000800;
constexpr std::uint32_t const tmfMPTokenIssuanceSetMutableMask = ~(tmfMPTSetCanLock | tmfMPTClearCanLock |
    tmfMPTSetRequireAuth | tmfMPTClearRequireAuth | tmfMPTSetCanEscrow | tmfMPTClearCanEscrow |
    tmfMPTSetCanTrade | tmfMPTClearCanTrade | tmfMPTSetCanTransfer | tmfMPTClearCanTransfer |
    tmfMPTSetCanClawback | tmfMPTClearCanClawback);

// MPTokenIssuanceDestroy flags:
constexpr std::uint32_t const tfMPTokenIssuanceDestroyMask  = ~tfUniversal;

// Prior to fixRemoveNFTokenAutoTrustLine, transfer of an NFToken between
// accounts allowed a TrustLine to be added to the issuer of that token
// without explicit permission from that issuer.  This was enabled by
// minting the NFToken with the tfTrustLine flag set.
//
// That capability could be used to attack the NFToken issuer.  It
// would be possible for two accounts to trade the NFToken back and forth
// building up any number of TrustLines on the issuer, increasing the
// issuer's reserve without bound.
//
// The fixRemoveNFTokenAutoTrustLine amendment disables minting with the
// tfTrustLine flag as a way to prevent the attack.  But until the
// amendment passes we still need to keep the old behavior available.
constexpr std::uint32_t const tfNFTokenMintMask =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable);

constexpr std::uint32_t const tfNFTokenMintOldMask =
    ~( ~tfNFTokenMintMask | tfTrustLine);

// if featureDynamicNFT enabled then new flag allowing mutable URI available.
constexpr std::uint32_t const tfNFTokenMintOldMaskWithMutable =
    ~( ~tfNFTokenMintOldMask | tfMutable);

constexpr std::uint32_t const tfNFTokenMintMaskWithMutable =
    ~( ~tfNFTokenMintMask | tfMutable);

// NFTokenCreateOffer flags:
constexpr std::uint32_t const tfSellNFToken                = 0x00000001;
constexpr std::uint32_t const tfNFTokenCreateOfferMask =
    ~(tfUniversal | tfSellNFToken);

// NFTokenCancelOffer flags:
constexpr std::uint32_t const tfNFTokenCancelOfferMask     = ~tfUniversal;

// NFTokenAcceptOffer flags:
constexpr std::uint32_t const tfNFTokenAcceptOfferMask     = ~tfUniversal;

// Clawback flags:
constexpr std::uint32_t const tfClawbackMask               = ~tfUniversal;

// AMM Flags:
constexpr std::uint32_t tfLPToken                          = 0x00010000;
constexpr std::uint32_t tfWithdrawAll                      = 0x00020000;
constexpr std::uint32_t tfOneAssetWithdrawAll              = 0x00040000;
constexpr std::uint32_t tfSingleAsset                      = 0x00080000;
constexpr std::uint32_t tfTwoAsset                         = 0x00100000;
constexpr std::uint32_t tfOneAssetLPToken                  = 0x00200000;
constexpr std::uint32_t tfLimitLPToken                     = 0x00400000;
constexpr std::uint32_t tfTwoAssetIfEmpty                  = 0x00800000;
constexpr std::uint32_t tfWithdrawSubTx =
    tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken |
    tfLimitLPToken | tfWithdrawAll | tfOneAssetWithdrawAll;
constexpr std::uint32_t tfDepositSubTx =
    tfLPToken | tfSingleAsset | tfTwoAsset | tfOneAssetLPToken |
    tfLimitLPToken | tfTwoAssetIfEmpty;
constexpr std::uint32_t tfWithdrawMask = ~(tfUniversal | tfWithdrawSubTx);
constexpr std::uint32_t tfDepositMask = ~(tfUniversal | tfDepositSubTx);

// AMMClawback flags:
constexpr std::uint32_t tfClawTwoAssets                = 0x00000001;
constexpr std::uint32_t tfAMMClawbackMask = ~(tfUniversal | tfClawTwoAssets);

// BridgeModify flags:
constexpr std::uint32_t tfClearAccountCreateAmount     = 0x00010000;
constexpr std::uint32_t tfBridgeModifyMask = ~(tfUniversal | tfClearAccountCreateAmount);

// VaultCreate flags:
constexpr std::uint32_t const tfVaultPrivate               = 0x00010000;
static_assert(tfVaultPrivate == lsfVaultPrivate);
constexpr std::uint32_t const tfVaultShareNonTransferable  = 0x00020000;
constexpr std::uint32_t const tfVaultCreateMask = ~(tfUniversal | tfVaultPrivate | tfVaultShareNonTransferable);

// Batch Flags:
constexpr std::uint32_t tfAllOrNothing                 = 0x00010000;
constexpr std::uint32_t tfOnlyOne                      = 0x00020000;
constexpr std::uint32_t tfUntilFailure                 = 0x00040000;
constexpr std::uint32_t tfIndependent                  = 0x00080000;
/**
 * @note If nested Batch transactions are supported in the future, the tfInnerBatchTxn flag
 *  will need to be removed from this mask to allow Batch transaction to be inside
 *  the sfRawTransactions array.
 */
constexpr std::uint32_t const tfBatchMask =
    ~(tfUniversal | tfAllOrNothing | tfOnlyOne | tfUntilFailure | tfIndependent) | tfInnerBatchTxn;

// LoanSet and LoanPay flags:
// LoanSet: True, indicates the loan supports overpayments
// LoanPay: True, indicates any excess in this payment can be used
// as an overpayment. False, no overpayments will be taken.
constexpr std::uint32_t const tfLoanOverpayment = 0x00010000;
// LoanPay exclusive flags:
// tfLoanFullPayment: True, indicates that the payment is an early
// full payment. It must pay the entire loan including close
// interest and fees, or it will fail. False: Not a full payment.
constexpr std::uint32_t const tfLoanFullPayment = 0x00020000;
// tfLoanLatePayment: True, indicates that the payment is late,
// and includes late iterest and fees. If the loan is not late,
// it will fail. False: not a late payment. If the current payment
// is overdue, the transaction will fail.
constexpr std::uint32_t const tfLoanLatePayment = 0x00040000;
constexpr std::uint32_t const tfLoanSetMask = ~(tfUniversal |
    tfLoanOverpayment);
constexpr std::uint32_t const tfLoanPayMask = ~(tfUniversal |
    tfLoanOverpayment | tfLoanFullPayment | tfLoanLatePayment);

// LoanManage flags:
constexpr std::uint32_t const tfLoanDefault = 0x00010000;
constexpr std::uint32_t const tfLoanImpair = 0x00020000;
constexpr std::uint32_t const tfLoanUnimpair = 0x00040000;
constexpr std::uint32_t const tfLoanManageMask = ~(tfUniversal | tfLoanDefault | tfLoanImpair | tfLoanUnimpair);

// clang-format on

}  // namespace ripple

#endif
