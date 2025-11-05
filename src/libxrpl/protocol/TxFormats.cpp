#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <initializer_list>

namespace ripple {

TxFormats::TxFormats()
{
    // Fields shared by all txFormats:
    static std::initializer_list<SOElement> const commonFields{
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

#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(                                              \
    tag, value, name, delegatable, amendment, privileges, fields) \
    add(jss::name, tag, UNWRAP fields, commonFields);

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

}  // namespace ripple
