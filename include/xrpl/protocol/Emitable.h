//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Emitable to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this emitable notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_EMITABLE_H_INCLUDED
#define RIPPLE_PROTOCOL_EMITABLE_H_INCLUDED

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace ripple {
/**
 * We have both transaction type emitables and granular type emitables.
 * Since we will reuse the TransactionFormats to parse the Transaction
 * Emitables, only the GranularEmitableType is defined here. To prevent
 * conflicts with TxType, the GranularEmitableType is always set to a value
 * greater than the maximum value of uint16.
 */
enum GranularEmitableType : std::uint32_t {
#pragma push_macro("EMITABLE")
#undef EMITABLE

#define EMITABLE(type, txType, value) type = value,

#include <xrpl/protocol/detail/emitable.macro>

#undef EMITABLE
#pragma pop_macro("EMITABLE")
};

enum Emittance { emitable, notEmitable };

class Emitable
{
private:
    Emitable();

    std::unordered_map<std::uint16_t, Emittance> emitableTx_;

    std::unordered_map<std::string, GranularEmitableType>
        granularEmitableMap_;

    std::unordered_map<GranularEmitableType, std::string> granularNameMap_;

    std::unordered_map<GranularEmitableType, TxType> granularTxTypeMap_;

public:
    static Emitable const&
    getInstance();

    Emitable(Emitable const&) = delete;
    Emitable&
    operator=(Emitable const&) = delete;

    std::optional<std::string>
    getEmitableName(std::uint32_t const value) const;

    std::optional<std::uint32_t>
    getGranularValue(std::string const& name) const;

    std::optional<std::string>
    getGranularName(GranularEmitableType const& value) const;

    std::optional<TxType>
    getGranularTxType(GranularEmitableType const& gpType) const;

    bool
    isEmitable(std::uint32_t const& emitableValue) const;

    // for tx level emitable, emitable value is equal to tx type plus one
    uint32_t
    txToEmitableType(TxType const& type) const;

    // tx type value is emitable value minus one
    TxType
    emitableToTxType(uint32_t const& value) const;
};

}  // namespace ripple

#endif
