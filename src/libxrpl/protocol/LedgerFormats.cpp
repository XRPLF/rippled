#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>

#include <initializer_list>

namespace xrpl {

std::initializer_list<SOElement> const&
LedgerFormats::getCommonFields()
{
    auto static const commonFields = std::initializer_list<SOElement>{
        {sfLedgerIndex, soeOPTIONAL},
        {sfLedgerEntryType, soeREQUIRED},
        {sfFlags, soeREQUIRED},
    };
    return commonFields;
}

LedgerFormats::LedgerFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define UNWRAP(...) __VA_ARGS__
#define LEDGER_ENTRY(tag, value, name, rpcName, fields) add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

LedgerFormats const&
LedgerFormats::getInstance()
{
    static LedgerFormats instance;
    return instance;
}

}  // namespace xrpl
