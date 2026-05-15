#include <xrpl/protocol/TxFormats.h>

#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>  // IWYU pragma: keep

#include <vector>

namespace xrpl {

std::vector<SOElement> const&
TxFormats::getCommonFields()
{
    static auto const kCommonFields = std::vector<SOElement>{
        {sfTransactionType, SoeRequired},
        {sfFlags, SoeOptional},
        {sfSourceTag, SoeOptional},
        {sfAccount, SoeRequired},
        {sfSequence, SoeRequired},
        {sfPreviousTxnID, SoeOptional},  // emulate027
        {sfLastLedgerSequence, SoeOptional},
        {sfAccountTxnID, SoeOptional},
        {sfFee, SoeRequired},
        {sfOperationLimit, SoeOptional},
        {sfMemos, SoeOptional},
        {sfSigningPubKey, SoeRequired},
        {sfTicketSequence, SoeOptional},
        {sfTxnSignature, SoeOptional},
        {sfSigners, SoeOptional},  // submit_multisigned
        {sfNetworkID, SoeOptional},
        {sfDelegate, SoeOptional},
    };
    return kCommonFields;
}

TxFormats::TxFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const kInstance;
    return kInstance;
}

}  // namespace xrpl
