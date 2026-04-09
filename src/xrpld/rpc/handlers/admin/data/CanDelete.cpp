#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>

namespace xrpl {

// can_delete [<ledgerid>|<ledgerhash>|now|always|never]
Json::Value
doCanDelete(RPC::JsonContext& context)
{
    if (!context.app.getSHAMapStore().advisoryDelete())
        return RPC::make_error(rpcNOT_ENABLED);

    Json::Value ret(Json::objectValue);

    if (context.params.isMember(jss::can_delete))
    {
        Json::Value const canDelete = context.params.get(jss::can_delete, 0);
        std::uint32_t canDeleteSeq = 0;

        if (canDelete.isUInt())
        {
            canDeleteSeq = canDelete.asUInt();
        }
        else if (canDelete.isString())
        {
            std::string canDeleteStr = canDelete.asString();
            boost::to_lower(canDeleteStr);

            if (canDeleteStr.find_first_not_of("0123456789") == std::string::npos)
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
            else
            {
                return RPC::make_error(rpcINVALID_PARAMS);
            }
        }
        else
        {
            // Not a uint or string: reject with INVALID_PARAMS
            return RPC::make_error(rpcINVALID_PARAMS);
        }
    }
    else
    {
        ret[jss::can_delete] = context.app.getSHAMapStore().getCanDelete();
    }

    return ret;
}

}  // namespace xrpl
