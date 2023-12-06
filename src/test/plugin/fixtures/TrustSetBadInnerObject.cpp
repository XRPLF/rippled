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

#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/ledger/View.h>
#include <ripple/plugin/createSFields.h>
#include <ripple/plugin/exports.h>
#include <ripple/plugin/reset.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <string>

using namespace ripple;

SField const&
sfFakeArray()
{
    return newUntypedSField<STArray>(30, "FakeArray");
}

SField const&
sfFakeElement()
{
    return newUntypedSField<STObject>(17, "FakeElement");
}

NotTEC
preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
doApply(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance)
{
    return tesSUCCESS;
}

extern "C" Container<TransactorExport>
getTransactors()
{
    static SOElementExport format[] = {
        {sfLimitAmount.getCode(), soeOPTIONAL},
        {sfQualityIn.getCode(), soeOPTIONAL},
        {sfQualityOut.getCode(), soeOPTIONAL},
        {sfFakeArray().getCode(), soeOPTIONAL},
        {sfFakeElement().getCode(), soeOPTIONAL},
    };
    SOElementExport* formatPtr = format;
    static TransactorExport list[] = {
        {"TrustSet2",
         50,
         {formatPtr, 5},
         ConsequencesFactoryType::Normal,
         NULL,
         NULL,
         preflight,
         preclaim,
         doApply,
         NULL,
         NULL,
         NULL,
         NULL}};
    TransactorExport* ptr = list;
    return {ptr, 1};
}

extern "C" Container<SFieldExport>
getSFields()
{
    auto const& fakeArray = sfFakeArray();
    auto const& fakeElement = sfFakeElement();
    static SFieldExport sfields[] = {
        {fakeArray.fieldType,
         fakeArray.fieldValue,
         fakeArray.fieldName.c_str()},
        {fakeElement.fieldType,
         fakeElement.fieldValue,
         fakeElement.fieldName.c_str()},
    };
    SFieldExport* ptr = sfields;
    return {ptr, 2};
}

extern "C" Container<InnerObjectExport>
getInnerObjectFormats()
{
    static SOElementExport format[] = {
        {sfAccount.getCode(), soeREQUIRED},
    };
    auto const& fakeArray = sfFakeArray();
    SOElementExport* formatPtr = format;
    static InnerObjectExport list[] = {{
        fakeArray.getCode(),
        fakeArray.jsonName.c_str(),
        {formatPtr, 1},
    }};
    InnerObjectExport* ptr = list;
    return {ptr, 1};
}
extern "C" Container<AmendmentExport>
getAmendments()
{
    AmendmentExport const amendment = {
        "featurePluginTest",
        true,
        VoteBehavior::DefaultNo,
    };
    static AmendmentExport list[] = {amendment};
    AmendmentExport* ptr = list;
    return {ptr, 1};
}
