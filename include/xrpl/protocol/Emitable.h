#pragma once

#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

#include <unordered_map>

namespace xrpl {

/**
 * Which transaction types a contract may emit, from each type's `emittance` setting in
 * `transactions.macro`.
 */
class Emitable
{
private:
    Emitable();

    std::unordered_map<TxType, Emittance> emittance_;

public:
    static Emitable const&
    getInstance();

    Emitable(Emitable const&) = delete;
    Emitable&
    operator=(Emitable const&) = delete;

    /**
     * Whether a contract may emit a transaction of `txType`; a type with no transaction
     * may not.
     */
    [[nodiscard]] bool
    isEmitable(TxType txType) const;
};

}  // namespace xrpl
