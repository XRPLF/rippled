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

#include <ripple/protocol/SField.h>
#include <cassert>
#include <string>
#include <utility>

namespace ripple {

// Storage for static const members.
SField::IsSigning const SField::notSigning;
int SField::num = 0;
std::map<int, SField const*> SField::knownCodeToField;

// Give only this translation unit permission to construct SFields
struct SField::private_access_tag_t
{
    explicit private_access_tag_t() = default;
};

static SField::private_access_tag_t access;

// Construct all compile-time SFields, and register them in the knownCodeToField
// database:

SField const sfInvalid(access, -1);
SField const sfGeneric(access, 0);
SField const sfLedgerEntry(access, STI_LEDGERENTRY, 257, "LedgerEntry");
SField const sfTransaction(access, STI_TRANSACTION, 257, "Transaction");
SField const sfValidation(access, STI_VALIDATION, 257, "Validation");
SField const sfMetadata(access, STI_METADATA, 257, "Metadata");
SField const sfHash(access, STI_HASH256, 257, "hash");
SField const sfIndex(access, STI_HASH256, 258, "index");

// 8-bit integers
SF_U8 const sfCloseResolution(access, STI_UINT8, 1, "CloseResolution");
SF_U8 const sfMethod(access, STI_UINT8, 2, "Method");
SF_U8 const sfTransactionResult(access, STI_UINT8, 3, "TransactionResult");

// 8-bit integers (uncommon)
SF_U8 const sfTickSize(access, STI_UINT8, 16, "TickSize");
SF_U8 const sfUNLModifyDisabling(access, STI_UINT8, 17, "UNLModifyDisabling");

// 16-bit integers
SF_U16 const sfLedgerEntryType(
    access,
    STI_UINT16,
    1,
    "LedgerEntryType",
    SField::sMD_Never);
SF_U16 const sfTransactionType(access, STI_UINT16, 2, "TransactionType");
SF_U16 const sfSignerWeight(access, STI_UINT16, 3, "SignerWeight");

// 16-bit integers (uncommon)
SF_U16 const sfVersion(access, STI_UINT16, 16, "Version");

// 32-bit integers (common)
SF_U32 const sfFlags(access, STI_UINT32, 2, "Flags");
SF_U32 const sfSourceTag(access, STI_UINT32, 3, "SourceTag");
SF_U32 const sfSequence(access, STI_UINT32, 4, "Sequence");
SF_U32 const sfPreviousTxnLgrSeq(
    access,
    STI_UINT32,
    5,
    "PreviousTxnLgrSeq",
    SField::sMD_DeleteFinal);
SF_U32 const sfLedgerSequence(access, STI_UINT32, 6, "LedgerSequence");
SF_U32 const sfCloseTime(access, STI_UINT32, 7, "CloseTime");
SF_U32 const sfParentCloseTime(access, STI_UINT32, 8, "ParentCloseTime");
SF_U32 const sfSigningTime(access, STI_UINT32, 9, "SigningTime");
SF_U32 const sfExpiration(access, STI_UINT32, 10, "Expiration");
SF_U32 const sfTransferRate(access, STI_UINT32, 11, "TransferRate");
SF_U32 const sfWalletSize(access, STI_UINT32, 12, "WalletSize");
SF_U32 const sfOwnerCount(access, STI_UINT32, 13, "OwnerCount");
SF_U32 const sfDestinationTag(access, STI_UINT32, 14, "DestinationTag");

// 32-bit integers (uncommon)
SF_U32 const sfHighQualityIn(access, STI_UINT32, 16, "HighQualityIn");
SF_U32 const sfHighQualityOut(access, STI_UINT32, 17, "HighQualityOut");
SF_U32 const sfLowQualityIn(access, STI_UINT32, 18, "LowQualityIn");
SF_U32 const sfLowQualityOut(access, STI_UINT32, 19, "LowQualityOut");
SF_U32 const sfQualityIn(access, STI_UINT32, 20, "QualityIn");
SF_U32 const sfQualityOut(access, STI_UINT32, 21, "QualityOut");
SF_U32 const sfStampEscrow(access, STI_UINT32, 22, "StampEscrow");
SF_U32 const sfBondAmount(access, STI_UINT32, 23, "BondAmount");
SF_U32 const sfLoadFee(access, STI_UINT32, 24, "LoadFee");
SF_U32 const sfOfferSequence(access, STI_UINT32, 25, "OfferSequence");
SF_U32 const
    sfFirstLedgerSequence(access, STI_UINT32, 26, "FirstLedgerSequence");
SF_U32 const sfLastLedgerSequence(access, STI_UINT32, 27, "LastLedgerSequence");
SF_U32 const sfTransactionIndex(access, STI_UINT32, 28, "TransactionIndex");
SF_U32 const sfOperationLimit(access, STI_UINT32, 29, "OperationLimit");
SF_U32 const sfReferenceFeeUnits(access, STI_UINT32, 30, "ReferenceFeeUnits");
SF_U32 const sfReserveBase(access, STI_UINT32, 31, "ReserveBase");
SF_U32 const sfReserveIncrement(access, STI_UINT32, 32, "ReserveIncrement");
SF_U32 const sfSetFlag(access, STI_UINT32, 33, "SetFlag");
SF_U32 const sfClearFlag(access, STI_UINT32, 34, "ClearFlag");
SF_U32 const sfSignerQuorum(access, STI_UINT32, 35, "SignerQuorum");
SF_U32 const sfCancelAfter(access, STI_UINT32, 36, "CancelAfter");
SF_U32 const sfFinishAfter(access, STI_UINT32, 37, "FinishAfter");
SF_U32 const sfSignerListID(access, STI_UINT32, 38, "SignerListID");
SF_U32 const sfSettleDelay(access, STI_UINT32, 39, "SettleDelay");

// 64-bit integers
SF_U64 const sfIndexNext(access, STI_UINT64, 1, "IndexNext");
SF_U64 const sfIndexPrevious(access, STI_UINT64, 2, "IndexPrevious");
SF_U64 const sfBookNode(access, STI_UINT64, 3, "BookNode");
SF_U64 const sfOwnerNode(access, STI_UINT64, 4, "OwnerNode");
SF_U64 const sfBaseFee(access, STI_UINT64, 5, "BaseFee");
SF_U64 const sfExchangeRate(access, STI_UINT64, 6, "ExchangeRate");
SF_U64 const sfLowNode(access, STI_UINT64, 7, "LowNode");
SF_U64 const sfHighNode(access, STI_UINT64, 8, "HighNode");
SF_U64 const sfDestinationNode(access, STI_UINT64, 9, "DestinationNode");
SF_U64 const sfCookie(access, STI_UINT64, 10, "Cookie");
SF_U64 const sfServerVersion(access, STI_UINT64, 11, "ServerVersion");

// 128-bit
SF_U128 const sfEmailHash(access, STI_HASH128, 1, "EmailHash");

// 160-bit (common)
SF_U160 const sfTakerPaysCurrency(access, STI_HASH160, 1, "TakerPaysCurrency");
SF_U160 const sfTakerPaysIssuer(access, STI_HASH160, 2, "TakerPaysIssuer");
SF_U160 const sfTakerGetsCurrency(access, STI_HASH160, 3, "TakerGetsCurrency");
SF_U160 const sfTakerGetsIssuer(access, STI_HASH160, 4, "TakerGetsIssuer");

// 256-bit (common)
SF_U256 const sfLedgerHash(access, STI_HASH256, 1, "LedgerHash");
SF_U256 const sfParentHash(access, STI_HASH256, 2, "ParentHash");
SF_U256 const sfTransactionHash(access, STI_HASH256, 3, "TransactionHash");
SF_U256 const sfAccountHash(access, STI_HASH256, 4, "AccountHash");
SF_U256 const sfPreviousTxnID(
    access,
    STI_HASH256,
    5,
    "PreviousTxnID",
    SField::sMD_DeleteFinal);
SF_U256 const sfLedgerIndex(access, STI_HASH256, 6, "LedgerIndex");
SF_U256 const sfWalletLocator(access, STI_HASH256, 7, "WalletLocator");
SF_U256 const
    sfRootIndex(access, STI_HASH256, 8, "RootIndex", SField::sMD_Always);
SF_U256 const sfAccountTxnID(access, STI_HASH256, 9, "AccountTxnID");

// 256-bit (uncommon)
SF_U256 const sfBookDirectory(access, STI_HASH256, 16, "BookDirectory");
SF_U256 const sfInvoiceID(access, STI_HASH256, 17, "InvoiceID");
SF_U256 const sfNickname(access, STI_HASH256, 18, "Nickname");
SF_U256 const sfAmendment(access, STI_HASH256, 19, "Amendment");
SF_U256 const sfTicketID(access, STI_HASH256, 20, "TicketID");
SF_U256 const sfDigest(access, STI_HASH256, 21, "Digest");
SF_U256 const sfPayChannel(access, STI_HASH256, 22, "Channel");
SF_U256 const sfConsensusHash(access, STI_HASH256, 23, "ConsensusHash");
SF_U256 const sfCheckID(access, STI_HASH256, 24, "CheckID");
SF_U256 const sfValidatedHash(access, STI_HASH256, 25, "ValidatedHash");

// currency amount (common)
SF_Amount const sfAmount(access, STI_AMOUNT, 1, "Amount");
SF_Amount const sfBalance(access, STI_AMOUNT, 2, "Balance");
SF_Amount const sfLimitAmount(access, STI_AMOUNT, 3, "LimitAmount");
SF_Amount const sfTakerPays(access, STI_AMOUNT, 4, "TakerPays");
SF_Amount const sfTakerGets(access, STI_AMOUNT, 5, "TakerGets");
SF_Amount const sfLowLimit(access, STI_AMOUNT, 6, "LowLimit");
SF_Amount const sfHighLimit(access, STI_AMOUNT, 7, "HighLimit");
SF_Amount const sfFee(access, STI_AMOUNT, 8, "Fee");
SF_Amount const sfSendMax(access, STI_AMOUNT, 9, "SendMax");
SF_Amount const sfDeliverMin(access, STI_AMOUNT, 10, "DeliverMin");

// currency amount (uncommon)
SF_Amount const sfMinimumOffer(access, STI_AMOUNT, 16, "MinimumOffer");
SF_Amount const sfRippleEscrow(access, STI_AMOUNT, 17, "RippleEscrow");
SF_Amount const sfDeliveredAmount(access, STI_AMOUNT, 18, "DeliveredAmount");

// variable length (common)
SF_Blob const sfPublicKey(access, STI_VL, 1, "PublicKey");
SF_Blob const sfMessageKey(access, STI_VL, 2, "MessageKey");
SF_Blob const sfSigningPubKey(access, STI_VL, 3, "SigningPubKey");
SF_Blob const sfTxnSignature(
    access,
    STI_VL,
    4,
    "TxnSignature",
    SField::sMD_Default,
    SField::notSigning);
SF_Blob const sfSignature(
    access,
    STI_VL,
    6,
    "Signature",
    SField::sMD_Default,
    SField::notSigning);
SF_Blob const sfDomain(access, STI_VL, 7, "Domain");
SF_Blob const sfFundCode(access, STI_VL, 8, "FundCode");
SF_Blob const sfRemoveCode(access, STI_VL, 9, "RemoveCode");
SF_Blob const sfExpireCode(access, STI_VL, 10, "ExpireCode");
SF_Blob const sfCreateCode(access, STI_VL, 11, "CreateCode");
SF_Blob const sfMemoType(access, STI_VL, 12, "MemoType");
SF_Blob const sfMemoData(access, STI_VL, 13, "MemoData");
SF_Blob const sfMemoFormat(access, STI_VL, 14, "MemoFormat");

// variable length (uncommon)
SF_Blob const sfFulfillment(access, STI_VL, 16, "Fulfillment");
SF_Blob const sfCondition(access, STI_VL, 17, "Condition");
SF_Blob const sfMasterSignature(
    access,
    STI_VL,
    18,
    "MasterSignature",
    SField::sMD_Default,
    SField::notSigning);
SF_Blob const sfUNLModifyValidator(access, STI_VL, 19, "UNLModifyValidator");
SF_Blob const sfValidatorToDisable(access, STI_VL, 20, "ValidatorToDisable");
SF_Blob const sfValidatorToReEnable(access, STI_VL, 21, "ValidatorToReEnable");

// account
SF_Account const sfAccount(access, STI_ACCOUNT, 1, "Account");
SF_Account const sfOwner(access, STI_ACCOUNT, 2, "Owner");
SF_Account const sfDestination(access, STI_ACCOUNT, 3, "Destination");
SF_Account const sfIssuer(access, STI_ACCOUNT, 4, "Issuer");
SF_Account const sfAuthorize(access, STI_ACCOUNT, 5, "Authorize");
SF_Account const sfUnauthorize(access, STI_ACCOUNT, 6, "Unauthorize");
SF_Account const sfTarget(access, STI_ACCOUNT, 7, "Target");
SF_Account const sfRegularKey(access, STI_ACCOUNT, 8, "RegularKey");

// path set
SField const sfPaths(access, STI_PATHSET, 1, "Paths");

// vector of 256-bit
SF_Vec256 const
    sfIndexes(access, STI_VECTOR256, 1, "Indexes", SField::sMD_Never);
SF_Vec256 const sfHashes(access, STI_VECTOR256, 2, "Hashes");
SF_Vec256 const sfAmendments(access, STI_VECTOR256, 3, "Amendments");

// inner object
// OBJECT/1 is reserved for end of object
SField const
    sfTransactionMetaData(access, STI_OBJECT, 2, "TransactionMetaData");
SField const sfCreatedNode(access, STI_OBJECT, 3, "CreatedNode");
SField const sfDeletedNode(access, STI_OBJECT, 4, "DeletedNode");
SField const sfModifiedNode(access, STI_OBJECT, 5, "ModifiedNode");
SField const sfPreviousFields(access, STI_OBJECT, 6, "PreviousFields");
SField const sfFinalFields(access, STI_OBJECT, 7, "FinalFields");
SField const sfNewFields(access, STI_OBJECT, 8, "NewFields");
SField const sfTemplateEntry(access, STI_OBJECT, 9, "TemplateEntry");
SField const sfMemo(access, STI_OBJECT, 10, "Memo");
SField const sfSignerEntry(access, STI_OBJECT, 11, "SignerEntry");

// inner object (uncommon)
SField const sfSigner(access, STI_OBJECT, 16, "Signer");
//                                                                                 17 has not been used yet...
SField const sfMajority(access, STI_OBJECT, 18, "Majority");
SField const sfDisabledValidator(access, STI_OBJECT, 19, "DisabledValidator");

// array of objects
// ARRAY/1 is reserved for end of array
// SField const sfSigningAccounts (access, STI_ARRAY, 2, "SigningAccounts"); //
// Never been used.
SField const sfSigners(
    access,
    STI_ARRAY,
    3,
    "Signers",
    SField::sMD_Default,
    SField::notSigning);
SField const sfSignerEntries(access, STI_ARRAY, 4, "SignerEntries");
SField const sfTemplate(access, STI_ARRAY, 5, "Template");
SField const sfNecessary(access, STI_ARRAY, 6, "Necessary");
SField const sfSufficient(access, STI_ARRAY, 7, "Sufficient");
SField const sfAffectedNodes(access, STI_ARRAY, 8, "AffectedNodes");
SField const sfMemos(access, STI_ARRAY, 9, "Memos");

// array of objects (uncommon)
SField const sfMajorities(access, STI_ARRAY, 16, "Majorities");
SField const sfDisabledValidators(access, STI_ARRAY, 17, "DisabledValidators");

SField::SField(
    private_access_tag_t,
    SerializedTypeID tid,
    int fv,
    const char* fn,
    int meta,
    IsSigning signing)
    : fieldCode(field_code(tid, fv))
    , fieldType(tid)
    , fieldValue(fv)
    , fieldName(fn)
    , fieldMeta(meta)
    , fieldNum(++num)
    , signingField(signing)
    , jsonName(fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

SField::SField(private_access_tag_t, int fc)
    : fieldCode(fc)
    , fieldType(STI_UNKNOWN)
    , fieldValue(0)
    , fieldMeta(sMD_Never)
    , fieldNum(++num)
    , signingField(IsSigning::yes)
    , jsonName(fieldName.c_str())
{
    knownCodeToField[fieldCode] = this;
}

SField const&
SField::getField(int code)
{
    auto it = knownCodeToField.find(code);

    if (it != knownCodeToField.end())
    {
        return *(it->second);
    }
    return sfInvalid;
}

int
SField::compare(SField const& f1, SField const& f2)
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
SField::getField(std::string const& fieldName)
{
    for (auto const& [_, f] : knownCodeToField)
    {
        (void)_;
        if (f->fieldName == fieldName)
            return *f;
    }
    return sfInvalid;
}

}  // namespace ripple
