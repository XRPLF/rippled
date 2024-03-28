//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PLUGIN_INVARIANTCHECKS_H_INCLUDED
#define RIPPLE_PLUGIN_INVARIANTCHECKS_H_INCLUDED

#include <ripple/basics/XRPAmount.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/st.h>

namespace ripple {

class ReadView;

typedef void (*visitEntryPtr)(
    void* id,
    bool isDelete,
    std::shared_ptr<STLedgerEntry const> const& before,
    std::shared_ptr<STLedgerEntry const> const& after);
typedef bool (*finalizePtr)(
    void* id,
    STTx const& tx,
    TER const result,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j);

struct InvariantCheckExport
{
    visitEntryPtr visitEntry;
    finalizePtr finalize;
};

#define EXPORT_INVARIANT_CHECKS(...)                                \
    /* This function is in the macro instead of in the header file  \
     * because it allows the list to be static                      \
     */                                                             \
    template <typename... Args>                                     \
    Container<InvariantCheckExport> exportInvariantChecks()         \
    {                                                               \
        (Args::checks.clear(), ...);                                \
        static InvariantCheckExport list[] = {                      \
            {Args::visitEntryExport, Args::finalizeExport}...};     \
        InvariantCheckExport* ptr = list;                           \
        return {ptr, sizeof...(Args)};                              \
    }                                                               \
                                                                    \
    extern "C" Container<InvariantCheckExport> getInvariantChecks() \
    {                                                               \
        return exportInvariantChecks<__VA_ARGS__>();                \
    }

}  // namespace ripple

#endif
