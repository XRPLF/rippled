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

#include <ripple/protocol/TxFormats.h>

namespace ripple {

TxFormats::TxFormats ()
{
    // Fields shared by all txFormats:
    static const std::initializer_list<SOElement> commonFields
    {
        { sfTransactionType,      soeREQUIRED },
        { sfFlags,                soeOPTIONAL },
        { sfSourceTag,            soeOPTIONAL },
        { sfAccount,              soeREQUIRED },
        { sfSequence,             soeREQUIRED },
        { sfPreviousTxnID,        soeOPTIONAL }, // emulate027
        { sfLastLedgerSequence,   soeOPTIONAL },
        { sfAccountTxnID,         soeOPTIONAL },
        { sfFee,                  soeREQUIRED },
        { sfOperationLimit,       soeOPTIONAL },
        { sfMemos,                soeOPTIONAL },
        { sfSigningPubKey,        soeREQUIRED },
        { sfTxnSignature,         soeOPTIONAL },
        { sfSigners,              soeOPTIONAL }, // submit_multisigned
    };

    add ("AccountSet", ttACCOUNT_SET,
        {
            { sfEmailHash,           soeOPTIONAL },
            { sfWalletLocator,       soeOPTIONAL },
            { sfWalletSize,          soeOPTIONAL },
            { sfMessageKey,          soeOPTIONAL },
            { sfDomain,              soeOPTIONAL },
            { sfTransferRate,        soeOPTIONAL },
            { sfSetFlag,             soeOPTIONAL },
            { sfClearFlag,           soeOPTIONAL },
            { sfTickSize,            soeOPTIONAL },
        },
        commonFields);

    add ("TrustSet", ttTRUST_SET,
        {
            { sfLimitAmount,         soeOPTIONAL },
            { sfQualityIn,           soeOPTIONAL },
            { sfQualityOut,          soeOPTIONAL },
        },
        commonFields);

    add ("OfferCreate", ttOFFER_CREATE,
        {
            { sfTakerPays,           soeREQUIRED },
            { sfTakerGets,           soeREQUIRED },
            { sfExpiration,          soeOPTIONAL },
            { sfOfferSequence,       soeOPTIONAL },
        },
        commonFields);

    add ("OfferCancel", ttOFFER_CANCEL,
        {
            { sfOfferSequence,       soeREQUIRED },
        },
        commonFields);

    add ("SetRegularKey", ttREGULAR_KEY_SET,
        {
            { sfRegularKey,          soeOPTIONAL },
        },
        commonFields);

    add ("Payment", ttPAYMENT,
        {
            { sfDestination,         soeREQUIRED },
            { sfAmount,              soeREQUIRED },
            { sfSendMax,             soeOPTIONAL },
            { sfPaths,               soeDEFAULT  },
            { sfInvoiceID,           soeOPTIONAL },
            { sfDestinationTag,      soeOPTIONAL },
            { sfDeliverMin,          soeOPTIONAL },
        },
        commonFields);

    add ("EscrowCreate", ttESCROW_CREATE,
        {
            { sfDestination,         soeREQUIRED },
            { sfAmount,              soeREQUIRED },
            { sfCondition,           soeOPTIONAL },
            { sfCancelAfter,         soeOPTIONAL },
            { sfFinishAfter,         soeOPTIONAL },
            { sfDestinationTag,      soeOPTIONAL },
        },
        commonFields);

    add ("EscrowFinish", ttESCROW_FINISH,
        {
            { sfOwner,               soeREQUIRED },
            { sfOfferSequence,       soeREQUIRED },
            { sfFulfillment,         soeOPTIONAL },
            { sfCondition,           soeOPTIONAL },
        },
        commonFields);

    add ("EscrowCancel", ttESCROW_CANCEL,
        {
            { sfOwner,               soeREQUIRED },
            { sfOfferSequence,       soeREQUIRED },
        },
        commonFields);

    add ("EnableAmendment", ttAMENDMENT,
        {
            { sfLedgerSequence,      soeREQUIRED },
            { sfAmendment,           soeREQUIRED },
        },
        commonFields);

    add ("SetFee", ttFEE,
        {
            { sfLedgerSequence,      soeOPTIONAL },
            { sfBaseFee,             soeREQUIRED },
            { sfReferenceFeeUnits,   soeREQUIRED },
            { sfReserveBase,         soeREQUIRED },
            { sfReserveIncrement,    soeREQUIRED },
        },
        commonFields);

    add ("TicketCreate", ttTICKET_CREATE,
        {
            { sfTarget,              soeOPTIONAL },
            { sfExpiration,          soeOPTIONAL },
        },
        commonFields);

    add ("TicketCancel", ttTICKET_CANCEL,
        {
            { sfTicketID,            soeREQUIRED },
        },
        commonFields);

    // The SignerEntries are optional because a SignerList is deleted by
    // setting the SignerQuorum to zero and omitting SignerEntries.
    add ("SignerListSet", ttSIGNER_LIST_SET,
        {
            { sfSignerQuorum,        soeREQUIRED },
            { sfSignerEntries,       soeOPTIONAL },
        },
        commonFields);

    add ("PaymentChannelCreate", ttPAYCHAN_CREATE,
        {
            { sfDestination,         soeREQUIRED },
            { sfAmount,              soeREQUIRED },
            { sfSettleDelay,         soeREQUIRED },
            { sfPublicKey,           soeREQUIRED },
            { sfCancelAfter,         soeOPTIONAL },
            { sfDestinationTag,      soeOPTIONAL },
        },
        commonFields);

    add ("PaymentChannelFund", ttPAYCHAN_FUND,
        {
            { sfPayChannel,          soeREQUIRED },
            { sfAmount,              soeREQUIRED },
            { sfExpiration,          soeOPTIONAL },
        },
        commonFields);

    add ("PaymentChannelClaim", ttPAYCHAN_CLAIM,
        {
            { sfPayChannel,          soeREQUIRED },
            { sfAmount,              soeOPTIONAL },
            { sfBalance,             soeOPTIONAL },
            { sfSignature,           soeOPTIONAL },
            { sfPublicKey,           soeOPTIONAL },
        },
        commonFields);

    add ("CheckCreate", ttCHECK_CREATE,
        {
            { sfDestination,         soeREQUIRED },
            { sfSendMax,             soeREQUIRED },
            { sfExpiration,          soeOPTIONAL },
            { sfDestinationTag,      soeOPTIONAL },
            { sfInvoiceID,           soeOPTIONAL },
        },
        commonFields);

    add ("CheckCash", ttCHECK_CASH,
        {
            { sfCheckID,             soeREQUIRED },
            { sfAmount,              soeOPTIONAL },
            { sfDeliverMin,          soeOPTIONAL },
        },
        commonFields);

    add ("CheckCancel", ttCHECK_CANCEL,
        {
            { sfCheckID,             soeREQUIRED },
        },
        commonFields);

    add ("DepositPreauth", ttDEPOSIT_PREAUTH,
        {
            { sfAuthorize,           soeOPTIONAL },
            { sfUnauthorize,         soeOPTIONAL },
        },
        commonFields);
}

TxFormats const&
TxFormats::getInstance ()
{
    static TxFormats const instance;
    return instance;
}

} // ripple
