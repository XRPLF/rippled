//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

namespace ripple {

template <class UnsignedInteger>
inline bool is_xrp (UnsignedInteger const& value)
{
    return value.isZero();
}

template <class UnsignedInteger>
inline bool is_not_xrp (UnsignedInteger const& value)
{
    return ! is_xrp (value);
}

inline uint160 const& xrp_issuer ()
{
    return ACCOUNT_XRP;
}

inline uint160 const& xrp_currency ()
{
    return CURRENCY_XRP;
}

inline uint160 const& neutral_issuer ()
{
    return ACCOUNT_ONE;
}

Json::Value doBookOffers (RPC::Context& context)
{
    context.lock_.unlock ();

    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (getApp().getJobQueue ().getJobCountGE (jtCLIENT) > 200)
        return rpcError (rpcTOO_BUSY);

    Ledger::pointer lpLedger;
    Json::Value jvResult (RPC::lookupLedger (context.params_, lpLedger, context.netOps_));

    if (!lpLedger)
        return jvResult;

    if (!context.params_.isMember ("taker_pays"))
        return RPC::missing_field_error ("taker_pays");

    if (!context.params_.isMember ("taker_gets"))
        return RPC::missing_field_error ("taker_gets");

    if (!context.params_["taker_pays"].isObject ())
        return RPC::object_field_error ("taker_pays");

    if (!context.params_["taker_gets"].isObject ())
        return RPC::object_field_error ("taker_gets");

    Json::Value const& taker_pays (context.params_["taker_pays"]);

    if (!taker_pays.isMember ("currency"))
        return RPC::missing_field_error ("taker_pays.currency");

    if (! taker_pays ["currency"].isString ())
        return RPC::expected_field_error ("taker_pays.currency", "string");

    Json::Value const& taker_gets = context.params_["taker_gets"];

    if (! taker_gets.isMember ("currency"))
        return RPC::missing_field_error ("taker_gets.currency");

    if (! taker_gets ["currency"].isString ())
        return RPC::expected_field_error ("taker_gets.currency", "string");

    uint160 pay_currency;

    if (! STAmount::currencyFromString (
        pay_currency, taker_pays ["currency"].asString ()))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad taker_pays currency.";
        return RPC::make_error (rpcSRC_CUR_MALFORMED,
            "Invalid field 'taker_pays.currency', bad currency.");
    }

    uint160 get_currency;

    if (! STAmount::currencyFromString (
        get_currency, taker_gets ["currency"].asString ()))
    {
        WriteLog (lsINFO, RPCHandler) << "Bad taker_gets currency.";
        return RPC::make_error (rpcDST_AMT_MALFORMED,
            "Invalid field 'taker_gets.currency', bad currency.");
    }

    uint160 pay_issuer;

    if (taker_pays.isMember ("issuer"))
    {
        if (! taker_pays ["issuer"].isString())
            return RPC::expected_field_error ("taker_pays.issuer", "string");

        if (! STAmount::issuerFromString (
            pay_issuer, taker_pays ["issuer"].asString ()))
            return RPC::make_error (rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer.");

        if (pay_issuer == neutral_issuer ())
            return RPC::make_error (rpcSRC_ISR_MALFORMED,
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
    }
    else
    {
        pay_issuer = xrp_issuer ();
    }

    if (is_xrp (pay_currency) && ! is_xrp (pay_issuer))
        return RPC::make_error (rpcSRC_ISR_MALFORMED,
            "Unneeded field 'taker_pays.issuer' for XRP currency specification.");

    if (is_not_xrp (pay_currency) && is_xrp (pay_issuer))
        return RPC::make_error (rpcSRC_ISR_MALFORMED,
            "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");

    uint160 get_issuer;

    if (taker_gets.isMember ("issuer"))
    {
        if (! taker_gets ["issuer"].isString())
            return RPC::expected_field_error ("taker_gets.issuer", "string");

        if (! STAmount::issuerFromString (
            get_issuer, taker_gets ["issuer"].asString ()))
            return RPC::make_error (rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer.");

        if (get_issuer == neutral_issuer ())
            return RPC::make_error (rpcDST_ISR_MALFORMED,
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
    }
    else
    {
        get_issuer = xrp_issuer ();
    }


    if (is_xrp (get_currency) && ! is_xrp (get_issuer))
        return RPC::make_error (rpcDST_ISR_MALFORMED,
            "Unneeded field 'taker_gets.issuer' for XRP currency specification.");

    if (is_not_xrp (get_currency) && is_xrp (get_issuer))
        return RPC::make_error (rpcDST_ISR_MALFORMED,
            "Invalid field 'taker_gets.issuer', expected non-XRP issuer.");

    RippleAddress raTakerID;

    if (context.params_.isMember ("taker"))
    {
        if (! context.params_ ["taker"].isString ())
            return RPC::expected_field_error ("taker", "string");

        if (! raTakerID.setAccountID (context.params_ ["taker"].asString ()))
            return RPC::invalid_field_error ("taker");
    }
    else
    {
        raTakerID.setAccountID (ACCOUNT_ONE);
    }

    if (pay_currency == get_currency && pay_issuer == get_issuer)
    {
        WriteLog (lsINFO, RPCHandler) << "taker_gets same as taker_pays.";
        return RPC::make_error (rpcBAD_MARKET);
    }

    if (context.params_.isMember ("limit") && ! context.params_ ["limit"].isIntegral())
        return RPC::expected_field_error (
        "limit", "integer");

    unsigned int const iLimit (context.params_.isMember ("limit")
        ? context.params_ ["limit"].asUInt ()
        : 0);

    bool const bProof (context.params_.isMember ("proof"));

    Json::Value const jvMarker (context.params_.isMember ("marker")
        ? context.params_["marker"]
        : Json::Value (Json::nullValue));

    context.netOps_.getBookPage (lpLedger, pay_currency, pay_issuer,
        get_currency, get_issuer, raTakerID.getAccountID (),
            bProof, iLimit, jvMarker, jvResult);

    context.loadType_ = Resource::feeMediumBurdenRPC;

    return jvResult;
}

} // ripple
