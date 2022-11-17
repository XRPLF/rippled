//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_TXFLAGS_H_INCLUDED
#define RIPPLE_PROTOCOL_TXFLAGS_H_INCLUDED

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

// clang-format off
// Universal Transaction flags:
constexpr std::uint32_t tfFullyCanonicalSig            = 0x80000000;
constexpr std::uint32_t tfUniversal                    = tfFullyCanonicalSig;
constexpr std::uint32_t tfUniversalMask                = ~tfUniversal;

// AccountSet flags:
constexpr std::uint32_t tfRequireDestTag               = 0x00010000;
constexpr std::uint32_t tfOptionalDestTag              = 0x00020000;
constexpr std::uint32_t tfRequireAuth                  = 0x00040000;
constexpr std::uint32_t tfOptionalAuth                 = 0x00080000;
constexpr std::uint32_t tfDisallowXRP                  = 0x00100000;
constexpr std::uint32_t tfAllowXRP                     = 0x00200000;
constexpr std::uint32_t tfAccountSetMask =
    ~(tfUniversal | tfRequireDestTag | tfOptionalDestTag | tfRequireAuth |
      tfOptionalAuth | tfDisallowXRP | tfAllowXRP);

// AccountSet SetFlag/ClearFlag values
constexpr std::uint32_t asfRequireDest                 =  1;
constexpr std::uint32_t asfRequireAuth                 =  2;
constexpr std::uint32_t asfDisallowXRP                 =  3;
constexpr std::uint32_t asfDisableMaster               =  4;
constexpr std::uint32_t asfAccountTxnID                =  5;
constexpr std::uint32_t asfNoFreeze                    =  6;
constexpr std::uint32_t asfGlobalFreeze                =  7;
constexpr std::uint32_t asfDefaultRipple               =  8;
constexpr std::uint32_t asfDepositAuth                 =  9;
constexpr std::uint32_t asfAuthorizedNFTokenMinter     = 10;

// OfferCreate flags:
constexpr std::uint32_t tfPassive                      = 0x00010000;
constexpr std::uint32_t tfImmediateOrCancel            = 0x00020000;
constexpr std::uint32_t tfFillOrKill                   = 0x00040000;
constexpr std::uint32_t tfSell                         = 0x00080000;
constexpr std::uint32_t tfOfferCreateMask =
    ~(tfUniversal | tfPassive | tfImmediateOrCancel | tfFillOrKill | tfSell);

// Payment flags:
constexpr std::uint32_t tfNoRippleDirect               = 0x00010000;
constexpr std::uint32_t tfPartialPayment               = 0x00020000;
constexpr std::uint32_t tfLimitQuality                 = 0x00040000;
constexpr std::uint32_t tfPaymentMask =
    ~(tfUniversal | tfPartialPayment | tfLimitQuality | tfNoRippleDirect);

// TrustSet flags:
constexpr std::uint32_t tfSetfAuth                     = 0x00010000;
constexpr std::uint32_t tfSetNoRipple                  = 0x00020000;
constexpr std::uint32_t tfClearNoRipple                = 0x00040000;
constexpr std::uint32_t tfSetFreeze                    = 0x00100000;
constexpr std::uint32_t tfClearFreeze                  = 0x00200000;
constexpr std::uint32_t tfTrustSetMask =
    ~(tfUniversal | tfSetfAuth | tfSetNoRipple | tfClearNoRipple | tfSetFreeze |
      tfClearFreeze);

// EnableAmendment flags:
constexpr std::uint32_t tfGotMajority                  = 0x00010000;
constexpr std::uint32_t tfLostMajority                 = 0x00020000;

// PaymentChannelClaim flags:
constexpr std::uint32_t tfRenew                        = 0x00010000;
constexpr std::uint32_t tfClose                        = 0x00020000;
constexpr std::uint32_t tfPayChanClaimMask = ~(tfUniversal | tfRenew | tfClose);

// NFTokenMint flags:
constexpr std::uint32_t const tfBurnable               = 0x00000001;
constexpr std::uint32_t const tfOnlyXRP                = 0x00000002;
constexpr std::uint32_t const tfTrustLine              = 0x00000004;
constexpr std::uint32_t const tfTransferable           = 0x00000008;

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
constexpr std::uint32_t const tfNFTokenMintOldMask =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTrustLine | tfTransferable);

constexpr std::uint32_t const tfNFTokenMintMask =
    ~(tfUniversal | tfBurnable | tfOnlyXRP | tfTransferable);

// NFTokenCreateOffer flags:
constexpr std::uint32_t const tfSellNFToken            = 0x00000001;
constexpr std::uint32_t const tfNFTokenCreateOfferMask =
    ~(tfUniversal | tfSellNFToken);

// NFTokenCancelOffer flags:
constexpr std::uint32_t const tfNFTokenCancelOfferMask = ~(tfUniversal);

// NFTokenAcceptOffer flags:
constexpr std::uint32_t const tfNFTokenAcceptOfferMask = ~tfUniversal;

// clang-format on

}  // namespace ripple

#endif
