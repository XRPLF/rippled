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

// Get state nodes from a ledger
//   Inputs:
//     limit:        integer, maximum number of entries
//     marker:       opaque, resume point
//     binary:       boolean, format
//   Outputs:
//     ledger_hash:  chosen ledger's hash
//     ledger_index: chosen ledger's index
//     state:        array of state nodes
//     marker:       resume point, if any
Json::Value doLedgerData (RPC::Context& context)
{
    context.lock_.unlock ();

    int const BINARY_PAGE_LENGTH = 256;
    int const JSON_PAGE_LENGTH = 2048;

    Ledger::pointer lpLedger;

    Json::Value jvResult = RPC::lookupLedger (context.params_, lpLedger, context.netOps_);
    if (!lpLedger)
        return jvResult;

    uint256 resumePoint;
    if (context.params_.isMember ("marker"))
    {
        Json::Value const& jMarker = context.params_["marker"];
        if (!jMarker.isString ())
            return RPC::expected_field_error ("marker", "valid");
        if (!resumePoint.SetHex (jMarker.asString ()))
            return RPC::expected_field_error ("marker", "valid");
    }

    bool isBinary = false;
    if (context.params_.isMember ("binary"))
    {
        Json::Value const& jBinary = context.params_["binary"];
        if (!jBinary.isBool ())
            return RPC::expected_field_error ("binary", "bool");
        isBinary = jBinary.asBool ();
    }

    int limit = -1;
    int maxLimit = isBinary ? BINARY_PAGE_LENGTH : JSON_PAGE_LENGTH;

    if (context.params_.isMember ("limit"))
    {
        Json::Value const& jLimit = context.params_["limit"];
        if (!jLimit.isIntegral ())
            return RPC::expected_field_error ("limit", "integer");

        limit = jLimit.asInt ();
    }

    if ((limit < 0) || ((limit > maxLimit) && (context.role_ != Config::ADMIN)))
        limit = maxLimit;

    Json::Value jvReply = Json::objectValue;

    jvReply["ledger_hash"] = to_string (lpLedger->getHash());
    jvReply["ledger_index"] = beast::lexicalCastThrow <std::string> (lpLedger->getLedgerSeq ());

    Json::Value& nodes = (jvReply["state"] = Json::arrayValue);
    SHAMap& map = *(lpLedger->peekAccountStateMap ());

    for (;;)
    {
       SHAMapItem::pointer item = map.peekNextItem (resumePoint);
       if (!item)
           break;
       resumePoint = item->getTag();

       if (limit-- <= 0)
       {
           --resumePoint;
           jvReply["marker"] = to_string (resumePoint);
           break;
       }

       if (isBinary)
       {
           Json::Value& entry = nodes.append (Json::objectValue);
           entry["data"] = strHex (item->peekData().begin(), item->peekData().size());
           entry["index"] = to_string (item->getTag ());
       }
       else
       {
           SLE sle (item->peekSerializer(), item->getTag ());
           Json::Value& entry = nodes.append (sle.getJson (0));
           entry["index"] = to_string (item->getTag ());
       }
    }

    return jvReply;
}

} // ripple
