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
    static auto const commonFields = std::vector<SOElement>{
        {sfTransactionType, soeREQUIRED},
        {sfFlags, soeOPTIONAL},
        {sfSourceTag, soeOPTIONAL},
        {sfAccount, soeREQUIRED},
        {sfSequence, soeREQUIRED},
        {sfPreviousTxnID, soeOPTIONAL},  // emulate027
        {sfLastLedgerSequence, soeOPTIONAL},
        {sfAccountTxnID, soeOPTIONAL},
        {sfFee, soeREQUIRED},
        {sfOperationLimit, soeOPTIONAL},
        {sfMemos, soeOPTIONAL},
        {sfSigningPubKey, soeREQUIRED},
        {sfTicketSequence, soeOPTIONAL},
        {sfTxnSignature, soeOPTIONAL},
        {sfSigners, soeOPTIONAL},  // submit_multisigned
        {sfNetworkID, soeOPTIONAL},
        {sfDelegate, soeOPTIONAL},
    };
    return commonFields;
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
    static TxFormats const instance;
    return instance;
}

}  // namespace xrpl
