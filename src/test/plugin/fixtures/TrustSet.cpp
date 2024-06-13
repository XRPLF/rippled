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
#include <ripple/plugin/macros.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/InnerObjectFormats.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <string>

using namespace ripple;

const int STI_UINT32_2 = 30;

Buffer
parseLeafTypeNew(
    SField const& field,
    std::string const& json_name,
    std::string const& fieldName,
    SField const* name,
    Json::Value const& value,
    Json::Value& error)
{
    // copied from parseLeafType<STUInt32>
    std::optional<uint32_t> ret;
    try
    {
        if (value.isString())
        {
            ret = beast::lexicalCastThrow<std::uint32_t>(value.asString());
        }
        else if (value.isInt())
        {
            ret = to_unsigned<std::uint32_t>(value.asInt());
        }
        else if (value.isUInt())
        {
            ret = safe_cast<std::uint32_t>(value.asUInt());
        }
        else
        {
            error = bad_type(json_name, fieldName);
        }
        if (ret.has_value())
        {
            auto const val = ret.value();
            return Buffer(&val, sizeof val);
        }
        return Buffer();
    }
    catch (std::exception const&)
    {
        error = invalid_data(json_name, fieldName);
        return Buffer();
    }
}

std::uint32_t
bufferToUInt32(Buffer const& buf)
{
    std::uint32_t result = 0;
    int shift = 0;
    for (int i = 0; i < buf.size(); i++)
    {
        auto const byte = *(buf.data() + i);
        result |= static_cast<std::uint32_t>(byte) << shift;
        shift += 8;
    }

    return result;
}

std::string
toString(int typeId, Buffer const& buf)
{
    uint32_t val = bufferToUInt32(buf);
    return std::to_string(val);
}

void
toSerializer(int typeId, Buffer const& buf, Serializer& s)
{
    uint32_t val = bufferToUInt32(buf);
    s.add32(val);
}

Buffer
fromSerialIter(int typeId, SerialIter& st)
{
    uint32_t val = st.get32();
    return Buffer(&val, sizeof val);
}

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

SF_PLUGINTYPE const&
sfQualityIn2()
{
    return constructCustomSField(STI_UINT32_2, 1, "QualityIn2");
}

EXPORT_AMENDMENT_TEST(featurePluginTest, true, VoteBehavior::DefaultNo);

const int temINVALID_FLAG2 = -210;

NotTEC
preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featurePluginTest))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (uTxFlags & tfTrustSetMask)
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid flags set.";
        return NotTEC::fromInt(temINVALID_FLAG2);
    }

    STAmount const saLimitAmount(tx.getFieldAmount(sfLimitAmount));

    if (!isLegalNet(saLimitAmount))
        return temBAD_AMOUNT;

    if (saLimitAmount.native())
    {
        JLOG(j.trace()) << "Malformed transaction: specifies native limit "
                        << saLimitAmount.getFullText();
        return temBAD_LIMIT;
    }

    if (badCurrency() == saLimitAmount.getCurrency())
    {
        JLOG(j.trace()) << "Malformed transaction: specifies XRP as IOU";
        return temBAD_CURRENCY;
    }

    if (saLimitAmount < beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: Negative credit limit.";
        return temBAD_LIMIT;
    }

    // Check if destination makes sense.
    auto const& issuer = saLimitAmount.getIssuer();

    if (!issuer || issuer == noAccount())
    {
        JLOG(j.trace()) << "Malformed transaction: no destination account.";
        return temDST_NEEDED;
    }

    return preflight2(ctx);
}

TER
preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth);

    if (bSetAuth && !(sle->getFieldU32(sfFlags) & lsfRequireAuth))
    {
        JLOG(ctx.j.trace()) << "Retry: Auth not required.";
        return tefNO_AUTH_REQUIRED;
    }

    auto const saLimitAmount = ctx.tx[sfLimitAmount];

    auto const currency = saLimitAmount.getCurrency();
    auto const uDstAccountID = saLimitAmount.getIssuer();

    if (ctx.view.rules().enabled(fixTrustLinesToSelf))
    {
        if (id == uDstAccountID)
            return temDST_IS_SRC;
    }
    else
    {
        if (id == uDstAccountID)
        {
            // Prevent trustline to self from being created,
            // unless one has somehow already been created
            // (in which case doApply will clean it up).
            auto const sleDelete =
                ctx.view.read(keylet::line(id, uDstAccountID, currency));

            if (!sleDelete)
            {
                JLOG(ctx.j.trace())
                    << "Malformed transaction: Can not extend credit to self.";
                return temDST_IS_SRC;
            }
        }
    }

    // If the destination has opted to disallow incoming trustlines
    // then honour that flag
    if (ctx.view.rules().enabled(featureDisallowIncoming))
    {
        auto const sleDst = ctx.view.read(keylet::account(uDstAccountID));

        if (!sleDst)
            return tecNO_DST;

        auto const dstFlags = sleDst->getFlags();
        if (dstFlags & lsfDisallowIncomingTrustline)
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
doApply(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance)
{
    TER terResult = tesSUCCESS;
    auto const account = ctx.tx.getAccountID(sfAccount);

    STAmount const saLimitAmount(ctx.tx.getFieldAmount(sfLimitAmount));
    bool const bQualityIn(ctx.tx.isFieldPresent(sfQualityIn2()));
    bool const bQualityOut(ctx.tx.isFieldPresent(sfQualityOut));

    Currency const currency(saLimitAmount.getCurrency());
    AccountID uDstAccountID(saLimitAmount.getIssuer());

    // true, iff current is high account.
    bool const bHigh = account > uDstAccountID;

    auto const sle = ctx.view().peek(keylet::account(account));
    if (!sle)
        return tefINTERNAL;

    std::uint32_t const uOwnerCount = sle->getFieldU32(sfOwnerCount);

    // The reserve that is required to create the line. Note
    // that although the reserve increases with every item
    // an account owns, in the case of trust lines we only
    // *enforce* a reserve if the user owns more than two
    // items.
    //
    // We do this because being able to exchange currencies,
    // which needs trust lines, is a powerful Ripple feature.
    // So we want to make it easy for a gateway to fund the
    // accounts of its users without fear of being tricked.
    //
    // Without this logic, a gateway that wanted to have a
    // new user use its services, would have to give that
    // user enough XRP to cover not only the account reserve
    // but the incremental reserve for the trust line as
    // well. A person with no intention of using the gateway
    // could use the extra XRP for their own purposes.

    XRPAmount const reserveCreate(
        (uOwnerCount < 2) ? XRPAmount(beast::zero)
                          : ctx.view().fees().accountReserve(uOwnerCount + 1));

    // equivalent to
    // std::uint32_t uQualityIn(bQualityIn ? ctx_.tx.getFieldU32(sfQualityIn) :
    // 0);
    std::uint32_t uQualityIn(
        bQualityIn ? static_cast<uint32_t>(std::stoul(
                         ctx.tx.getFieldPluginType(sfQualityIn2()).getText()))
                   : 0);
    std::uint32_t uQualityOut(
        bQualityOut ? ctx.tx.getFieldU32(sfQualityOut) : 0);

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth);
    bool const bSetNoRipple = (uTxFlags & tfSetNoRipple);
    bool const bClearNoRipple = (uTxFlags & tfClearNoRipple);
    bool const bSetFreeze = (uTxFlags & tfSetFreeze);
    bool const bClearFreeze = (uTxFlags & tfClearFreeze);

    auto viewJ = ctx.app.journal("View");

    // Trust lines to self are impossible but because of the old bug there are
    // two on 19-02-2022. This code was here to allow those trust lines to be
    // deleted. The fixTrustLinesToSelf fix amendment will remove them when it
    // enables so this code will no longer be needed.
    if (!ctx.view().rules().enabled(fixTrustLinesToSelf) &&
        account == uDstAccountID)
    {
        return trustDelete(
            ctx.view(),
            ctx.view().peek(keylet::line(account, uDstAccountID, currency)),
            account,
            uDstAccountID,
            viewJ);
    }

    SLE::pointer sleDst = ctx.view().peek(keylet::account(uDstAccountID));

    if (!sleDst)
    {
        JLOG(ctx.journal.trace())
            << "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.setIssuer(account);

    SLE::pointer sleRippleState =
        ctx.view().peek(keylet::line(account, uDstAccountID, currency));

    if (sleRippleState)
    {
        STAmount saLowBalance;
        STAmount saLowLimit;
        STAmount saHighBalance;
        STAmount saHighLimit;
        std::uint32_t uLowQualityIn;
        std::uint32_t uLowQualityOut;
        std::uint32_t uHighQualityIn;
        std::uint32_t uHighQualityOut;
        auto const& uLowAccountID = !bHigh ? account : uDstAccountID;
        auto const& uHighAccountID = bHigh ? account : uDstAccountID;
        SLE::ref sleLowAccount = !bHigh ? sle : sleDst;
        SLE::ref sleHighAccount = bHigh ? sle : sleDst;

        //
        // Balances
        //

        saLowBalance = sleRippleState->getFieldAmount(sfBalance);
        saHighBalance = -saLowBalance;

        //
        // Limits
        //

        sleRippleState->setFieldAmount(
            !bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

        saLowLimit =
            !bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfLowLimit);
        saHighLimit =
            bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfHighLimit);

        //
        // Quality in
        //

        if (!bQualityIn)
        {
            // Not setting. Just get it.

            uLowQualityIn = sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else if (uQualityIn)
        {
            // Setting.

            sleRippleState->setFieldU32(
                !bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

            uLowQualityIn = !bHigh
                ? uQualityIn
                : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh
                ? uQualityIn
                : sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(
                !bHigh ? sfLowQualityIn : sfHighQualityIn);

            uLowQualityIn =
                !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn =
                bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
        }

        if (QUALITY_ONE == uLowQualityIn)
            uLowQualityIn = 0;

        if (QUALITY_ONE == uHighQualityIn)
            uHighQualityIn = 0;

        //
        // Quality out
        //

        if (!bQualityOut)
        {
            // Not setting. Just get it.

            uLowQualityOut = sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else if (uQualityOut)
        {
            // Setting.

            sleRippleState->setFieldU32(
                !bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

            uLowQualityOut = !bHigh
                ? uQualityOut
                : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh
                ? uQualityOut
                : sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent(
                !bHigh ? sfLowQualityOut : sfHighQualityOut);

            uLowQualityOut =
                !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut =
                bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
        }

        std::uint32_t const uFlagsIn(sleRippleState->getFieldU32(sfFlags));
        std::uint32_t uFlagsOut(uFlagsIn);

        if (bSetNoRipple && !bClearNoRipple)
        {
            if ((bHigh ? saHighBalance : saLowBalance) >= beast::zero)
                uFlagsOut |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);

            else if (ctx.view().rules().enabled(fix1578))
                // Cannot set noRipple on a negative balance.
                return tecNO_PERMISSION;
        }
        else if (bClearNoRipple && !bSetNoRipple)
        {
            uFlagsOut &= ~(bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }

        if (bSetFreeze && !bClearFreeze && !sle->isFlag(lsfNoFreeze))
        {
            uFlagsOut |= (bHigh ? lsfHighFreeze : lsfLowFreeze);
        }
        else if (bClearFreeze && !bSetFreeze)
        {
            uFlagsOut &= ~(bHigh ? lsfHighFreeze : lsfLowFreeze);
        }

        if (QUALITY_ONE == uLowQualityOut)
            uLowQualityOut = 0;

        if (QUALITY_ONE == uHighQualityOut)
            uHighQualityOut = 0;

        bool const bLowDefRipple = sleLowAccount->getFlags() & lsfDefaultRipple;
        bool const bHighDefRipple =
            sleHighAccount->getFlags() & lsfDefaultRipple;

        bool const bLowReserveSet = uLowQualityIn || uLowQualityOut ||
            ((uFlagsOut & lsfLowNoRipple) == 0) != bLowDefRipple ||
            (uFlagsOut & lsfLowFreeze) || saLowLimit ||
            saLowBalance > beast::zero;
        bool const bLowReserveClear = !bLowReserveSet;

        bool const bHighReserveSet = uHighQualityIn || uHighQualityOut ||
            ((uFlagsOut & lsfHighNoRipple) == 0) != bHighDefRipple ||
            (uFlagsOut & lsfHighFreeze) || saHighLimit ||
            saHighBalance > beast::zero;
        bool const bHighReserveClear = !bHighReserveSet;

        bool const bDefault = bLowReserveClear && bHighReserveClear;

        bool const bLowReserved = (uFlagsIn & lsfLowReserve);
        bool const bHighReserved = (uFlagsIn & lsfHighReserve);

        bool bReserveIncrease = false;

        if (bSetAuth)
        {
            uFlagsOut |= (bHigh ? lsfHighAuth : lsfLowAuth);
        }

        if (bLowReserveSet && !bLowReserved)
        {
            // Set reserve for low account.
            adjustOwnerCount(ctx.view(), sleLowAccount, 1, viewJ);
            uFlagsOut |= lsfLowReserve;

            if (!bHigh)
                bReserveIncrease = true;
        }

        if (bLowReserveClear && bLowReserved)
        {
            // Clear reserve for low account.
            adjustOwnerCount(ctx.view(), sleLowAccount, -1, viewJ);
            uFlagsOut &= ~lsfLowReserve;
        }

        if (bHighReserveSet && !bHighReserved)
        {
            // Set reserve for high account.
            adjustOwnerCount(ctx.view(), sleHighAccount, 1, viewJ);
            uFlagsOut |= lsfHighReserve;

            if (bHigh)
                bReserveIncrease = true;
        }

        if (bHighReserveClear && bHighReserved)
        {
            // Clear reserve for high account.
            adjustOwnerCount(ctx.view(), sleHighAccount, -1, viewJ);
            uFlagsOut &= ~lsfHighReserve;
        }

        if (uFlagsIn != uFlagsOut)
            sleRippleState->setFieldU32(sfFlags, uFlagsOut);

        if (bDefault || badCurrency() == currency)
        {
            // Delete.

            terResult = trustDelete(
                ctx.view(),
                sleRippleState,
                uLowAccountID,
                uHighAccountID,
                viewJ);
        }
        // Reserve is not scaled by load.
        else if (bReserveIncrease && mPriorBalance < reserveCreate)
        {
            JLOG(ctx.journal.trace())
                << "Delay transaction: Insufficent reserve to add trust line.";

            // Another transaction could provide XRP to the account and then
            // this transaction would succeed.
            terResult = tecINSUF_RESERVE_LINE;
        }
        else
        {
            ctx.view().update(sleRippleState);

            JLOG(ctx.journal.trace()) << "Modify ripple line";
        }
    }
    // Line does not exist.
    else if (
        !saLimitAmount &&                  // Setting default limit.
        (!bQualityIn || !uQualityIn) &&    // Not setting quality in or setting
                                           // default quality in.
        (!bQualityOut || !uQualityOut) &&  // Not setting quality out or setting
                                           // default quality out.
        (!bSetAuth))
    {
        JLOG(ctx.journal.trace())
            << "Redundant: Setting non-existent ripple line to defaults.";
        return tecNO_LINE_REDUNDANT;
    }
    else if (mPriorBalance < reserveCreate)  // Reserve is not scaled by load.
    {
        JLOG(ctx.journal.trace()) << "Delay transaction: Line does not exist. "
                                     "Insufficent reserve to create line.";

        // Another transaction could create the account and then this
        // transaction would succeed.
        terResult = tecNO_LINE_INSUF_RESERVE;
    }
    else
    {
        // Zero balance in currency.
        STAmount saBalance({currency, noAccount()});

        auto const k = keylet::line(account, uDstAccountID, currency);

        JLOG(ctx.journal.trace())
            << "doTrustSet: Creating ripple line: " << to_string(k.key);

        // Create a new ripple line.
        terResult = trustCreate(
            ctx.view(),
            bHigh,
            account,
            uDstAccountID,
            k.key,
            sle,
            bSetAuth,
            bSetNoRipple && !bClearNoRipple,
            bSetFreeze && !bClearFreeze,
            saBalance,
            saLimitAllow,  // Limit for who is being charged.
            uQualityIn,
            uQualityOut,
            viewJ);
    }

    return terResult;
}

extern "C" Container<TransactorExport>
getTransactors()
{
    static SOElementExport format[] = {
        {sfLimitAmount.getCode(), soeOPTIONAL},
        {field_code(STI_UINT32_2, 1), soeOPTIONAL},
        {sfQualityOut.getCode(), soeOPTIONAL},
        {sfFakeArray().getCode(), soeOPTIONAL},
        {sfFakeElement().getCode(), soeOPTIONAL},
    };
    SOElementExport* formatPtr = format;
    static TransactorExport list[] = {
        {"TrustSet2",
         61,
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

EXPORT_STYPES({
    STI_UINT32_2,
    "STI_UINT32_2",
    parseLeafTypeNew,
    toString,
    NULL,
    toSerializer,
    fromSerialIter,
});

EXPORT_SFIELDS(
    {STI_UINT32_2, 1, "QualityIn2"},
    {STI_ARRAY, 30, "FakeArray"},
    {STI_OBJECT, 17, "FakeElement"}, );

extern "C" Container<InnerObjectExport>
getInnerObjectFormats()
{
    static SOElementExport format[] = {
        {sfAccount.getCode(), soeREQUIRED},
    };
    auto const& fakeElement = sfFakeElement();
    SOElementExport* formatPtr = format;
    static InnerObjectExport list[] = {{
        static_cast<uint16_t>(fakeElement.getCode()),
        fakeElement.jsonName.c_str(),
        {formatPtr, 1},
    }};
    InnerObjectExport* ptr = list;
    return {ptr, 1};
}

EXPORT_TER({temINVALID_FLAG2, "temINVALID_FLAG2", "Test code"});

INITIALIZE_PLUGIN()
