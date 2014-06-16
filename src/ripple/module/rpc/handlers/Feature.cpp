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

static void textTime (std::string& text, int& seconds, const char* unitName, int unitVal)
{
    int i = seconds / unitVal;

    if (i == 0)
        return;

    seconds -= unitVal * i;

    if (!text.empty ())
        text += ", ";

    text += beast::lexicalCastThrow <std::string> (i);
    text += " ";
    text += unitName;

    if (i > 1)
        text += "s";
}

Json::Value doFeature (RPC::Context& context)
{
    if (!context.params_.isMember ("feature"))
    {
        Json::Value jvReply = Json::objectValue;
        jvReply["features"] = getApp().getAmendmentTable ().getJson(0);
        return jvReply;
    }

    uint256 uFeature = getApp().getAmendmentTable ().get(context.params_["feature"].asString());

    if (uFeature.isZero ())
    {
        uFeature.SetHex (context.params_["feature"].asString ());

        if (uFeature.isZero ())
            return rpcError (rpcBAD_FEATURE);
    }

    if (!context.params_.isMember ("vote"))
        return getApp().getAmendmentTable ().getJson(uFeature);

    // WRITEME
    return rpcError (rpcNOT_SUPPORTED);
}


} // ripple
