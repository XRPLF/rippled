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

#include <ripple/basics/safe_cast.h>
#include <ripple/protocol/SField.h>
#include <cassert>
#include <string>
#include <utility>

namespace ripple {

// Storage for static const members.
SField::IsSigning const SField::notSigning;
int SField::num = 0;
std::mutex SField::SField_mutex;
std::map<int, SField const*> SField::knownCodeToField;
std::map<int, std::unique_ptr<SField const>> SField::unknownCodeToField;

using StaticScopedLockType = std::lock_guard <std::mutex>;

// Give only this translation unit permission to construct SFields
struct SField::private_access_tag_t
{
};

SField::private_access_tag_t dummy;

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:

SField const sfInvalid      (dummy, -1);
SField const sfGeneric      (dummy, 0);
SField const sfLedgerEntry  (dummy, STI_LEDGERENTRY, 257, "LedgerEntry");
SField const sfTransaction  (dummy, STI_TRANSACTION, 257, "Transaction");
SField const sfValidation   (dummy, STI_VALIDATION,  257, "Validation");
SField const sfMetadata     (dummy, STI_METADATA,    257, "Metadata");
SField const sfHash         (dummy, STI_HASH256,     257, "hash");
SField const sfIndex        (dummy, STI_HASH256,     258, "index");

// 8-bit integers
SF_U8 const sfCloseResolution   (dummy, STI_UINT8, 1, "CloseResolution");
SF_U8 const sfMethod            (dummy, STI_UINT8, 2, "Method");
SF_U8 const sfTransactionResult (dummy, STI_UINT8, 3, "TransactionResult");

// 8-bit integers (uncommon)
SF_U8 const sfTickSize          (dummy, STI_UINT8, 16, "TickSize");

// 16-bit integers
SF_U16 const sfLedgerEntryType (dummy, STI_UINT16, 1, "LedgerEntryType", SField::sMD_Never);
SF_U16 const sfTransactionType (dummy, STI_UINT16, 2, "TransactionType");
SF_U16 const sfSignerWeight    (dummy, STI_UINT16, 3, "SignerWeight");

// 32-bit integers (common)
SF_U32 const sfFlags             (dummy, STI_UINT32,  2, "Flags");
SF_U32 const sfSourceTag         (dummy, STI_UINT32,  3, "SourceTag");
SF_U32 const sfSequence          (dummy, STI_UINT32,  4, "Sequence");
SF_U32 const sfPreviousTxnLgrSeq (dummy, STI_UINT32,  5, "PreviousTxnLgrSeq", SField::sMD_DeleteFinal);
SF_U32 const sfLedgerSequence    (dummy, STI_UINT32,  6, "LedgerSequence");
SF_U32 const sfCloseTime         (dummy, STI_UINT32,  7, "CloseTime");
SF_U32 const sfParentCloseTime   (dummy, STI_UINT32,  8, "ParentCloseTime");
SF_U32 const sfSigningTime       (dummy, STI_UINT32,  9, "SigningTime");
SF_U32 const sfExpiration        (dummy, STI_UINT32, 10, "Expiration");
SF_U32 const sfTransferRate      (dummy, STI_UINT32, 11, "TransferRate");
SF_U32 const sfWalletSize        (dummy, STI_UINT32, 12, "WalletSize");
SF_U32 const sfOwnerCount        (dummy, STI_UINT32, 13, "OwnerCount");
SF_U32 const sfDestinationTag    (dummy, STI_UINT32, 14, "DestinationTag");

// 32-bit integers (uncommon)
SF_U32 const sfHighQualityIn       (dummy, STI_UINT32, 16, "HighQualityIn");
SF_U32 const sfHighQualityOut      (dummy, STI_UINT32, 17, "HighQualityOut");
SF_U32 const sfLowQualityIn        (dummy, STI_UINT32, 18, "LowQualityIn");
SF_U32 const sfLowQualityOut       (dummy, STI_UINT32, 19, "LowQualityOut");
SF_U32 const sfQualityIn           (dummy, STI_UINT32, 20, "QualityIn");
SF_U32 const sfQualityOut          (dummy, STI_UINT32, 21, "QualityOut");
SF_U32 const sfStampEscrow         (dummy, STI_UINT32, 22, "StampEscrow");
SF_U32 const sfBondAmount          (dummy, STI_UINT32, 23, "BondAmount");
SF_U32 const sfLoadFee             (dummy, STI_UINT32, 24, "LoadFee");
SF_U32 const sfOfferSequence       (dummy, STI_UINT32, 25, "OfferSequence");
SF_U32 const sfFirstLedgerSequence (dummy, STI_UINT32, 26, "FirstLedgerSequence");  // Deprecated: do not use
SF_U32 const sfLastLedgerSequence  (dummy, STI_UINT32, 27, "LastLedgerSequence");
SF_U32 const sfTransactionIndex    (dummy, STI_UINT32, 28, "TransactionIndex");
SF_U32 const sfOperationLimit      (dummy, STI_UINT32, 29, "OperationLimit");
SF_U32 const sfReferenceFeeUnits   (dummy, STI_UINT32, 30, "ReferenceFeeUnits");
SF_U32 const sfReserveBase         (dummy, STI_UINT32, 31, "ReserveBase");
SF_U32 const sfReserveIncrement    (dummy, STI_UINT32, 32, "ReserveIncrement");
SF_U32 const sfSetFlag             (dummy, STI_UINT32, 33, "SetFlag");
SF_U32 const sfClearFlag           (dummy, STI_UINT32, 34, "ClearFlag");
SF_U32 const sfSignerQuorum        (dummy, STI_UINT32, 35, "SignerQuorum");
SF_U32 const sfCancelAfter         (dummy, STI_UINT32, 36, "CancelAfter");
SF_U32 const sfFinishAfter         (dummy, STI_UINT32, 37, "FinishAfter");
SF_U32 const sfSignerListID        (dummy, STI_UINT32, 38, "SignerListID");
SF_U32 const sfSettleDelay         (dummy, STI_UINT32, 39, "SettleDelay");

// 64-bit integers
SF_U64 const sfIndexNext        (dummy, STI_UINT64, 1, "IndexNext");
SF_U64 const sfIndexPrevious    (dummy, STI_UINT64, 2, "IndexPrevious");
SF_U64 const sfBookNode         (dummy, STI_UINT64, 3, "BookNode");
SF_U64 const sfOwnerNode        (dummy, STI_UINT64, 4, "OwnerNode");
SF_U64 const sfBaseFee          (dummy, STI_UINT64, 5, "BaseFee");
SF_U64 const sfExchangeRate     (dummy, STI_UINT64, 6, "ExchangeRate");
SF_U64 const sfLowNode          (dummy, STI_UINT64, 7, "LowNode");
SF_U64 const sfHighNode         (dummy, STI_UINT64, 8, "HighNode");
SF_U64 const sfDestinationNode  (dummy, STI_UINT64, 9, "DestinationNode");
SF_U64 const sfCookie           (dummy, STI_UINT64, 10,"Cookie");


// 128-bit
SF_U128 const sfEmailHash (dummy, STI_HASH128, 1, "EmailHash");

// 160-bit (common)
SF_U160 const sfTakerPaysCurrency (dummy, STI_HASH160, 1, "TakerPaysCurrency");
SF_U160 const sfTakerPaysIssuer   (dummy, STI_HASH160, 2, "TakerPaysIssuer");
SF_U160 const sfTakerGetsCurrency (dummy, STI_HASH160, 3, "TakerGetsCurrency");
SF_U160 const sfTakerGetsIssuer   (dummy, STI_HASH160, 4, "TakerGetsIssuer");

// 256-bit (common)
SF_U256 const sfLedgerHash      (dummy, STI_HASH256, 1, "LedgerHash");
SF_U256 const sfParentHash      (dummy, STI_HASH256, 2, "ParentHash");
SF_U256 const sfTransactionHash (dummy, STI_HASH256, 3, "TransactionHash");
SF_U256 const sfAccountHash     (dummy, STI_HASH256, 4, "AccountHash");
SF_U256 const sfPreviousTxnID   (dummy, STI_HASH256, 5, "PreviousTxnID", SField::sMD_DeleteFinal);
SF_U256 const sfLedgerIndex     (dummy, STI_HASH256, 6, "LedgerIndex");
SF_U256 const sfWalletLocator   (dummy, STI_HASH256, 7, "WalletLocator");
SF_U256 const sfRootIndex       (dummy, STI_HASH256, 8, "RootIndex", SField::sMD_Always);
SF_U256 const sfAccountTxnID    (dummy, STI_HASH256, 9, "AccountTxnID");

// 256-bit (uncommon)
SF_U256 const sfBookDirectory (dummy, STI_HASH256, 16, "BookDirectory");
SF_U256 const sfInvoiceID     (dummy, STI_HASH256, 17, "InvoiceID");
SF_U256 const sfNickname      (dummy, STI_HASH256, 18, "Nickname");
SF_U256 const sfAmendment     (dummy, STI_HASH256, 19, "Amendment");
SF_U256 const sfTicketID      (dummy, STI_HASH256, 20, "TicketID");
SF_U256 const sfDigest        (dummy, STI_HASH256, 21, "Digest");
SF_U256 const sfPayChannel    (dummy, STI_HASH256, 22, "Channel");
SF_U256 const sfConsensusHash (dummy, STI_HASH256, 23, "ConsensusHash");
SF_U256 const sfCheckID       (dummy, STI_HASH256, 24, "CheckID");

// currency amount (common)
SF_Amount const sfAmount      (dummy, STI_AMOUNT,  1, "Amount");
SF_Amount const sfBalance     (dummy, STI_AMOUNT,  2, "Balance");
SF_Amount const sfLimitAmount (dummy, STI_AMOUNT,  3, "LimitAmount");
SF_Amount const sfTakerPays   (dummy, STI_AMOUNT,  4, "TakerPays");
SF_Amount const sfTakerGets   (dummy, STI_AMOUNT,  5, "TakerGets");
SF_Amount const sfLowLimit    (dummy, STI_AMOUNT,  6, "LowLimit");
SF_Amount const sfHighLimit   (dummy, STI_AMOUNT,  7, "HighLimit");
SF_Amount const sfFee         (dummy, STI_AMOUNT,  8, "Fee");
SF_Amount const sfSendMax     (dummy, STI_AMOUNT,  9, "SendMax");
SF_Amount const sfDeliverMin  (dummy, STI_AMOUNT, 10, "DeliverMin");

// currency amount (uncommon)
SF_Amount const sfMinimumOffer    (dummy, STI_AMOUNT, 16, "MinimumOffer");
SF_Amount const sfRippleEscrow    (dummy, STI_AMOUNT, 17, "RippleEscrow");
SF_Amount const sfDeliveredAmount (dummy, STI_AMOUNT, 18, "DeliveredAmount");

// variable length (common)
SF_Blob const sfPublicKey       (dummy, STI_VL,  1, "PublicKey");
SF_Blob const sfSigningPubKey   (dummy, STI_VL,  3, "SigningPubKey");
SF_Blob const sfSignature       (dummy, STI_VL,  6, "Signature", SField::sMD_Default, SField::notSigning);
SF_Blob const sfMessageKey      (dummy, STI_VL,  2, "MessageKey");
SF_Blob const sfTxnSignature    (dummy, STI_VL,  4, "TxnSignature", SField::sMD_Default, SField::notSigning);
SF_Blob const sfDomain          (dummy, STI_VL,  7, "Domain");
SF_Blob const sfFundCode        (dummy, STI_VL,  8, "FundCode");
SF_Blob const sfRemoveCode      (dummy, STI_VL,  9, "RemoveCode");
SF_Blob const sfExpireCode      (dummy, STI_VL, 10, "ExpireCode");
SF_Blob const sfCreateCode      (dummy, STI_VL, 11, "CreateCode");
SF_Blob const sfMemoType        (dummy, STI_VL, 12, "MemoType");
SF_Blob const sfMemoData        (dummy, STI_VL, 13, "MemoData");
SF_Blob const sfMemoFormat      (dummy, STI_VL, 14, "MemoFormat");


// variable length (uncommon)
SF_Blob const sfFulfillment     (dummy, STI_VL, 16, "Fulfillment");
SF_Blob const sfCondition       (dummy, STI_VL, 17, "Condition");
SF_Blob const sfMasterSignature (dummy, STI_VL, 18, "MasterSignature", SField::sMD_Default, SField::notSigning);


// account
SF_Account const sfAccount     (dummy, STI_ACCOUNT, 1, "Account");
SF_Account const sfOwner       (dummy, STI_ACCOUNT, 2, "Owner");
SF_Account const sfDestination (dummy, STI_ACCOUNT, 3, "Destination");
SF_Account const sfIssuer      (dummy, STI_ACCOUNT, 4, "Issuer");
SF_Account const sfAuthorize   (dummy, STI_ACCOUNT, 5, "Authorize");
SF_Account const sfUnauthorize (dummy, STI_ACCOUNT, 6, "Unauthorize");
SF_Account const sfTarget      (dummy, STI_ACCOUNT, 7, "Target");
SF_Account const sfRegularKey  (dummy, STI_ACCOUNT, 8, "RegularKey");

// path set
SField const sfPaths (dummy, STI_PATHSET, 1, "Paths");

// vector of 256-bit
SF_Vec256 const sfIndexes    (dummy, STI_VECTOR256, 1, "Indexes", SField::sMD_Never);
SF_Vec256 const sfHashes     (dummy, STI_VECTOR256, 2, "Hashes");
SF_Vec256 const sfAmendments (dummy, STI_VECTOR256, 3, "Amendments");

// inner object
// OBJECT/1 is reserved for end of object
SField const sfTransactionMetaData (dummy, STI_OBJECT,  2, "TransactionMetaData");
SField const sfCreatedNode         (dummy, STI_OBJECT,  3, "CreatedNode");
SField const sfDeletedNode         (dummy, STI_OBJECT,  4, "DeletedNode");
SField const sfModifiedNode        (dummy, STI_OBJECT,  5, "ModifiedNode");
SField const sfPreviousFields      (dummy, STI_OBJECT,  6, "PreviousFields");
SField const sfFinalFields         (dummy, STI_OBJECT,  7, "FinalFields");
SField const sfNewFields           (dummy, STI_OBJECT,  8, "NewFields");
SField const sfTemplateEntry       (dummy, STI_OBJECT,  9, "TemplateEntry");
SField const sfMemo                (dummy, STI_OBJECT, 10, "Memo");
SField const sfSignerEntry         (dummy, STI_OBJECT, 11, "SignerEntry");

// inner object (uncommon)
SField const sfSigner              (dummy, STI_OBJECT, 16, "Signer");
//                                                                                 17 has not been used yet...
SField const sfMajority            (dummy, STI_OBJECT, 18, "Majority");

// array of objects
// ARRAY/1 is reserved for end of array
// SField const sfSigningAccounts (dummy, STI_ARRAY, 2, "SigningAccounts"); // Never been used.
SField const sfSigners         (dummy, STI_ARRAY, 3, "Signers", SField::sMD_Default, SField::notSigning);
SField const sfSignerEntries   (dummy, STI_ARRAY, 4, "SignerEntries");
SField const sfTemplate        (dummy, STI_ARRAY, 5, "Template");
SField const sfNecessary       (dummy, STI_ARRAY, 6, "Necessary");
SField const sfSufficient      (dummy, STI_ARRAY, 7, "Sufficient");
SField const sfAffectedNodes   (dummy, STI_ARRAY, 8, "AffectedNodes");
SField const sfMemos           (dummy, STI_ARRAY, 9, "Memos");

// array of objects (uncommon)
SField const sfMajorities      (dummy, STI_ARRAY, 16, "Majorities");

SField::SField(private_access_tag_t,
    SerializedTypeID tid, int fv, const char* fn, int meta,
    IsSigning signing)
    : fieldCode (field_code (tid, fv))
    , fieldType (tid)
    , fieldValue (fv)
    , fieldName (fn)
    , fieldMeta (meta)
    , fieldNum (++num)
    , signingField (signing)
    , jsonName (fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

SField::SField(private_access_tag_t, int fc)
    : fieldCode (fc)
    , fieldType (STI_UNKNOWN)
    , fieldValue (0)
    , fieldMeta (sMD_Never)
    , fieldNum (++num)
    , signingField (IsSigning::yes)
    , jsonName (fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

// call with the map mutex to protect num.
// This is naturally done with no extra expense
// from getField(int code).
SField::SField(SerializedTypeID tid, int fv)
    : fieldCode (field_code (tid, fv))
    , fieldType (tid)
    , fieldValue (fv)
    , fieldName (std::to_string (tid) + '/' + std::to_string (fv))
    , fieldMeta (sMD_Default)
    , fieldNum (++num)
    , signingField (IsSigning::yes)
    , jsonName (fieldName.c_str())
{
    assert ((fv != 1) || ((tid != STI_ARRAY) && (tid != STI_OBJECT)));
}

SField::SField(private_access_tag_t,
    SerializedTypeID tid, int fv)
    : SField(tid, fv)
{
    knownCodeToField[fieldCode] = this;
}

SField const&
SField::getField (int code)
{
    auto it = knownCodeToField.find (code);

    if (it != knownCodeToField.end ())
    {
        // 99+% of the time, it will be a valid, known field
        return * (it->second);
    }

    int type = code >> 16;
    int field = code & 0xffff;

    // Don't dynamically extend types that have no binary encoding.
    if ((field > 255) || (code < 0))
        return sfInvalid;

    switch (type)
    {
    // Types we are willing to dynamically extend
    // types (common)
    case STI_UINT16:
    case STI_UINT32:
    case STI_UINT64:
    case STI_HASH128:
    case STI_HASH256:
    case STI_AMOUNT:
    case STI_VL:
    case STI_ACCOUNT:
    case STI_OBJECT:
    case STI_ARRAY:
    // types (uncommon)
    case STI_UINT8:
    case STI_HASH160:
    case STI_PATHSET:
    case STI_VECTOR256:
        break;

    default:
        return sfInvalid;
    }

    {
        // Lookup in the run-time data base, and create if it does not
        // yet exist.
        StaticScopedLockType sl (SField_mutex);

        auto it = unknownCodeToField.find (code);

        if (it != unknownCodeToField.end ())
            return * (it->second);
        return *(unknownCodeToField[code] = std::unique_ptr<SField const>(
                       new SField(safe_cast<SerializedTypeID>(type), field)));
    }
}

int SField::compare (SField const& f1, SField const& f2)
{
    // -1 = f1 comes before f2, 0 = illegal combination, 1 = f1 comes after f2
    if ((f1.fieldCode <= 0) || (f2.fieldCode <= 0))
        return 0;

    if (f1.fieldCode < f2.fieldCode)
        return -1;

    if (f2.fieldCode < f1.fieldCode)
        return 1;

    return 0;
}

SField const&
SField::getField (std::string const& fieldName)
{
    for (auto const & fieldPair : knownCodeToField)
    {
        if (fieldPair.second->fieldName == fieldName)
            return * (fieldPair.second);
    }
    {
        StaticScopedLockType sl (SField_mutex);

        for (auto const & fieldPair : unknownCodeToField)
        {
            if (fieldPair.second->fieldName == fieldName)
                return * (fieldPair.second);
        }
    }
    return sfInvalid;
}

} // ripple
