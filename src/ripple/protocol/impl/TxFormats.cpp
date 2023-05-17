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

#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/jss.h>
#include <dlfcn.h>
#include <iostream>
#include <list>
#include <map>

namespace ripple {

typedef void (*getTxFormatPtr)(std::vector<FakeSOElement>&);
typedef char const* (*getTxNamePtr)();
typedef std::uint16_t (*getTxTypePtr)();
typedef char const* (*getTTNamePtr)();

struct TxFormatsWrapper
{
    char const* name;
    std::uint16_t type;
    std::vector<SOElement> uniqueFields;
    std::initializer_list<SOElement> commonFields;
};

std::map<std::string, std::uint16_t> txTypes{
    {"ttPAYMENT", 0},
    {"ttACCOUNT_SET", 3},
    {"ttREGULAR_KEY_SET", 5},
    {"ttSIGNER_LIST_SET", 12},
    {"ttACCOUNT_DELETE", 21},
    {"ttAMENDMENT", 100},
    {"ttFEE", 101},
    {"ttUNL_MODIFY", 102},
};

std::uint16_t
getTxTypeFromName(std::string name)
{
    if (auto it = txTypes.find(name); it != txTypes.end())
    {
        return it->second;
    }
    assert(false);
    return -1;
}

void
addToTxTypes(std::uint16_t const type, std::string dynamicLib)
{
    void* handle = dlopen(dynamicLib.c_str(), RTLD_LAZY);
    auto const ttName = ((getTTNamePtr)dlsym(handle, "getTTName"))();
    txTypes.insert({ttName, type});
}

const std::initializer_list<SOElement> commonFields
    __attribute__((init_priority(300))){
        {sfTransactionType, soeREQUIRED},
        {sfFlags, soeOPTIONAL},
        {sfSourceTag, soeOPTIONAL},
        {sfAccount, soeREQUIRED},
        {sfSequence, soeREQUIRED},
        {sfPreviousTxnID, soeOPTIONAL},  // emulate027
        {sfLastLedgerSequence, soeOPTIONAL},
        {sfAccountTxnID, soeOPTIONAL},
        {sfFee, soeREQUIRED},
        {sfOperationLimit, soeOPTIONAL},
        {sfMemos, soeOPTIONAL},
        {sfSigningPubKey, soeREQUIRED},
        {sfTxnSignature, soeOPTIONAL},
        {sfSigners, soeOPTIONAL},  // submit_multisigned
    };

std::initializer_list<TxFormatsWrapper> txFormatsList{
    {jss::AccountSet,
     getTxTypeFromName("ttACCOUNT_SET"),
     {
         {sfEmailHash, soeOPTIONAL},
         {sfWalletLocator, soeOPTIONAL},
         {sfWalletSize, soeOPTIONAL},
         {sfMessageKey, soeOPTIONAL},
         {sfDomain, soeOPTIONAL},
         {sfTransferRate, soeOPTIONAL},
         {sfSetFlag, soeOPTIONAL},
         {sfClearFlag, soeOPTIONAL},
     },
     commonFields},
    {jss::SetRegularKey,
     getTxTypeFromName("ttREGULAR_KEY_SET"),
     {
         {sfRegularKey, soeOPTIONAL},
     },
     commonFields},
    {jss::Payment,
     getTxTypeFromName("ttPAYMENT"),
     {
         {sfDestination, soeREQUIRED},
         {sfAmount, soeREQUIRED},
         {sfInvoiceID, soeOPTIONAL},
         {sfDestinationTag, soeOPTIONAL},
     },
     commonFields},
    {jss::EnableAmendment,
     getTxTypeFromName("ttAMENDMENT"),
     {
         {sfLedgerSequence, soeREQUIRED},
         {sfAmendment, soeREQUIRED},
     },
     commonFields},
    {jss::SetFee,
     getTxTypeFromName("ttFEE"),
     {
         {sfLedgerSequence, soeOPTIONAL},
         // Old version uses raw numbers
         {sfBaseFee, soeOPTIONAL},
         {sfReferenceFeeUnits, soeOPTIONAL},
         {sfReserveBase, soeOPTIONAL},
         {sfReserveIncrement, soeOPTIONAL},
         // New version uses Amounts
         {sfBaseFeeDrops, soeOPTIONAL},
         {sfReserveBaseDrops, soeOPTIONAL},
         {sfReserveIncrementDrops, soeOPTIONAL},
     },
     commonFields},
    {jss::UNLModify,
     getTxTypeFromName("ttUNL_MODIFY"),
     {
         {sfUNLModifyDisabling, soeREQUIRED},
         {sfLedgerSequence, soeREQUIRED},
         {sfUNLModifyValidator, soeREQUIRED},
     },
     commonFields},

    // The SignerEntries are optional because a SignerList is deleted by
    // setting the SignerQuorum to zero and omitting SignerEntries.
    {jss::SignerListSet,
     getTxTypeFromName("ttSIGNER_LIST_SET"),
     {
         {sfSignerQuorum, soeREQUIRED},
         {sfSignerEntries, soeOPTIONAL},
     },
     commonFields},
    {jss::AccountDelete,
     getTxTypeFromName("ttACCOUNT_DELETE"),
     {
         {sfDestination, soeREQUIRED},
         {sfDestinationTag, soeOPTIONAL},
     },
     commonFields},
};

std::vector<TxFormatsWrapper> txFormatsList2{};

std::vector<SOElement>
convertToUniqueFields(std::vector<FakeSOElement> txFormat)
{
    std::vector<SOElement> uniqueFields;
    for (auto& param : txFormat)
    {
        uniqueFields.push_back(
            {SField::getField(param.fieldCode), param.style});
    }
    return uniqueFields;
}

void
addToTxFormats(std::uint16_t type, std::string dynamicLib)
{
    void* handle = dlopen(dynamicLib.c_str(), RTLD_LAZY);
    auto const name = ((getTxNamePtr)dlsym(handle, "getTxName"))();
    std::vector<FakeSOElement> txFormat = std::vector<FakeSOElement>{};
    ((getTxFormatPtr)dlsym(handle, "getTxFormat"))(txFormat);
    const std::vector<SOElement>& uniqueFields =
        convertToUniqueFields(txFormat);
    txFormatsList2.push_back({name, type, uniqueFields, commonFields});

    std::cout << "blah " << std::endl;
}

TxFormats::TxFormats()
{
    std::cout << "txFormatsList2.size()" << txFormatsList2.size() << std::endl;
    // Fields shared by all txFormats:
    for (auto& e : txFormatsList)
    {
        std::vector<SOElement> uniqueFields(e.uniqueFields);
        add(e.name, e.type, uniqueFields, e.commonFields);
    }
    std::cout << "txFormatsList2.size() 2" << txFormatsList2.size()
              << std::endl;
    for (auto& e : txFormatsList2)
    {
        std::cout << "Adding transaction format for " << e.name << std::endl;
        add(e.name, e.type, e.uniqueFields, e.commonFields);
        std::cout << "Added transaction format for " << e.name << std::endl;
    }
}

TxFormats const&
TxFormats::getInstance()
{
    static TxFormats const instance;
    return instance;
}

}  // namespace ripple
