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

#include <ripple/app/main/PluginSetup.h>
#include <ripple/plugin/exports.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/rpc/handlers/Handlers.h>

namespace ripple {

std::map<std::uint16_t, PluginTxFormat> pluginTxFormats{};
std::map<std::uint16_t, PluginLedgerFormat> pluginObjectsMap{};
std::map<std::uint16_t, PluginInnerObjectFormat> pluginInnerObjectFormats{};
std::vector<int> pluginSFieldCodes{};
std::map<int, STypeFunctions> pluginSTypes{};
std::map<int, parsePluginValuePtr> pluginLeafParserMap{};
std::vector<TERExport> pluginTERcodes{};

void
registerTxFormat(
    std::uint16_t txType,
    char const* txName,
    Container<SOElementExport> txFormat)
{
    auto const strName = std::string(txName);
    if (auto it = pluginTxFormats.find(txType); it != pluginTxFormats.end())
    {
        LogicError(
            std::string("Duplicate key for plugin transactor '") + strName +
            "': already exists");
    }
    pluginTxFormats.insert(
        {txType, {strName, convertToUniqueFields(txFormat)}});
}

void
registerLedgerObject(
    std::uint16_t type,
    char const* name,
    Container<SOElementExport> format)
{
    auto const strName = std::string(name);
    if (auto it = pluginObjectsMap.find(type); it != pluginObjectsMap.end())
    {
        if (it->second.objectName == strName)
            return;
        LogicError(
            std::string("Duplicate key for plugin ledger object '") + strName +
            "': already exists");
    }
    pluginObjectsMap.insert({type, {strName, convertToUniqueFields(format)}});
}

void
registerPluginInnerObjectFormat(InnerObjectExport innerObject)
{
    SField const& field = SField::getField(innerObject.name);
    if (field == sfInvalid)
    {
        throw std::runtime_error(
            "Inner object SField " + std::string(innerObject.name) +
            " does not exist");
    }
    if (field.fieldType != STI_OBJECT)
    {
        throw std::runtime_error(
            "Inner object SField " + std::string(innerObject.name) +
            " is not an STObject");
    }
    auto const strName = std::string(innerObject.name);
    if (auto it = pluginInnerObjectFormats.find(innerObject.code);
        it != pluginInnerObjectFormats.end())
    {
        if (it->second.name == strName)
            return;
        LogicError(
            std::string("Duplicate key for plugin inner object '") + strName +
            "': already exists");
    }
    pluginInnerObjectFormats.insert(
        {innerObject.code,
         {strName, convertToUniqueFields(innerObject.format)}});
}

void
registerSField(SFieldExport const& sfield)
{
    if (SField const& field = SField::getField(sfield.txtName);
        field != sfInvalid)
    {
        throw std::runtime_error(
            "SField " + field.fieldName + " already exists with code " +
            std::to_string(field.getCode()));
    }
    if (SField const& field =
            SField::getField(field_code(sfield.typeId, sfield.fieldValue));
        field != sfInvalid)
    {
        throw std::runtime_error(
            "SField (type " + std::to_string(sfield.typeId) + ", field value " +
            std::to_string(sfield.fieldValue) + ") already exists");
    }
    pluginSFieldCodes.push_back(field_code(sfield.typeId, sfield.fieldValue));
    // NOTE: there might be memory leak issues here
    switch (sfield.typeId)
    {
        case STI_UINT16:
            new SF_UINT16(STI_UINT16, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT32:
            new SF_UINT32(STI_UINT32, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT64:
            new SF_UINT64(STI_UINT64, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT128:
            new SF_UINT128(STI_UINT128, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT256:
            new SF_UINT256(STI_UINT256, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT8:
            new SF_UINT8(STI_UINT8, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT160:
            new SF_UINT160(STI_UINT160, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT96:
            new SF_UINT96(STI_UINT96, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT192:
            new SF_UINT192(STI_UINT192, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT384:
            new SF_UINT384(STI_UINT384, sfield.fieldValue, sfield.txtName);
            break;
        case STI_UINT512:
            new SF_UINT512(STI_UINT512, sfield.fieldValue, sfield.txtName);
            break;
        case STI_AMOUNT:
            new SF_AMOUNT(STI_AMOUNT, sfield.fieldValue, sfield.txtName);
            break;
        case STI_VL:
            new SF_VL(STI_VL, sfield.fieldValue, sfield.txtName);
            break;
        case STI_ACCOUNT:
            new SF_ACCOUNT(STI_ACCOUNT, sfield.fieldValue, sfield.txtName);
            break;
        case STI_OBJECT:
            new SField(STI_OBJECT, sfield.fieldValue, sfield.txtName);
            break;
        case STI_ARRAY:
            new SField(STI_ARRAY, sfield.fieldValue, sfield.txtName);
            break;
        default: {
            if (auto const it = pluginSTypes.find(sfield.typeId);
                it != pluginSTypes.end())
            {
                new SF_PLUGINTYPE(
                    sfield.typeId, sfield.fieldValue, sfield.txtName);
            }
            else
            {
                throw std::runtime_error(
                    "Do not recognize type ID " +
                    std::to_string(sfield.typeId));
            }
        }
    }
}

void
registerSType(STypeFunctions type)
{
    if (auto const it = pluginSTypes.find(type.typeId);
        it != pluginSTypes.end())
    {
        throw std::runtime_error(
            "Type code " + std::to_string(type.typeId) + " already exists");
    }
    for (auto& it : sTypeMap)
    {
        if (it.second == type.typeId)
        {
            throw std::runtime_error(
                "Type code " + std::to_string(type.typeId) + " already exists");
        }
    }
    pluginSTypes.insert({type.typeId, type});
}

void
registerLeafType(int type, parsePluginValuePtr functionPtr)
{
    pluginLeafParserMap.insert({type, functionPtr});
}

void
registerPluginTER(TERExport ter)
{
    for (auto terExport : pluginTERcodes)
    {
        if (terExport.code == ter.code)
        {
            LogicError(
                std::string("Duplicate key for plugin TER code '") +
                std::to_string(ter.code) + "': already exists");
        }
    }
    pluginTERcodes.emplace_back(ter);
}

void
registerPluginPointers()
{
    registerTxFormats(&pluginTxFormats);
    registerLedgerObjects(&pluginObjectsMap);
    registerPluginInnerObjectFormats(&pluginInnerObjectFormats);
    registerSFields(nullptr, &pluginSFieldCodes);
    registerSTypes(&pluginSTypes);
    registerLeafTypes(&pluginLeafParserMap);
    registerPluginTERs(&pluginTERcodes);
}

void
clearPluginPointers()
{
    pluginTxFormats.clear();
    pluginObjectsMap.clear();
    pluginInnerObjectFormats.clear();
    SField::reset();
    pluginSFieldCodes.clear();
    pluginSTypes.clear();
    pluginLeafParserMap.clear();
    pluginTERcodes.clear();
    resetPluginTERcodes();
    clearPluginDeletionBlockers();
}

void
setPluginPointers(LIBTYPE handle)
{
    ((setPluginPointersPtr)LIBFUNC(handle, "setPluginPointers"))(
        &pluginTxFormats,
        &pluginObjectsMap,
        &pluginInnerObjectFormats,
        SField::getKnownCodeToField(),
        &pluginSFieldCodes,
        &pluginSTypes,
        &pluginLeafParserMap,
        &pluginTERcodes);
}

}  // namespace ripple
