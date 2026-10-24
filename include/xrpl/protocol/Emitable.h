#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/TxSettings.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace xrpl {
/**
 * We have both transaction type emitables and granular type emitables. A transaction type's
 * emitable value is its type plus one; a granular emitable's value, from `emitable.macro`, is
 * always greater than the maximum value of uint16 so the two cannot collide.
 */
class Emitable
{
private:
    Emitable();

    std::unordered_map<std::uint16_t, Emittance> emitableTx_;

    std::unordered_map<std::string, std::uint32_t> granularEmitableMap_;

    std::unordered_map<std::uint32_t, std::string> granularNameMap_;

    std::unordered_map<std::uint32_t, TxType> granularTxTypeMap_;

public:
    static Emitable const&
    getInstance();

    Emitable(Emitable const&) = delete;
    Emitable&
    operator=(Emitable const&) = delete;

    [[nodiscard]] std::optional<std::string>
    getEmitableName(std::uint32_t const value) const;

    [[nodiscard]] std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    [[nodiscard]] std::optional<std::string>
    getGranularName(std::uint32_t value) const;

    [[nodiscard]] std::optional<TxType>
    getGranularTxType(std::uint32_t value) const;

    [[nodiscard]] bool
    isEmitable(std::uint32_t const& emitableValue) const;

    // for tx level emitable, emitable value is equal to tx type plus one
    [[nodiscard]] static std::uint32_t
    txToEmitableType(TxType type);

    // tx type value is emitable value minus one
    [[nodiscard]] static TxType
    emitableToTxType(std::uint32_t value);
};

}  // namespace xrpl
