//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/TxFormats.h>

#include "org/xrpl/rpc/v1/ledger_objects.pb.h"
#include "org/xrpl/rpc/v1/transaction.pb.h"

#include <cctype>
#include <map>
#include <string>
#include <type_traits>

namespace ripple {

// This test suite uses the google::protobuf::Descriptor class to do runtime
// reflection on our gRPC stuff.  At the time of this writing documentation
// for Descriptor could be found here:
//
// https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.descriptor#Descriptor

class KnownFormatToGRPC_test : public beast::unit_test::suite
{
private:
    static constexpr auto fieldTYPE_UINT32 =
        google::protobuf::FieldDescriptor::Type::TYPE_UINT32;

    static constexpr auto fieldTYPE_UINT64 =
        google::protobuf::FieldDescriptor::Type::TYPE_UINT64;

    static constexpr auto fieldTYPE_BYTES =
        google::protobuf::FieldDescriptor::Type::TYPE_BYTES;

    static constexpr auto fieldTYPE_STRING =
        google::protobuf::FieldDescriptor::Type::TYPE_STRING;

    static constexpr auto fieldTYPE_MESSAGE =
        google::protobuf::FieldDescriptor::Type::TYPE_MESSAGE;

    // Format names are CamelCase and FieldDescriptor names are snake_case.
    // Convert from CamelCase to snake_case.  Do not be fooled by consecutive
    // capital letters like in NegativeUNL.
    static std::string
    formatNameToEntryTypeName(std::string const& fmtName)
    {
        std::string entryName;
        entryName.reserve(fmtName.size());
        bool prevUpper = false;
        for (std::size_t i = 0; i < fmtName.size(); i++)
        {
            char const ch = fmtName[i];
            bool const upper = std::isupper(ch);
            if (i > 0 && !prevUpper && upper)
                entryName.push_back('_');

            prevUpper = upper;
            entryName.push_back(std::tolower(ch));
        }
        return entryName;
    };

    // Create a map of (most) all the SFields in an SOTemplate.  This map
    // can be used to correlate a gRPC Descriptor to its corresponding SField.
    template <typename KeyType>
    static std::map<std::string, SField const*>
    soTemplateToSFields(
        SOTemplate const& soTemplate,
        [[maybe_unused]] KeyType fmtId)
    {
        std::map<std::string, SField const*> sFields;
        for (SOElement const& element : soTemplate)
        {
            SField const& sField = element.sField();

            // Fields that gRPC never includes.
            //
            //   o sfLedgerIndex and
            //   o sfLedgerEntryType are common to all ledger objects, so
            //     gRPC includes them at a higher level than the ledger
            //     object itself.
            //
            //   o sfOperationLimit is an optional field in all transactions,
            //     but no one knows what it was intended for.
            using FieldCode_t =
                std::remove_const<decltype(SField::fieldCode)>::type;
            static const std::set<FieldCode_t> excludedSFields{
                sfLedgerIndex.fieldCode,
                sfLedgerEntryType.fieldCode,
                sfOperationLimit.fieldCode};

            if (excludedSFields.count(sField.fieldCode))
                continue;

            // There are certain fields that gRPC never represents in
            // transactions.  Exclude those.
            //
            //   o sfPreviousTxnID is obsolete and was replaced by
            //     sfAccountTxnID some time before November of 2014.
            //
            //   o sfWalletLocator and
            //   o sfWalletSize have been deprecated for six years or more.
            //
            //   o sfTransactionType is not needed by gRPC, since the typing
            //     is handled using protobuf message types.
            if constexpr (std::is_same_v<KeyType, TxType>)
            {
                static const std::set<FieldCode_t> excludedTxFields{
                    sfPreviousTxnID.fieldCode,
                    sfTransactionType.fieldCode,
                    sfWalletLocator.fieldCode,
                    sfWalletSize.fieldCode};

                if (excludedTxFields.count(sField.fieldCode))
                    continue;
            }

            // If fmtId is a LedgerEntryType, exclude certain fields.
            if constexpr (std::is_same_v<KeyType, LedgerEntryType>)
            {
                // Fields that gRPC does not include in certain LedgerFormats.
                //
                //   o sfWalletLocator,
                //   o sfWalletSize,
                //   o sfExchangeRate, and
                //   o sfFirstLedgerSequence are all deprecated fields in
                //     their respective ledger objects.
                static const std::
                    map<LedgerEntryType, std::vector<SField const*>>
                        gRPCOmitFields{
                            {ltACCOUNT_ROOT, {&sfWalletLocator, &sfWalletSize}},
                            {ltDIR_NODE, {&sfExchangeRate}},
                            {ltLEDGER_HASHES, {&sfFirstLedgerSequence}},
                        };

                if (auto const iter = gRPCOmitFields.find(fmtId);
                    iter != gRPCOmitFields.end())
                {
                    std::vector<SField const*> const& omits = iter->second;

                    // Check for fields that gRPC omits from this type.
                    if (std::find_if(
                            omits.begin(),
                            omits.end(),
                            [&sField](SField const* const omit) {
                                return *omit == sField;
                            }) != omits.end())
                    {
                        // This is one of the fields that gRPC omits.
                        continue;
                    }
                }
            }

            // The SFields and gRPC disagree on the names of some fields.
            // Provide a mapping from SField names to gRPC names for the
            // known exceptions.
            //
            // clang-format off
            //
            // The implementers of the gRPC interface made the decision not
            // to abbreviate anything.  This accounts for the following
            // field name differences:
            //
            //   "AccountTxnID",      "AccountTransactionID"
            //   "PreviousTxnID",     "PreviousTransactionID"
            //   "PreviousTxnLgrSeq", "PreviousTransactionLedgerSequence"
            //   "SigningPubKey",     "SigningPublicKey"
            //   "TxnSignature",      "TransactionSignature"
            //
            // gRPC adds typing information for Fee, which accounts for
            //   "Fee",               "XRPDropsAmount"
            //
            // There's one misspelling which accounts for
            //   "TakerGetsCurrency", "TakerGetsCurreny"
            //
            // The implementers of the gRPC interface observed that a
            // PaymentChannelClaim transaction has a TxnSignature field at the
            // upper level and a Signature field at the lever level.  They
            // felt that was confusing, which is the reason for
            //    "Signature",         "PaymentChannelSignature"
            //
            static const std::map<std::string, std::string> sFieldToGRPC{
                {"AccountTxnID",      "AccountTransactionID"},
                {"Fee",               "XRPDropsAmount"},
                {"PreviousTxnID",     "PreviousTransactionID"},
                {"PreviousTxnLgrSeq", "PreviousTransactionLedgerSequence"},
                {"Signature",         "PaymentChannelSignature"},
                {"SigningPubKey",     "SigningPublicKey"},
                {"TakerGetsCurrency", "TakerGetsCurreny"},
                {"TxnSignature",      "TransactionSignature"},
            };
            // clang-format on

            auto const iter = sFieldToGRPC.find(sField.getName());
            std::string gRPCName =
                iter != sFieldToGRPC.end() ? iter->second : sField.getName();

            sFields.insert({std::move(gRPCName), &sField});
        }
        return sFields;
    }

    // Given a Descriptor for a KnownFormat and a map of the SFields of that
    // KnownFormat, make sure the fields are aligned.
    void
    validateDescriptorAgainstSFields(
        google::protobuf::Descriptor const* const pbufDescriptor,
        google::protobuf::Descriptor const* const commonFields,
        std::string const& knownFormatName,
        std::map<std::string, SField const*>&& sFields)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        // We'll be running through two sets of pbuf::Descriptors: the ones in
        // the OneOf and the common fields.  Here is a lambda that factors out
        // the common checking code for these two cases.
        auto checkFieldDesc =
            [this, &sFields, &knownFormatName](
                pbuf::FieldDescriptor const* const fieldDesc) {
                // gRPC has different handling for repeated vs non-repeated
                // types.  So we need to do that too.
                std::string name;
                if (fieldDesc->is_repeated())
                {
                    // Repeated-type handling.

                    // Munge the fieldDescriptor name so it looks like the
                    // name in sFields.
                    name = fieldDesc->camelcase_name();
                    name[0] = toupper(name[0]);

                    // The ledger gives UNL all caps.  Adapt to that.
                    if (size_t const i = name.find("Unl");
                        i != std::string::npos)
                    {
                        name[i + 1] = 'N';
                        name[i + 2] = 'L';
                    }

                    if (!sFields.count(name))
                    {
                        fail(
                            std::string("Repeated Protobuf Descriptor '") +
                                name + "' expected in KnownFormat '" +
                                knownFormatName + "' and not found",
                            __FILE__,
                            __LINE__);
                        return;
                    }
                    pass();

                    validateRepeatedField(fieldDesc, sFields.at(name));
                }
                else
                {
                    // Non-repeated handling.
                    pbuf::Descriptor const* const entryDesc =
                        fieldDesc->message_type();
                    if (entryDesc == nullptr)
                        return;

                    name = entryDesc->name();
                    if (!sFields.count(name))
                    {
                        fail(
                            std::string("Protobuf Descriptor '") +
                                entryDesc->name() +
                                "' expected in KnownFormat '" +
                                knownFormatName + "' and not found",
                            __FILE__,
                            __LINE__);
                        return;
                    }
                    pass();

                    validateDescriptor(
                        entryDesc, sFields.at(entryDesc->name()));
                }
                // Remove the validated field from the map so we can tell if
                // there are left over fields at the end of all comparisons.
                sFields.erase(name);
            };

        // Compare the SFields to the FieldDescriptor->Descriptors.
        for (int i = 0; i < pbufDescriptor->field_count(); ++i)
        {
            pbuf::FieldDescriptor const* const fieldDesc =
                pbufDescriptor->field(i);
            if (fieldDesc == nullptr || fieldDesc->type() != fieldTYPE_MESSAGE)
                continue;

            checkFieldDesc(fieldDesc);
        }

        // Now all of the OneOf-specific fields have been removed from
        // sFields.  But there may be common fields left in there.  Process
        // the commonFields next.
        if (commonFields)
        {
            for (int i = 0; i < commonFields->field_count(); ++i)
            {
                // If the field we picked up is a OneOf, skip it.  Common
                // fields are never OneOfs.
                pbuf::FieldDescriptor const* const fieldDesc =
                    commonFields->field(i);

                if (fieldDesc == nullptr ||
                    fieldDesc->containing_oneof() != nullptr ||
                    fieldDesc->type() != fieldTYPE_MESSAGE)
                    continue;

                checkFieldDesc(fieldDesc);
            }
        }

        // All SFields in the KnownFormat have corresponding gRPC fields
        // if the sFields map is now empty.
        if (!sFields.empty())
        {
            fail(
                std::string("Protobuf Descriptor '") + pbufDescriptor->name() +
                    "' did not account for all fields in KnownFormat '" +
                    knownFormatName + "'.  Left over field: `" +
                    sFields.begin()->first + "'",
                __FILE__,
                __LINE__);
            return;
        }
        pass();
    }

    // Compare a protobuf descriptor with multiple oneOfFields to choose from
    // to an SField.
    void
    validateOneOfDescriptor(
        google::protobuf::Descriptor const* const entryDesc,
        SField const* const sField)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        // Note that it's not okay to compare names because SFields and
        // gRPC do not always agree on the names.
        if (entryDesc->field_count() == 0 || entryDesc->oneof_decl_count() != 1)
        {
            fail(
                std::string("Protobuf Descriptor '") + entryDesc->name() +
                    "' expected to have multiple OneOf fields and nothing else",
                __FILE__,
                __LINE__);
            return;
        }

        pbuf::FieldDescriptor const* const fieldDesc = entryDesc->field(0);
        if (fieldDesc == nullptr)
        {
            fail(
                std::string("Internal test failure.  Unhandled nullptr "
                            "in FieldDescriptor for '") +
                    entryDesc->name() + "'",
                __FILE__,
                __LINE__);
            return;
        }

        // Special handling for CurrencyAmount
        if (sField->fieldType == STI_AMOUNT &&
            entryDesc->name() == "CurrencyAmount")
        {
            // SFields of type STI_AMOUNT are represented in gRPC by a
            // multi-field CurrencyAmount.  We don't really learn anything
            // by diving into the interior of CurrencyAmount, so we stop here
            // and call it good.
            pass();
            return;
        }

        fail(
            std::string("Unhandled OneOf Protobuf Descriptor '") +
                entryDesc->name() + "'",
            __FILE__,
            __LINE__);
    }

    void
    validateMultiFieldDescriptor(
        google::protobuf::Descriptor const* const entryDesc,
        SField const* const sField)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        if (entryDesc->field_count() <= 1 || entryDesc->oneof_decl_count() != 0)
        {
            fail(
                std::string("Protobuf Descriptor '") + entryDesc->name() +
                    "' expected to have multiple fields and nothing else",
                __FILE__,
                __LINE__);
            return;
        }

        // There are composite fields that the SFields handle differently
        // from gRPC.  Handle those here.
        {
            struct FieldContents
            {
                std::string_view fieldName;
                google::protobuf::FieldDescriptor::Type fieldType;

                bool
                operator<(FieldContents const& other) const
                {
                    return this->fieldName < other.fieldName;
                }

                bool
                operator==(FieldContents const& other) const
                {
                    return this->fieldName == other.fieldName &&
                        this->fieldType == other.fieldType;
                }
            };

            struct SpecialEntry
            {
                std::string_view const descriptorName;
                SerializedTypeID const sFieldType;
                std::set<FieldContents> const fields;
            };

            // clang-format off
            static const std::array specialEntries{
                SpecialEntry{
                    "Currency", STI_HASH160,
                    {
                        {"name", fieldTYPE_STRING},
                        {"code", fieldTYPE_BYTES}
                    }
                },
                SpecialEntry{
                    "Memo", STI_OBJECT,
                    {
                        {"memo_data", fieldTYPE_BYTES},
                        {"memo_format", fieldTYPE_BYTES},
                        {"memo_type", fieldTYPE_BYTES}
                    }
                }
            };
            // clang-format on

            // If we're handling a SpecialEntry...
            if (auto const iter = std::find_if(
                    specialEntries.begin(),
                    specialEntries.end(),
                    [entryDesc, sField](SpecialEntry const& entry) {
                        return entryDesc->name() == entry.descriptorName &&
                            sField->fieldType == entry.sFieldType;
                    });
                iter != specialEntries.end())
            {
                // Verify the SField.
                if (!BEAST_EXPECT(sField->fieldType == iter->sFieldType))
                    return;

                // Verify all of the fields in the entryDesc.
                if (!BEAST_EXPECT(
                        entryDesc->field_count() == iter->fields.size()))
                    return;

                for (int i = 0; i < entryDesc->field_count(); ++i)
                {
                    pbuf::FieldDescriptor const* const fieldDesc =
                        entryDesc->field(i);

                    FieldContents const contents{
                        fieldDesc->name(), fieldDesc->type()};

                    if (!BEAST_EXPECT(
                            iter->fields.find(contents) != iter->fields.end()))
                        return;
                }

                // This field is good.
                pass();
                return;
            }
        }

        // If the field was not one of the SpecialEntries, we expect it to be
        // an InnerObjectFormat.
        SOTemplate const* const innerFormat =
            InnerObjectFormats::getInstance().findSOTemplateBySField(*sField);
        if (innerFormat == nullptr)
        {
            fail(
                "SOTemplate for field '" + sField->getName() + "' not found",
                __FILE__,
                __LINE__);
            return;
        }

        // Create a map we can use use to correlate each field in the
        // gRPC Descriptor to its corresponding SField.
        std::map<std::string, SField const*> sFields =
            soTemplateToSFields(*innerFormat, 0);

        // Compare the SFields to the FieldDescriptor->Descriptors.
        validateDescriptorAgainstSFields(
            entryDesc, nullptr, sField->getName(), std::move(sFields));
    }

    // Compare a protobuf descriptor with only one field to an SField.
    void
    validateOneDescriptor(
        google::protobuf::Descriptor const* const entryDesc,
        SField const* const sField)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        // Note that it's not okay to compare names because SFields and
        // gRPC do not always agree on the names.
        if (entryDesc->field_count() != 1 || entryDesc->oneof_decl_count() != 0)
        {
            fail(
                std::string("Protobuf Descriptor '") + entryDesc->name() +
                    "' expected to be one field and nothing else",
                __FILE__,
                __LINE__);
            return;
        }

        pbuf::FieldDescriptor const* const fieldDesc = entryDesc->field(0);
        if (fieldDesc == nullptr)
        {
            fail(
                std::string("Internal test failure.  Unhandled nullptr "
                            "in FieldDescriptor for '") +
                    entryDesc->name() + "'",
                __FILE__,
                __LINE__);
            return;
        }

        // Create a map from SerializedTypeID to pbuf::FieldDescriptor::Type.
        //
        // This works for most, but not all, types because of divergence
        // between the gRPC and LedgerFormat implementations.  We deal
        // with the special cases later.
        // clang-format off
        static const std::map<SerializedTypeID, pbuf::FieldDescriptor::Type>
            sTypeToFieldDescType{
                {STI_UINT8,   fieldTYPE_UINT32},
                {STI_UINT16,  fieldTYPE_UINT32},
                {STI_UINT32,  fieldTYPE_UINT32},

                {STI_UINT64,  fieldTYPE_UINT64},

                {STI_ACCOUNT, fieldTYPE_STRING},

                {STI_AMOUNT,  fieldTYPE_BYTES},
                {STI_HASH128, fieldTYPE_BYTES},
                {STI_HASH160, fieldTYPE_BYTES},
                {STI_HASH256, fieldTYPE_BYTES},
                {STI_VL,      fieldTYPE_BYTES},
            };
        //clang-format on

        // If the SField and FieldDescriptor::Type correlate we're good.
        if (auto const iter = sTypeToFieldDescType.find(sField->fieldType);
            iter != sTypeToFieldDescType.end() &&
            iter->second == fieldDesc->type())
        {
            pass();
            return;
        }

        // Handle special cases for specific SFields.
        static const std::map<int, pbuf::FieldDescriptor::Type>
            sFieldCodeToFieldDescType{
                {sfDomain.fieldCode, fieldTYPE_STRING},
                {sfFee.fieldCode,    fieldTYPE_UINT64}};

        if (auto const iter = sFieldCodeToFieldDescType.find(sField->fieldCode);
            iter != sFieldCodeToFieldDescType.end() &&
            iter->second == fieldDesc->type())
        {
            pass();
            return;
        }

        // Special handling for all Message types.
        if (fieldDesc->type() == fieldTYPE_MESSAGE)
        {
            // We need to recurse to get to the bottom of the field(s)
            // in question.

            // Start by identifying which fields we need to be handling.
            // clang-format off
            static const std::map<int, std::string> messageMap{
                {sfAccount.fieldCode,           "AccountAddress"},
                {sfAmount.fieldCode,            "CurrencyAmount"},
                {sfAuthorize.fieldCode,         "AccountAddress"},
                {sfBalance.fieldCode,           "CurrencyAmount"},
                {sfDestination.fieldCode,       "AccountAddress"},
                {sfFee.fieldCode,               "XRPDropsAmount"},
                {sfHighLimit.fieldCode,         "CurrencyAmount"},
                {sfLowLimit.fieldCode,          "CurrencyAmount"},
                {sfOwner.fieldCode,             "AccountAddress"},
                {sfRegularKey.fieldCode,        "AccountAddress"},
                {sfSendMax.fieldCode,           "CurrencyAmount"},
                {sfTakerGets.fieldCode,         "CurrencyAmount"},
                {sfTakerGetsCurrency.fieldCode, "Currency"},
                {sfTakerPays.fieldCode,         "CurrencyAmount"},
                {sfTakerPaysCurrency.fieldCode, "Currency"},
            };
            // clang-format on
            if (messageMap.count(sField->fieldCode))
            {
                pbuf::Descriptor const* const entry2Desc =
                    fieldDesc->message_type();

                if (entry2Desc == nullptr)
                {
                    fail(
                        std::string("Unexpected gRPC.  ") + fieldDesc->name() +
                            " MESSAGE with null Descriptor",
                        __FILE__,
                        __LINE__);
                    return;
                }

                // The Descriptor name should match the messageMap name.
                if (messageMap.at(sField->fieldCode) != entry2Desc->name())
                {
                    fail(
                        std::string(
                            "Internal test error.  Mismatch between SField '") +
                            sField->getName() + "' and gRPC Descriptor name '" +
                            entry2Desc->name() + "'",
                        __FILE__,
                        __LINE__);
                    return;
                }
                pass();

                // Recurse to the next lower Descriptor.
                validateDescriptor(entry2Desc, sField);
            }
            return;
        }

        fail(
            std::string("Internal test error.  Unhandled FieldDescriptor '") +
                entryDesc->name() + "' has type `" + fieldDesc->type_name() +
                "` and label " + std::to_string(fieldDesc->label()),
            __FILE__,
            __LINE__);
    }

    // Compare a repeated protobuf FieldDescriptor to an SField.
    void
    validateRepeatedField(
        google::protobuf::FieldDescriptor const* const fieldDesc,
        SField const* const sField)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        pbuf::Descriptor const* const entryDesc = fieldDesc->message_type();
        if (entryDesc == nullptr)
        {
            fail(
                std::string("Expected Descriptor for repeated type ") +
                    sField->getName(),
                __FILE__,
                __LINE__);
            return;
        }

        // The following repeated types provide no further structure for their
        // in-ledger representation.  We just have to trust that the gRPC
        // representation is reasonable for what the ledger implements.
        static const std::set<std::string> noFurtherDetail{{sfPaths.getName()}};

        if (noFurtherDetail.count(sField->getName()))
        {
            // There is no Format representation for further details of this
            // repeated type.  We've done the best we can.
            pass();
            return;
        }

        // All of the repeated types that the test currently supports.
        static const std::map<std::string, SField const*> repeatsWhat{
            {sfAmendments.getName(), &sfAmendment},
            {sfDisabledValidators.getName(), &sfDisabledValidator},
            {sfHashes.getName(), &sfLedgerHash},
            {sfIndexes.getName(), &sfLedgerIndex},
            {sfMajorities.getName(), &sfMajority},
            {sfMemos.getName(), &sfMemo},
            {sfSignerEntries.getName(), &sfSignerEntry},
            {sfSigners.getName(), &sfSigner}};

        if (!repeatsWhat.count(sField->getName()))
        {
            fail(
                std::string("Unexpected repeated type ") + fieldDesc->name(),
                __FILE__,
                __LINE__);
            return;
        }
        pass();

        // Process the type contained by the repeated type.
        validateDescriptor(entryDesc, repeatsWhat.at(sField->getName()));
    }

    // Determine which of the Descriptor validators to dispatch to.
    void
    validateDescriptor(
        google::protobuf::Descriptor const* const entryDesc,
        SField const* const sField)
    {
        if (entryDesc->nested_type_count() != 0 ||
            entryDesc->enum_type_count() != 0 ||
            entryDesc->extension_range_count() != 0 ||
            entryDesc->reserved_range_count() != 0)
        {
            fail(
                std::string("Protobuf Descriptor '") + entryDesc->name() +
                    "' uses unsupported protobuf features",
                __FILE__,
                __LINE__);
            return;
        }

        // Dispatch to the correct validator
        if (entryDesc->oneof_decl_count() > 0)
            return validateOneOfDescriptor(entryDesc, sField);

        if (entryDesc->field_count() > 1)
            return validateMultiFieldDescriptor(entryDesc, sField);

        return validateOneDescriptor(entryDesc, sField);
    }

    // Compare a protobuf descriptor to a KnownFormat::Item
    template <typename FmtType>
    void
    validateFields(
        google::protobuf::Descriptor const* const pbufDescriptor,
        google::protobuf::Descriptor const* const commonFields,
        typename KnownFormats<FmtType>::Item const* const knownFormatItem)
    {
        // Create namespace aliases for shorter names.
        namespace pbuf = google::protobuf;

        // The names should usually be the same, but the bpufDescriptor
        // name might have "Object" appended.
        if (knownFormatItem->getName() != pbufDescriptor->name() &&
            knownFormatItem->getName() + "Object" != pbufDescriptor->name())
        {
            fail(
                std::string("Protobuf Descriptor '") + pbufDescriptor->name() +
                    "' and KnownFormat::Item '" + knownFormatItem->getName() +
                    "' don't have the same name",
                __FILE__,
                __LINE__);
            return;
        }
        pass();

        // Create a map we can use use to correlate each field in the
        // gRPC Descriptor to its corresponding SField.
        std::map<std::string, SField const*> sFields = soTemplateToSFields(
            knownFormatItem->getSOTemplate(), knownFormatItem->getType());

        // Compare the SFields to the FieldDescriptor->Descriptors.
        validateDescriptorAgainstSFields(
            pbufDescriptor,
            commonFields,
            knownFormatItem->getName(),
            std::move(sFields));
    }

    template <typename FmtType>
    void
    testKnownFormats(
        KnownFormats<FmtType> const& knownFormat,
        std::string const& knownFormatName,
        google::protobuf::Descriptor const* const commonFields,
        google::protobuf::OneofDescriptor const* const oneOfDesc)
    {
        // Create namespace aliases for shorter names.
        namespace grpc = org::xrpl::rpc::v1;
        namespace pbuf = google::protobuf;

        if (!BEAST_EXPECT(oneOfDesc != nullptr))
            return;

        // Get corresponding names for all KnownFormat Items.
        std::map<std::string, typename KnownFormats<FmtType>::Item const*>
            formatTypes;

        for (auto const& item : knownFormat)
        {
            if constexpr (std::is_same_v<FmtType, LedgerEntryType>)
            {
                // Skip LedgerEntryTypes that gRPC does not currently support.
                static constexpr std::array<LedgerEntryType, 0> notSupported{};

                if (std::find(
                        notSupported.begin(),
                        notSupported.end(),
                        item.getType()) != notSupported.end())
                    continue;
            }

            if constexpr (std::is_same_v<FmtType, TxType>)
            {
                // Skip TxTypes that gRPC does not currently support.
                static constexpr std::array notSupported{
                    ttAMENDMENT, ttFEE, ttUNL_MODIFY};

                if (std::find(
                        notSupported.begin(),
                        notSupported.end(),
                        item.getType()) != notSupported.end())
                    continue;
            }

            BEAST_EXPECT(
                formatTypes
                    .insert({formatNameToEntryTypeName(item.getName()), &item})
                    .second == true);
        }

        // Verify that the OneOf objects match.  Start by comparing
        // KnownFormat vs gRPC OneOf counts.
        {
            BEAST_EXPECT(formatTypes.size() == oneOfDesc->field_count());
        }

        // This loop
        //  1. Iterates through the gRPC OneOfs,
        //  2. Finds each gRPC OneOf's matching KnownFormat::Item,
        //  3. Sanity checks that the fields of the objects align well.
        for (auto i = 0; i < oneOfDesc->field_count(); ++i)
        {
            pbuf::FieldDescriptor const* const fieldDesc = oneOfDesc->field(i);

            // The Field should be a TYPE_MESSAGE, which means we can get its
            // descriptor.
            if (fieldDesc->type() != fieldTYPE_MESSAGE)
            {
                fail(
                    std::string("gRPC OneOf '") + fieldDesc->name() +
                        "' is not TYPE_MESSAGE",
                    __FILE__,
                    __LINE__);
                continue;
            }

            auto const fmtIter = formatTypes.find(fieldDesc->name());

            if (fmtIter == formatTypes.cend())
            {
                fail(
                    std::string("gRPC OneOf '") + fieldDesc->name() +
                        "' not found in " + knownFormatName,
                    __FILE__,
                    __LINE__);
                continue;
            }

            // Validate that the gRPC and KnownFormat fields align.
            validateFields<FmtType>(
                fieldDesc->message_type(), commonFields, fmtIter->second);

            // Remove the checked KnownFormat from the map.  This way we
            // can check for leftovers when we're done processing.
            formatTypes.erase(fieldDesc->name());
        }

        // Report any KnownFormats that don't have gRPC OneOfs.
        for (auto const& spare : formatTypes)
        {
            fail(
                knownFormatName + " '" + spare.second->getName() +
                    "' does not have a corresponding gRPC OneOf",
                __FILE__,
                __LINE__);
        }
    }

public:
    void
    testLedgerObjectGRPCOneOfs()
    {
        testcase("Ledger object validation");

        org::xrpl::rpc::v1::LedgerObject const ledgerObject;

        testKnownFormats(
            LedgerFormats::getInstance(),
            "LedgerFormats",
            ledgerObject.GetDescriptor(),
            ledgerObject.GetDescriptor()->FindOneofByName("object"));

        return;
    }

    void
    testTransactionGRPCOneOfs()
    {
        testcase("Transaction validation");

        org::xrpl::rpc::v1::Transaction const txData;

        testKnownFormats(
            TxFormats::getInstance(),
            "TxFormats",
            txData.GetDescriptor(),
            txData.GetDescriptor()->FindOneofByName("transaction_data"));

        return;
    }

    void
    run() override
    {
        testLedgerObjectGRPCOneOfs();
        testTransactionGRPCOneOfs();
    }
};

BEAST_DEFINE_TESTSUITE(KnownFormatToGRPC, protocol, ripple);

}  // namespace ripple
