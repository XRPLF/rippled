#include <xrpl/protocol/Firewall.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

namespace xrpl {

#pragma push_macro("UNWRAP")
#undef UNWRAP
#pragma push_macro("TRANSACTION")
#undef TRANSACTION

#define UNWRAP(...) __VA_ARGS__
#define TRANSACTION(tag, value, name, settings, ...) \
    case tag:                                        \
        return (TxSettings UNWRAP settings).firewall;

FirewallAction
firewallAction(TxType txType) noexcept
{
    switch (txType)
    {
#include <xrpl/protocol/detail/transactions.macro>

        // Deprecated types
        default:
            return FirewallAction::Allow;
    }
}

#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
#undef UNWRAP
#pragma pop_macro("UNWRAP")

}  // namespace xrpl
