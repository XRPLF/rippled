//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef RIPPLE_PLUGIN_MACROS_H_INCLUDED
#define RIPPLE_PLUGIN_MACROS_H_INCLUDED

#include <ripple/plugin/exports.h>

namespace ripple {

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

#define EXPORT_AMENDMENT(name, supported, vote)           \
    static uint256 name{};                                \
    extern "C" Container<AmendmentExport> getAmendments() \
    {                                                     \
        AmendmentExport const amendment = {               \
            #name,                                        \
            supported,                                    \
            vote,                                         \
        };                                                \
        name = registerPluginAmendment(amendment);        \
        static AmendmentExport list[] = {amendment};      \
        AmendmentExport* ptr = list;                      \
        return {ptr, 1};                                  \
    }

#define EXPORT_AMENDMENT_TEST(name, supported, vote)      \
    static uint256 name{};                                \
    extern "C" Container<AmendmentExport> getAmendments() \
    {                                                     \
        reinitialize();                                   \
        AmendmentExport const amendment = {               \
            #name,                                        \
            supported,                                    \
            vote,                                         \
        };                                                \
        name = registerPluginAmendment(amendment);        \
        static AmendmentExport list[] = {amendment};      \
        AmendmentExport* ptr = list;                      \
        return {ptr, 1};                                  \
    }

#define EXPORT_STYPES(...)                                  \
    extern "C" Container<STypeExport> getSTypes()           \
    {                                                       \
        static STypeExport exports[] = {__VA_ARGS__};       \
        STypeExport* ptr = exports;                         \
        return {ptr, sizeof(exports) / sizeof(exports[0])}; \
    }

#define EXPORT_SFIELDS(...)                                 \
    extern "C" Container<SFieldExport> getSFields()         \
    {                                                       \
        static SFieldExport exports[] = {__VA_ARGS__};      \
        SFieldExport* ptr = exports;                        \
        return {ptr, sizeof(exports) / sizeof(exports[0])}; \
    }

#define EXPORT_TER(...)                             \
    extern "C" Container<TERExport> getTERcodes()   \
    {                                               \
        static TERExport exports[] = {__VA_ARGS__}; \
        TERExport* ptr = exports;                   \
        return {ptr, 1};                            \
    }

}  // namespace ripple

#endif
