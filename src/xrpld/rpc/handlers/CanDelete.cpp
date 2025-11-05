#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>

namespace ripple {

// can_delete [<ledgerid>|<ledgerhash>|now|always|never]
Json::Value
doCanDelete(RPC::JsonContext& context)
{
    if (!context.app.getSHAMapStore().advisoryDelete())
        return RPC::make_error(rpcNOT_ENABLED);

    Json::Value ret(Json::objectValue);

    if (context.params.isMember(jss::can_delete))
    {
        Json::Value canDelete = context.params.get(jss::can_delete, 0);
        std::uint32_t canDeleteSeq = 0;

        if (canDelete.isUInt())
        {
            canDeleteSeq = canDelete.asUInt();
        }
        else
        {
            std::string canDeleteStr = canDelete.asString();
            boost::to_lower(canDeleteStr);

            if (canDeleteStr.find_first_not_of("0123456789") ==
                std::string::npos)
            {
                canDeleteSeq = beast::lexicalCast<std::uint32_t>(canDeleteStr);
            }
            else if (canDeleteStr == "never")
            {
                canDeleteSeq = 0;
            }
            else if (canDeleteStr == "always")
            {
                canDeleteSeq = std::numeric_limits<std::uint32_t>::max();
            }
            else if (canDeleteStr == "now")
            {
                canDeleteSeq = context.app.getSHAMapStore().getLastRotated();
                if (!canDeleteSeq)
                    return RPC::make_error(rpcNOT_READY);
            }
            else if (uint256 lh; lh.parseHex(canDeleteStr))
            {
                auto ledger = context.ledgerMaster.getLedgerByHash(lh);

                if (!ledger)
                    return RPC::make_error(rpcLGR_NOT_FOUND, "ledgerNotFound");

                canDeleteSeq = ledger->info().seq;
            }
            else
            {
                return RPC::make_error(rpcINVALID_PARAMS);
            }
        }

        ret[jss::can_delete] =
            context.app.getSHAMapStore().setCanDelete(canDeleteSeq);
    }
    else
    {
        ret[jss::can_delete] = context.app.getSHAMapStore().getCanDelete();
    }

    return ret;
}

}  // namespace ripple
