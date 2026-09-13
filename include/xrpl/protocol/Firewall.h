#pragma once

#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

namespace xrpl {

/**
 * How an account's firewall treats a transaction of the given type.
 *
 * The classification is declared per transaction in transactions.macro. A type
 * the switch does not name, which means a deprecated one, is allowed.
 */
[[nodiscard]] FirewallAction
firewallAction(TxType txType) noexcept;

}  // namespace xrpl
