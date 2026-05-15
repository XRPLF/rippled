#include <xrpl/protocol/LedgerFormats.h>

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/jss.h>  // IWYU pragma: keep

#include <vector>

namespace xrpl {

std::vector<SOElement> const&
LedgerFormats::getCommonFields()
{
    static auto const kCommonFields = std::vector<SOElement>{
        {sfLedgerIndex, SoeOptional},
        {sfLedgerEntryType, SoeRequired},
        {sfFlags, SoeRequired},
    };
    return kCommonFields;
}

LedgerFormats::LedgerFormats()
{
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("LEDGER_ENTRY")
#undef LEDGER_ENTRY

#define UNWRAP(...) __VA_ARGS__
#define LEDGER_ENTRY(tag, value, name, rpcName, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());

#include <xrpl/protocol/detail/ledger_entries.macro>

#undef LEDGER_ENTRY
#pragma pop_macro("LEDGER_ENTRY")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
}

LedgerFormats const&
LedgerFormats::getInstance()
{
    static LedgerFormats const kInstance;
    return kInstance;
}

}  // namespace xrpl
