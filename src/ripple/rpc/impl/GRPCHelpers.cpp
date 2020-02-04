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

#include <ripple/rpc/impl/GRPCHelpers.h>

namespace ripple {
namespace RPC {

// In the below populateProto* functions, getProto is a function that returns
// a reference to the mutable protobuf message to be populated. The reason this
// is a function, as opposed to just a pointer or reference to the object,
// is that there is no way to get a non-const reference, and getting a pointer
// to the proto object causes default initialization of the object. However,
// if the corresponding field is not present in the STObject, we don't want to
// initialize the proto object. To get around this, getProto is a function that
// is called only if the field is present in the STObject
template <class T, class L>
void
populateProtoPrimitive(
    L const& getProto,
    STObject const& from,
    TypedField<T> const& field)
{
    if (!from.isFieldPresent(field))
        return;

    if constexpr (std::is_integral_v<typename T::value_type>)
    {
        getProto()->set_value(from[field]);
    }
    else
    {
        auto v = from[field];
        getProto()->set_value(v.data(), v.size());
    }
}

template <class T>
void
populateProtoVLasString(
    T const& getProto,
    STObject const& from,
    SF_Blob const& field)
{
    if (from.isFieldPresent(field))
    {
        auto data = from.getFieldVL(field);
        getProto()->set_value(
            reinterpret_cast<const char*>(data.data()), data.size());
    }
}

template <class T>
void
populateProtoVec256(
    T const& getProto,
    STObject const& from,
    SF_Vec256 const& field)
{
    if (from.isFieldPresent(field))
    {
        const STVector256& vec = from.getFieldV256(field);
        for (size_t i = 0; i < vec.size(); ++i)
        {
            uint256 const& elt = vec[i];
            getProto()->set_value(elt.data(), elt.size());
        }
    }
}

template <class T>
void
populateProtoAccount(
    T const& getProto,
    STObject const& from,
    SF_Account const& field)
{
    if (from.isFieldPresent(field))
    {
        getProto()->mutable_value()->set_address(
            toBase58(from.getAccountID(field)));
    }
}

template <class T>
void
populateProtoAmount(
    T const& getProto,
    STObject const& from,
    SF_Amount const& field)
{
    if (from.isFieldPresent(field))
    {
        auto amount = from.getFieldAmount(field);
        convert(*getProto(), amount);
    }
}

template <class T>
void
populateProtoCurrency(
    T const& getProto,
    STObject const& from,
    SF_U160 const& field)
{
    if (from.isFieldPresent(field))
    {
        auto cur = from.getFieldH160(field);
        auto proto = getProto()->mutable_value();
        proto->set_code(cur.data(), cur.size());
        proto->set_name(to_string(cur));
    }
}

template <class T, class R>
void
populateProtoArray(
    T const& getProto,
    R const& populateProto,
    STObject const& from,
    SField const& outerField,
    SField const& innerField)
{
    if (from.isFieldPresent(outerField) &&
        from.peekAtField(outerField).getSType() == SerializedTypeID::STI_ARRAY)
    {
        auto arr = from.getFieldArray(outerField);
        for (auto it = arr.begin(); it != arr.end(); ++it)
        {
            populateProto(*it, *getProto());
        }
    }
}

template <class T>
void
populateClearFlag(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_clear_flag(); }, from, sfClearFlag);
}

template <class T>
void
populateDomain(T& to, STObject const& from)
{
    populateProtoVLasString(
        [&to]() { return to.mutable_domain(); }, from, sfDomain);
}

template <class T>
void
populateEmailHash(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_email_hash(); }, from, sfEmailHash);
}

template <class T>
void
populateMessageKey(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_message_key(); }, from, sfMessageKey);
}

template <class T>
void
populateSetFlag(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_set_flag(); }, from, sfSetFlag);
}

template <class T>
void
populateTransferRate(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_transfer_rate(); }, from, sfTransferRate);
}

template <class T>
void
populateTickSize(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_tick_size(); }, from, sfTickSize);
}

template <class T>
void
populateExpiration(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_expiration(); }, from, sfExpiration);
}

template <class T>
void
populateOfferSequence(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_offer_sequence(); }, from, sfOfferSequence);
}

template <class T>
void
populateTakerGets(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_taker_gets(); }, from, sfTakerGets);
}

template <class T>
void
populateTakerPays(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_taker_pays(); }, from, sfTakerPays);
}

template <class T>
void
populateDestination(T& to, STObject const& from)
{
    populateProtoAccount(
        [&to]() { return to.mutable_destination(); }, from, sfDestination);
}

template <class T>
void
populateCheckID(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_check_id(); }, from, sfCheckID);
}

template <class T>
void
populateAmount(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_amount(); }, from, sfAmount);
}

template <class T>
void
populateDeliverMin(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_deliver_min(); }, from, sfDeliverMin);
}

template <class T>
void
populateSendMax(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_send_max(); }, from, sfSendMax);
}

template <class T>
void
populateDeliveredAmount(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_delivered_amount(); },
        from,
        sfDeliveredAmount);
}

template <class T>
void
populateDestinationTag(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_destination_tag(); },
        from,
        sfDestinationTag);
}

template <class T>
void
populateInvoiceID(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_invoice_id(); }, from, sfInvoiceID);
}

template <class T>
void
populateAuthorize(T& to, STObject const& from)
{
    populateProtoAccount(
        [&to]() { return to.mutable_authorize(); }, from, sfAuthorize);
}

template <class T>
void
populateUnauthorize(T& to, STObject const& from)
{
    populateProtoAccount(
        [&to]() { return to.mutable_unauthorize(); }, from, sfUnauthorize);
}

template <class T>
void
populateOwner(T& to, STObject const& from)
{
    populateProtoAccount([&to]() { return to.mutable_owner(); }, from, sfOwner);
}

template <class T>
void
populateCancelAfter(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_cancel_after(); }, from, sfCancelAfter);
}

template <class T>
void
populateFinishAfter(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_finish_after(); }, from, sfFinishAfter);
}

template <class T>
void
populateCondition(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_condition(); }, from, sfCondition);
}

template <class T>
void
populateFulfillment(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_fulfillment(); }, from, sfFulfillment);
}

template <class T>
void
populateChannel(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_channel(); }, from, sfPayChannel);
}

template <class T>
void
populateBalance(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_balance(); }, from, sfBalance);
}

template <class T>
void
populatePaymentChannelSignature(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_payment_channel_signature(); },
        from,
        sfSignature);
}

template <class T>
void
populatePublicKey(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_public_key(); }, from, sfPublicKey);
}

template <class T>
void
populateSettleDelay(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_settle_delay(); }, from, sfSettleDelay);
}

template <class T>
void
populateRegularKey(T& to, STObject const& from)
{
    populateProtoAccount(
        [&to]() { return to.mutable_regular_key(); }, from, sfRegularKey);
}

template <class T>
void
populateSignerQuorum(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_signer_quorum(); }, from, sfSignerQuorum);
}
template <class T>
void
populateLimitAmount(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_limit_amount(); }, from, sfLimitAmount);
}
template <class T>
void
populateQualityIn(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_quality_in(); }, from, sfQualityIn);
}

template <class T>
void
populateQualityOut(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_quality_out(); }, from, sfQualityOut);
}

template <class T>
void
populateAccount(T& to, STObject const& from)
{
    populateProtoAccount(
        [&to]() { return to.mutable_account(); }, from, sfAccount);
}

template <class T>
void
populateFee(T& to, STObject const& from)
{
    if (from.isFieldPresent(sfFee))
    {
        to.mutable_fee()->set_drops(from.getFieldAmount(sfFee).xrp().drops());
    }
}

template <class T>
void
populateSigningPublicKey(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_signing_public_key(); },
        from,
        sfSigningPubKey);
}

template <class T>
void
populateTransactionSignature(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_transaction_signature(); },
        from,
        sfTxnSignature);
}

template <class T>
void
populateFlags(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_flags(); }, from, sfFlags);
}

template <class T>
void
populateLastLedgerSequence(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_last_ledger_sequence(); },
        from,
        sfLastLedgerSequence);
}

template <class T>
void
populateSourceTag(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_source_tag(); }, from, sfSourceTag);
}

template <class T>
void
populateAccountTransactionID(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_account_transaction_id(); },
        from,
        sfAccountTxnID);
}

template <class T>
void
populateMemoData(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_memo_data(); }, from, sfMemoData);
}

template <class T>
void
populateMemoFormat(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_memo_format(); }, from, sfMemoFormat);
}

template <class T>
void
populateMemoType(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_memo_type(); }, from, sfMemoType);
}

template <class T>
void
populateSequence(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_sequence(); }, from, sfSequence);
}

template <class T>
void
populateAmendment(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_amendment(); }, from, sfAmendment);
}

template <class T>
void
populateCloseTime(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_close_time(); }, from, sfCloseTime);
}

template <class T>
void
populateSignerWeight(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_signer_weight(); }, from, sfSignerWeight);
}

template <class T>
void
populateAmendments(T& to, STObject const& from)
{
    populateProtoVec256(
        [&to]() { return to.add_amendments(); }, from, sfAmendments);
}

template <class T>
void
populateOwnerCount(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_owner_count(); }, from, sfOwnerCount);
}

template <class T>
void
populatePreviousTransactionID(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_previous_transaction_id(); },
        from,
        sfPreviousTxnID);
}

template <class T>
void
populatePreviousTransactionLedgerSequence(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_previous_transaction_ledger_sequence(); },
        from,
        sfPreviousTxnLgrSeq);
}

template <class T>
void
populateLowLimit(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_low_limit(); }, from, sfLowLimit);
}

template <class T>
void
populateHighLimit(T& to, STObject const& from)
{
    populateProtoAmount(
        [&to]() { return to.mutable_high_limit(); }, from, sfHighLimit);
}

template <class T>
void
populateLowNode(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_low_node(); }, from, sfLowNode);
}

template <class T>
void
populateHighNode(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_high_node(); }, from, sfHighNode);
}

template <class T>
void
populateLowQualityIn(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_low_quality_in(); }, from, sfLowQualityIn);
}

template <class T>
void
populateLowQualityOut(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_low_quality_out(); },
        from,
        sfLowQualityOut);
}

template <class T>
void
populateHighQualityIn(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_high_quality_in(); },
        from,
        sfHighQualityIn);
}

template <class T>
void
populateHighQualityOut(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_high_quality_out(); },
        from,
        sfHighQualityOut);
}

template <class T>
void
populateBookDirectory(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_book_directory(); }, from, sfBookDirectory);
}

template <class T>
void
populateBookNode(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_book_node(); }, from, sfBookNode);
}

template <class T>
void
populateOwnerNode(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_owner_node(); }, from, sfOwnerNode);
}

template <class T>
void
populateSignerListID(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_signer_list_id(); }, from, sfSignerListID);
}

template <class T>
void
populateHashes(T& to, STObject const& from)
{
    populateProtoVec256([&to]() { return to.add_hashes(); }, from, sfHashes);
}

template <class T>
void
populateIndexes(T& to, STObject const& from)
{
    populateProtoVec256([&to]() { return to.add_indexes(); }, from, sfIndexes);
}

template <class T>
void
populateRootIndex(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_root_index(); }, from, sfRootIndex);
}

template <class T>
void
populateIndexNext(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_index_next(); }, from, sfIndexNext);
}

template <class T>
void
populateIndexPrevious(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_index_previous(); }, from, sfIndexPrevious);
}

template <class T>
void
populateTakerPaysCurrency(T& to, STObject const& from)
{
    populateProtoCurrency(
        [&to]() { return to.mutable_taker_pays_currency(); },
        from,
        sfTakerPaysCurrency);
}

template <class T>
void
populateTakerPaysIssuer(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_taker_pays_issuer(); },
        from,
        sfTakerPaysIssuer);
}

template <class T>
void
populateTakerGetsCurrency(T& to, STObject const& from)
{
    populateProtoCurrency(
        [&to]() { return to.mutable_taker_gets_currency(); },
        from,
        sfTakerGetsCurrency);
}

template <class T>
void
populateTakerGetsIssuer(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_taker_gets_issuer(); },
        from,
        sfTakerGetsIssuer);
}

template <class T>
void
populateDestinationNode(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_destination_node(); },
        from,
        sfDestinationNode);
}

template <class T>
void
populateBaseFee(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_base_fee(); }, from, sfBaseFee);
}

template <class T>
void
populateReferenceFeeUnits(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_reference_fee_units(); },
        from,
        sfReferenceFeeUnits);
}

template <class T>
void
populateReserveBase(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_reserve_base(); }, from, sfReserveBase);
}

template <class T>
void
populateReserveIncrement(T& to, STObject const& from)
{
    populateProtoPrimitive(
        [&to]() { return to.mutable_reserve_increment(); },
        from,
        sfReserveIncrement);
}

template <class T>
void
populateSignerEntries(T& to, STObject const& from)
{
    populateProtoArray(
        [&to]() { return to.add_signer_entries(); },
        [](auto& innerObj, auto& innerProto) {
            populateAccount(innerProto, innerObj);
            populateSignerWeight(innerProto, innerObj);
        },
        from,
        sfSignerEntries,
        sfSignerEntry);
}

template <class T>
void
populateMemos(T& to, STObject const& from)
{
    populateProtoArray(
        [&to]() { return to.add_memos(); },
        [](auto& innerObj, auto& innerProto) {
            populateMemoData(innerProto, innerObj);
            populateMemoType(innerProto, innerObj);
            populateMemoFormat(innerProto, innerObj);
        },
        from,
        sfMemos,
        sfMemo);
}

template <class T>
void
populateSigners(T& to, STObject const& from)
{
    populateProtoArray(
        [&to]() { return to.add_signers(); },
        [](auto& innerObj, auto& innerProto) {
            populateAccount(innerProto, innerObj);
            populateTransactionSignature(innerProto, innerObj);
            populateSigningPublicKey(innerProto, innerObj);
        },
        from,
        sfSigners,
        sfSigner);
}

template <class T>
void
populateMajorities(T& to, STObject const& from)
{
    populateProtoArray(
        [&to]() { return to.add_majorities(); },
        [](auto innerObj, auto innerProto) {
            populateAmendment(innerProto, innerObj);
            populateCloseTime(innerProto, innerObj);
        },
        from,
        sfMajorities,
        sfMajority);
}

void
convert(org::xrpl::rpc::v1::TransactionResult& to, TER from)
{
    if (isTecClaim(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TEC);
    }
    if (isTefFailure(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TEF);
    }
    if (isTelLocal(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TEL);
    }
    if (isTemMalformed(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TEM);
    }
    if (isTerRetry(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TER);
    }
    if (isTesSuccess(from))
    {
        to.set_result_type(
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TES);
    }
}

void
convert(org::xrpl::rpc::v1::AccountSet& to, STObject const& from)
{
    populateClearFlag(to, from);

    populateDomain(to, from);

    populateEmailHash(to, from);

    populateMessageKey(to, from);

    populateSetFlag(to, from);

    populateTransferRate(to, from);

    populateTickSize(to, from);
}

void
convert(org::xrpl::rpc::v1::OfferCreate& to, STObject const& from)
{
    populateExpiration(to, from);

    populateOfferSequence(to, from);

    populateTakerGets(to, from);

    populateTakerPays(to, from);
}

void
convert(org::xrpl::rpc::v1::OfferCancel& to, STObject const& from)
{
    populateOfferSequence(to, from);
}

void
convert(org::xrpl::rpc::v1::AccountDelete& to, STObject const& from)
{
    populateDestination(to, from);
}

void
convert(org::xrpl::rpc::v1::CheckCancel& to, STObject const& from)
{
    populateCheckID(to, from);
}

void
convert(org::xrpl::rpc::v1::CheckCash& to, STObject const& from)
{
    populateCheckID(to, from);

    populateAmount(to, from);

    populateDeliverMin(to, from);
}

void
convert(org::xrpl::rpc::v1::CheckCreate& to, STObject const& from)
{
    populateDestination(to, from);

    populateSendMax(to, from);

    populateDestinationTag(to, from);

    populateExpiration(to, from);

    populateInvoiceID(to, from);
}

void
convert(org::xrpl::rpc::v1::DepositPreauth& to, STObject const& from)
{
    populateAuthorize(to, from);

    populateUnauthorize(to, from);
}

void
convert(org::xrpl::rpc::v1::EscrowCancel& to, STObject const& from)
{
    populateOwner(to, from);

    populateOfferSequence(to, from);
}

void
convert(org::xrpl::rpc::v1::EscrowCreate& to, STObject const& from)
{
    populateAmount(to, from);

    populateDestination(to, from);

    populateCancelAfter(to, from);

    populateFinishAfter(to, from);

    populateCondition(to, from);

    populateDestinationTag(to, from);
}

void
convert(org::xrpl::rpc::v1::EscrowFinish& to, STObject const& from)
{
    populateOwner(to, from);

    populateOfferSequence(to, from);

    populateCondition(to, from);

    populateFulfillment(to, from);
}

void
convert(org::xrpl::rpc::v1::PaymentChannelClaim& to, STObject const& from)
{
    populateChannel(to, from);

    populateBalance(to, from);

    populateAmount(to, from);

    populatePaymentChannelSignature(to, from);

    populatePublicKey(to, from);
}

void
convert(org::xrpl::rpc::v1::PaymentChannelCreate& to, STObject const& from)
{
    populateAmount(to, from);

    populateDestination(to, from);

    populateSettleDelay(to, from);

    populatePublicKey(to, from);

    populateCancelAfter(to, from);

    populateDestinationTag(to, from);
}

void
convert(org::xrpl::rpc::v1::PaymentChannelFund& to, STObject const& from)
{
    populateChannel(to, from);

    populateAmount(to, from);

    populateExpiration(to, from);
}

void
convert(org::xrpl::rpc::v1::SetRegularKey& to, STObject const& from)
{
    populateRegularKey(to, from);
}

void
convert(org::xrpl::rpc::v1::SignerListSet& to, STObject const& from)
{
    populateSignerQuorum(to, from);

    populateSignerEntries(to, from);
}

void
convert(org::xrpl::rpc::v1::TrustSet& to, STObject const& from)
{
    populateLimitAmount(to, from);

    populateQualityIn(to, from);

    populateQualityOut(to, from);
}

void
convert(org::xrpl::rpc::v1::Payment& to, STObject const& from)
{
    populateAmount(to, from);

    populateDestination(to, from);

    populateDestinationTag(to, from);

    populateInvoiceID(to, from);

    populateSendMax(to, from);

    populateDeliverMin(to, from);

    if (from.isFieldPresent(sfPaths))
    {
        // populate path data
        STPathSet const& pathset = from.getFieldPathSet(sfPaths);
        for (auto it = pathset.begin(); it < pathset.end(); ++it)
        {
            STPath const& path = *it;

            org::xrpl::rpc::v1::Payment_Path* protoPath = to.add_paths();

            for (auto it2 = path.begin(); it2 != path.end(); ++it2)
            {
                org::xrpl::rpc::v1::Payment_PathElement* protoElement =
                    protoPath->add_elements();
                STPathElement const& elt = *it2;

                if (elt.isOffer())
                {
                    if (elt.hasCurrency())
                    {
                        Currency const& currency = elt.getCurrency();
                        protoElement->mutable_currency()->set_name(
                            to_string(currency));
                    }
                    if (elt.hasIssuer())
                    {
                        AccountID const& issuer = elt.getIssuerID();
                        protoElement->mutable_issuer()->set_address(
                            toBase58(issuer));
                    }
                }
                else if (elt.isAccount())
                {
                    AccountID const& pathAccount = elt.getAccountID();
                    protoElement->mutable_account()->set_address(
                        toBase58(pathAccount));
                }
            }
        }
    }
}

void
convert(org::xrpl::rpc::v1::AccountRoot& to, STObject const& from)
{
    populateAccount(to, from);

    populateBalance(to, from);

    populateSequence(to, from);

    populateFlags(to, from);

    populateOwnerCount(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);

    populateAccountTransactionID(to, from);

    populateDomain(to, from);

    populateEmailHash(to, from);

    populateMessageKey(to, from);

    populateRegularKey(to, from);

    populateTickSize(to, from);

    populateTransferRate(to, from);
}

void
convert(org::xrpl::rpc::v1::Amendments& to, STObject const& from)
{
    populateAmendments(to, from);

    populateMajorities(to, from);
}

void
convert(org::xrpl::rpc::v1::Check& to, STObject const& from)
{
    populateAccount(to, from);

    populateDestination(to, from);

    populateFlags(to, from);

    populateOwnerNode(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);

    populateSendMax(to, from);

    populateSequence(to, from);

    populateDestinationNode(to, from);

    populateDestinationTag(to, from);

    populateExpiration(to, from);

    populateInvoiceID(to, from);

    populateSourceTag(to, from);
}

void
convert(org::xrpl::rpc::v1::DepositPreauthObject& to, STObject const& from)
{
    populateAccount(to, from);

    populateAuthorize(to, from);

    populateFlags(to, from);

    populateOwnerNode(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);
}

void
convert(org::xrpl::rpc::v1::FeeSettings& to, STObject const& from)
{
    populateBaseFee(to, from);

    populateReferenceFeeUnits(to, from);

    populateReserveBase(to, from);

    populateReserveIncrement(to, from);

    populateFlags(to, from);
}

void
convert(org::xrpl::rpc::v1::Escrow& to, STObject const& from)
{
    populateAccount(to, from);

    populateDestination(to, from);

    populateAmount(to, from);

    populateCondition(to, from);

    populateCancelAfter(to, from);

    populateFinishAfter(to, from);

    populateFlags(to, from);

    populateSourceTag(to, from);

    populateDestinationTag(to, from);

    populateOwnerNode(to, from);

    populateDestinationNode(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);
}

void
convert(org::xrpl::rpc::v1::LedgerHashes& to, STObject const& from)
{
    populateLastLedgerSequence(to, from);

    populateHashes(to, from);

    populateFlags(to, from);
}

void
convert(org::xrpl::rpc::v1::PayChannel& to, STObject const& from)
{
    populateAccount(to, from);

    populateAmount(to, from);

    populateBalance(to, from);

    populatePublicKey(to, from);

    populateSettleDelay(to, from);

    populateOwnerNode(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);

    populateFlags(to, from);

    populateExpiration(to, from);

    populateCancelAfter(to, from);

    populateSourceTag(to, from);

    populateDestinationTag(to, from);
}

void
convert(org::xrpl::rpc::v1::DirectoryNode& to, STObject const& from)
{
    populateFlags(to, from);

    populateRootIndex(to, from);

    populateIndexes(to, from);

    populateIndexNext(to, from);

    populateIndexPrevious(to, from);

    populateTakerPaysIssuer(to, from);

    populateTakerPaysCurrency(to, from);

    populateTakerGetsCurrency(to, from);

    populateTakerGetsIssuer(to, from);
}

void
convert(org::xrpl::rpc::v1::Offer& to, STObject const& from)
{
    populateAccount(to, from);

    populateSequence(to, from);

    populateFlags(to, from);

    populateTakerPays(to, from);

    populateTakerGets(to, from);

    populateBookDirectory(to, from);

    populateBookNode(to, from);
}

void
convert(org::xrpl::rpc::v1::RippleState& to, STObject const& from)
{
    populateBalance(to, from);

    populateFlags(to, from);

    populateLowNode(to, from);

    populateHighNode(to, from);

    populateLowQualityIn(to, from);

    populateLowQualityOut(to, from);

    populateHighQualityIn(to, from);

    populateHighQualityOut(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);
}

void
convert(org::xrpl::rpc::v1::SignerList& to, STObject const& from)
{
    populateFlags(to, from);

    populatePreviousTransactionID(to, from);

    populatePreviousTransactionLedgerSequence(to, from);

    populateOwnerNode(to, from);

    populateSignerEntries(to, from);

    populateSignerQuorum(to, from);

    populateSignerListID(to, from);
}

void
setLedgerEntryType(
    org::xrpl::rpc::v1::AffectedNode& proto,
    std::uint16_t lgrType)
{
    switch (lgrType)
    {
        case ltACCOUNT_ROOT:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_ACCOUNT_ROOT);
            break;
        case ltDIR_NODE:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_DIRECTORY_NODE);
            break;
        case ltRIPPLE_STATE:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_RIPPLE_STATE);
            break;
        case ltSIGNER_LIST:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_SIGNER_LIST);
            break;
        case ltOFFER:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_OFFER);
            break;
        case ltLEDGER_HASHES:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_LEDGER_HASHES);
            break;
        case ltAMENDMENTS:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_AMENDMENTS);
            break;
        case ltFEE_SETTINGS:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_FEE_SETTINGS);
            break;
        case ltESCROW:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_ESCROW);
            break;
        case ltPAYCHAN:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_PAY_CHANNEL);
            break;
        case ltCHECK:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_CHECK);
            break;
        case ltDEPOSIT_PREAUTH:
            proto.set_ledger_entry_type(
                org::xrpl::rpc::v1::LEDGER_ENTRY_TYPE_DEPOSIT_PREAUTH);
            break;
    }
}

template <class T>
void
convert(T& to, STObject& from, std::uint16_t type)
{
    switch (type)
    {
        case ltACCOUNT_ROOT:
            RPC::convert(*to.mutable_account_root(), from);
            break;
        case ltAMENDMENTS:
            RPC::convert(*to.mutable_amendments(), from);
            break;
        case ltDIR_NODE:
            RPC::convert(*to.mutable_directory_node(), from);
            break;
        case ltRIPPLE_STATE:
            RPC::convert(*to.mutable_ripple_state(), from);
            break;
        case ltSIGNER_LIST:
            RPC::convert(*to.mutable_signer_list(), from);
            break;
        case ltOFFER:
            RPC::convert(*to.mutable_offer(), from);
            break;
        case ltLEDGER_HASHES:
            RPC::convert(*to.mutable_ledger_hashes(), from);
            break;
        case ltFEE_SETTINGS:
            RPC::convert(*to.mutable_fee_settings(), from);
            break;
        case ltESCROW:
            RPC::convert(*to.mutable_escrow(), from);
            break;
        case ltPAYCHAN:
            RPC::convert(*to.mutable_pay_channel(), from);
            break;
        case ltCHECK:
            RPC::convert(*to.mutable_check(), from);
            break;
        case ltDEPOSIT_PREAUTH:
            RPC::convert(*to.mutable_deposit_preauth(), from);
            break;
    }
}

template <class T>
void
populateFields(
    T const& getProto,
    STObject& obj,
    SField const& field,
    uint16_t lgrType)
{
    // final fields
    if (obj.isFieldPresent(field))
    {
        STObject& data = obj.getField(field).downcast<STObject>();

        convert(*getProto(), data, lgrType);
    }
}

template <class T>
void
populateFinalFields(T const& getProto, STObject& obj, uint16_t lgrType)
{
    populateFields(getProto, obj, sfFinalFields, lgrType);
}

template <class T>
void
populatePreviousFields(T const& getProto, STObject& obj, uint16_t lgrType)
{
    populateFields(getProto, obj, sfPreviousFields, lgrType);
}

template <class T>
void
populateNewFields(T const& getProto, STObject& obj, uint16_t lgrType)
{
    populateFields(getProto, obj, sfNewFields, lgrType);
}

void
convert(org::xrpl::rpc::v1::Meta& to, std::shared_ptr<TxMeta> const& from)
{
    to.set_transaction_index(from->getIndex());

    convert(*to.mutable_transaction_result(), from->getResultTER());
    to.mutable_transaction_result()->set_result(
        transToken(from->getResultTER()));

    STArray& nodes = from->getNodes();
    for (auto it = nodes.begin(); it != nodes.end(); ++it)
    {
        STObject& obj = *it;
        org::xrpl::rpc::v1::AffectedNode* node = to.add_affected_nodes();

        // ledger index
        uint256 ledgerIndex = obj.getFieldH256(sfLedgerIndex);
        node->set_ledger_index(ledgerIndex.data(), ledgerIndex.size());

        // ledger entry type
        std::uint16_t lgrType = obj.getFieldU16(sfLedgerEntryType);
        setLedgerEntryType(*node, lgrType);

        // modified node
        if (obj.getFName() == sfModifiedNode)
        {
            populateFinalFields(
                [&node]() {
                    return node->mutable_modified_node()
                        ->mutable_final_fields();
                },
                obj,
                lgrType);

            populatePreviousFields(
                [&node]() {
                    return node->mutable_modified_node()
                        ->mutable_previous_fields();
                },
                obj,
                lgrType);

            populatePreviousTransactionID(*node->mutable_modified_node(), obj);

            populatePreviousTransactionLedgerSequence(
                *node->mutable_modified_node(), obj);
        }
        // created node
        else if (obj.getFName() == sfCreatedNode)
        {
            populateNewFields(
                [&node]() {
                    return node->mutable_created_node()->mutable_new_fields();
                },
                obj,
                lgrType);
        }
        // deleted node
        else if (obj.getFName() == sfDeletedNode)
        {
            populateFinalFields(
                [&node]() {
                    return node->mutable_deleted_node()->mutable_final_fields();
                },
                obj,
                lgrType);
        }
    }
}

void
convert(
    org::xrpl::rpc::v1::QueueData& to,
    std::map<TxSeq, TxQ::AccountTxDetails const> const& from)
{
    if (!from.empty())
    {
        to.set_txn_count(from.size());
        to.set_lowest_sequence(from.begin()->first);
        to.set_highest_sequence(from.rbegin()->first);

        boost::optional<bool> anyAuthChanged(false);
        boost::optional<XRPAmount> totalSpend(0);

        for (auto const& [txSeq, txDetails] : from)
        {
            org::xrpl::rpc::v1::QueuedTransaction& qt = *to.add_transactions();

            qt.mutable_sequence()->set_value(txSeq);
            qt.set_fee_level(txDetails.feeLevel.fee());
            if (txDetails.lastValid)
                qt.mutable_last_ledger_sequence()->set_value(
                    *txDetails.lastValid);

            if (txDetails.consequences)
            {
                qt.mutable_fee()->set_drops(
                    txDetails.consequences->fee.drops());
                auto spend = txDetails.consequences->potentialSpend +
                    txDetails.consequences->fee;
                qt.mutable_max_spend_drops()->set_drops(spend.drops());
                if (totalSpend)
                    *totalSpend += spend;
                auto authChanged =
                    txDetails.consequences->category == TxConsequences::blocker;
                if (authChanged)
                    anyAuthChanged.emplace(authChanged);
                qt.set_auth_change(authChanged);
            }
            else
            {
                if (anyAuthChanged && !*anyAuthChanged)
                    anyAuthChanged.reset();
                totalSpend.reset();
            }
        }

        if (anyAuthChanged)
            to.set_auth_change_queued(*anyAuthChanged);
        if (totalSpend)
            to.mutable_max_spend_drops_total()->set_drops(
                (*totalSpend).drops());
    }
}

void
convert(
    org::xrpl::rpc::v1::Transaction& to,
    std::shared_ptr<STTx const> const& from)
{
    STObject const& fromObj = *from;

    populateAccount(to, fromObj);

    populateFee(to, fromObj);

    populateSequence(to, fromObj);

    populateSigningPublicKey(to, fromObj);

    populateTransactionSignature(to, fromObj);

    populateFlags(to, fromObj);

    populateLastLedgerSequence(to, fromObj);

    populateSourceTag(to, fromObj);

    populateAccountTransactionID(to, fromObj);

    populateMemos(to, fromObj);

    populateSigners(to, fromObj);

    auto type = safe_cast<TxType>(fromObj.getFieldU16(sfTransactionType));

    switch (type)
    {
        case TxType::ttPAYMENT:
            convert(*to.mutable_payment(), fromObj);
            break;
        case TxType::ttESCROW_CREATE:
            convert(*to.mutable_escrow_create(), fromObj);
            break;
        case TxType::ttESCROW_FINISH:
            convert(*to.mutable_escrow_finish(), fromObj);
            break;
        case TxType::ttACCOUNT_SET:
            convert(*to.mutable_account_set(), fromObj);
            break;
        case TxType::ttESCROW_CANCEL:
            convert(*to.mutable_escrow_cancel(), fromObj);
            break;
        case TxType::ttREGULAR_KEY_SET:
            convert(*to.mutable_set_regular_key(), fromObj);
            break;
        case TxType::ttOFFER_CREATE:
            convert(*to.mutable_offer_create(), fromObj);
            break;
        case TxType::ttOFFER_CANCEL:
            convert(*to.mutable_offer_cancel(), fromObj);
            break;
        case TxType::ttSIGNER_LIST_SET:
            convert(*to.mutable_signer_list_set(), fromObj);
            break;
        case TxType::ttPAYCHAN_CREATE:
            convert(*to.mutable_payment_channel_create(), fromObj);
            break;
        case TxType::ttPAYCHAN_FUND:
            convert(*to.mutable_payment_channel_fund(), fromObj);
            break;
        case TxType::ttPAYCHAN_CLAIM:
            convert(*to.mutable_payment_channel_claim(), fromObj);
            break;
        case TxType::ttCHECK_CREATE:
            convert(*to.mutable_check_create(), fromObj);
            break;
        case TxType::ttCHECK_CASH:
            convert(*to.mutable_check_cash(), fromObj);
            break;
        case TxType::ttCHECK_CANCEL:
            convert(*to.mutable_check_cancel(), fromObj);
            break;
        case TxType::ttDEPOSIT_PREAUTH:
            convert(*to.mutable_deposit_preauth(), fromObj);
            break;
        case TxType::ttTRUST_SET:
            convert(*to.mutable_trust_set(), fromObj);
            break;
        case TxType::ttACCOUNT_DELETE:
            convert(*to.mutable_account_delete(), fromObj);
            break;
        default:
            break;
    }
}

}  // namespace RPC
}  // namespace ripple
