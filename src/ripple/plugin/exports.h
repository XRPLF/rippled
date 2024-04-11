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

#ifndef RIPPLE_PLUGIN_EXPORT_H_INCLUDED
#define RIPPLE_PLUGIN_EXPORT_H_INCLUDED

#include <ripple/app/tx/TxConsequences.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/plugin/invariantChecks.h>
#include <ripple/plugin/ledgerObjects.h>
#include <ripple/plugin/plugin.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TER.h>
#include <vector>

namespace ripple {

template struct Container<STypeExport>;
typedef Container<STypeExport> (*getSTypesPtr)();

template struct Container<SFieldExport>;
typedef Container<SFieldExport> (*getSFieldsPtr)();

template struct Container<LedgerObjectExport>;
typedef Container<LedgerObjectExport> (*getLedgerObjectsPtr)();

template struct Container<TERExport>;
typedef Container<TERExport> (*getTERcodesPtr)();

template struct Container<InvariantCheckExport>;
typedef Container<InvariantCheckExport> (*getInvariantChecksPtr)();

template struct Container<AmendmentExport>;
typedef Container<AmendmentExport> (*getAmendmentsPtr)();

template struct Container<InnerObjectExport>;
typedef Container<InnerObjectExport> (*getInnerObjectFormatsPtr)();

// Transactors

typedef TxConsequences (*makeTxConsequencesPtr)(
    PreflightContext const&);  // TODO: fix
typedef XRPAmount (*calculateBaseFeePtr)(ReadView const& view, STTx const& tx);
typedef NotTEC (*preflightPtr)(PreflightContext const&);
typedef TER (*preclaimPtr)(PreclaimContext const&);
typedef TER (*doApplyPtr)(
    ApplyContext& ctx,
    XRPAmount mPriorBalance,
    XRPAmount mSourceBalance);

// less common ones
typedef NotTEC (
    *checkSeqProxyPtr)(ReadView const& view, STTx const& tx, beast::Journal j);
typedef NotTEC (*checkPriorTxAndLastLedgerPtr)(PreclaimContext const& ctx);
typedef TER (*checkFeePtr)(PreclaimContext const& ctx, XRPAmount baseFee);
typedef NotTEC (*checkSignPtr)(PreclaimContext const& ctx);

struct TransactorExport
{
    char const* txName;
    std::uint16_t txType;
    Container<SOElementExport> txFormat;
    ConsequencesFactoryType consequencesFactoryType;
    makeTxConsequencesPtr makeTxConsequences = nullptr;
    calculateBaseFeePtr calculateBaseFee = nullptr;
    preflightPtr preflight = nullptr;
    preclaimPtr preclaim = nullptr;
    doApplyPtr doApply = nullptr;
    checkSeqProxyPtr checkSeqProxy = nullptr;
    checkPriorTxAndLastLedgerPtr checkPriorTxAndLastLedger = nullptr;
    checkFeePtr checkFee = nullptr;
    checkSignPtr checkSign = nullptr;
};

template struct Container<TransactorExport>;
typedef Container<TransactorExport> (*getTransactorsPtr)();

#define INITIALIZE_PLUGIN()                                               \
    extern "C" void setPluginPointers(                                    \
        std::map<std::uint16_t, PluginTxFormat>* pluginTxFormatPtr,       \
        std::map<std::uint16_t, PluginLedgerFormat>* pluginObjectsMapPtr, \
        std::map<std::uint16_t, PluginInnerObjectFormat>*                 \
            pluginInnerObjectFormatsPtr,                                  \
        std::map<int, SField const*>* knownCodeToFieldPtr,                \
        std::vector<int>* pluginSFieldCodesPtr,                           \
        std::map<int, STypeFunctions>* pluginSTypesPtr,                   \
        std::map<int, parsePluginValuePtr>* pluginLeafParserMapPtr,       \
        std::vector<TERExport>* pluginTERcodes)                           \
    {                                                                     \
        registerTxFormats(pluginTxFormatPtr);                             \
        registerLedgerObjects(pluginObjectsMapPtr);                       \
        registerPluginInnerObjectFormats(pluginInnerObjectFormatsPtr);    \
        registerSFields(knownCodeToFieldPtr, pluginSFieldCodesPtr);       \
        registerSTypes(pluginSTypesPtr);                                  \
        registerLeafTypes(pluginLeafParserMapPtr);                        \
        registerPluginTERs(pluginTERcodes);                               \
    }

template <class... Args>
static uint256
indexHash(std::uint16_t space, Args const&... args)
{
    return sha512Half(space, args...);
}

}  // namespace ripple

#endif
