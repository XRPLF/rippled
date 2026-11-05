#include <xrpl/protocol/Emitable.h>

#include <xrpl/protocol/Feature.h>  // IWYU pragma: keep
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

namespace xrpl {

Emitable::Emitable()
{
    emittance_ = {
#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, settings, ...) {tag, (TxSettings UNWRAP settings).emittance},
#include <xrpl/protocol/detail/transactions.macro>

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")
    };
}

Emitable const&
Emitable::getInstance()
{
    static Emitable const kInstance;
    return kInstance;
}

bool
Emitable::isEmitable(TxType txType) const
{
    auto const it = emittance_.find(txType);
    return it != emittance_.end() && it->second != Emittance::NotEmitable;
}

}  // namespace xrpl
