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

#ifndef RIPPLE_APP_MAIN_PLUGINSETUP_H_INCLUDED
#define RIPPLE_APP_MAIN_PLUGINSETUP_H_INCLUDED

#include <ripple/plugin/exports.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/TxFormats.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <map>

#if __linux__ || __APPLE__
#define LIBTYPE void*
#define OPENLIB(libname) dlopen((libname), RTLD_LAZY)
#define LIBFUNC(lib, fn) dlsym((lib), (fn))
#define GETERROR() std::string(strerror(errno))
#elif _WIN32
#define LIBTYPE HINSTANCE
#define OPENLIB(libname) LoadLibraryW(L##libname)
#define LIBFUNC(lib, fn) GetProcAddress((lib), (fn))
#define GETERROR() std::to_string(GetLastError())
#endif
#define CLOSELIB(libname)                                                      \
    static_assert(                                                             \
        false,                                                                 \
        "Unloading the plugin will have unpredictable side effects. Consider " \
        "not using it.")

namespace ripple {

typedef LIBTYPE (*setPluginPointersPtr)(
    std::map<std::uint16_t, PluginTxFormat>* pluginTxFormatPtr,
    std::map<std::uint16_t, PluginLedgerFormat>* pluginObjectsMap,
    std::map<std::uint16_t, PluginInnerObjectFormat>* pluginInnerObjectFormats,
    std::map<int, SField const*>* knownCodeToField,
    std::vector<int>* pluginSFieldCodes,
    std::map<int, STypeFunctions>* pluginSTypes,
    std::map<int, parsePluginValuePtr>* pluginLeafParserMap,
    std::vector<TERExport>* pluginTERcodes);

void
registerTxFormat(
    std::uint16_t txType,
    char const* txName,
    Container<SOElementExport> txFormat);

void
registerLedgerObject(
    std::uint16_t type,
    char const* name,
    Container<SOElementExport> format);

void
registerPluginInnerObjectFormat(InnerObjectExport innerObject);

void
registerSField(SFieldExport const& sfield);

void
registerSType(STypeFunctions type);

void
registerLeafType(int type, parsePluginValuePtr functionPtr);

void
registerPluginTER(TERExport ter);

void
registerPluginPointers();

void
clearPluginPointers();

void
setPluginPointers(LIBTYPE handle);

}  // namespace ripple

#endif
