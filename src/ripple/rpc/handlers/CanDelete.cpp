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

#include <ripple/app/misc/SHAMapStore.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>

namespace ripple {

// can_delete [<ledgerid>|<ledgerhash>|now|always|never]
Json::Value doCanDelete (RPC::Context& context)
{
    if (! getApp().getSHAMapStore().advisoryDelete())
        return RPC::make_error(rpcNOT_ENABLED);

    Json::Value ret (Json::objectValue);

    if (context.params.isMember("can_delete"))
    {
        Json::Value canDelete  = context.params.get(jss::can_delete, 0);
        std::uint32_t canDeleteSeq = 0;

        if (canDelete.isUInt())
        {
            canDeleteSeq = canDelete.asUInt();
        }
        else
        {
            std::string canDeleteStr = canDelete.asString();
            boost::to_lower (canDeleteStr);

            if (canDeleteStr.find_first_not_of ("0123456789") ==
                std::string::npos)
            {
                canDeleteSeq =
                        beast::lexicalCast <std::uint32_t>(canDeleteStr);
            }
            else if (canDeleteStr == "never")
            {
                canDeleteSeq = 0;
            }
            else if (canDeleteStr == "always")
            {
                canDeleteSeq = std::numeric_limits <std::uint32_t>::max();
            }
            else if (canDeleteStr == "now")
            {
                canDeleteSeq = getApp().getSHAMapStore().getLastRotated();
                if (!canDeleteSeq)
                    return RPC::make_error (rpcNOT_READY);            }
                else if (canDeleteStr.size() == 64 &&
                    canDeleteStr.find_first_not_of("0123456789abcdef") ==
                    std::string::npos)
            {
                uint256 ledgerHash (0);
                ledgerHash.SetHex (canDeleteStr);
                Ledger::pointer ledger =
                        context.netOps.getLedgerByHash (ledgerHash);
                if (!ledger)
                    return RPC::make_error(rpcLGR_NOT_FOUND, "ledgerNotFound");

                canDeleteSeq = ledger->getLedgerSeq();
            }
            else
            {
                return RPC::make_error (rpcINVALID_PARAMS);
            }
        }

        ret["can_delete"] =
                getApp().getSHAMapStore().setCanDelete (canDeleteSeq);
    }
    else
    {
        ret["can_delete"] = getApp().getSHAMapStore().getCanDelete();
    }

    return ret;
}

} // ripple
