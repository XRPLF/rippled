#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <limits>
#include <string>

namespace xrpl {

// can_delete [<ledgerid>|<ledgerhash>|now|always|never]
json::Value
doCanDelete(rpc::JsonContext& context)
{
    if (!context.app.getSHAMapStore().advisoryDelete())
        return rpc::makeError(RpcNotEnabled);

    json::Value ret(json::ValueType::Object);

    if (context.params.isMember(jss::can_delete))
    {
        json::Value const canDelete = context.params.get(jss::can_delete, 0);
        std::uint32_t canDeleteSeq = 0;

        if (canDelete.isUInt())
        {
            canDeleteSeq = canDelete.asUInt();
        }
        else if (canDelete.isInt())
        {
            auto const seq = canDelete.asInt();
            if (seq < 0)
                return RPC::make_error(rpcINVALID_PARAMS);
            canDeleteSeq = static_cast<std::uint32_t>(seq);
        }
        else if (canDelete.isString())
        {
            std::string canDeleteStr = canDelete.asString();
            canDeleteStr = toLower(canDeleteStr);

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
            else if (canDeleteStr == "now")
            {
                canDeleteSeq = context.app.getSHAMapStore().getLastRotated();
                if (canDeleteSeq == 0u)
                    return rpc::makeError(RpcNotReady);
            }
            else if (uint256 lh; lh.parseHex(canDeleteStr))
            {
                auto ledger = context.ledgerMaster.getLedgerByHash(lh);

                if (!ledger)
                    return rpc::makeError(RpcLgrNotFound, "ledgerNotFound");

                canDeleteSeq = ledger->header().seq;
            }
            else
            {
                return rpc::makeError(RpcInvalidParams);
            }
        }
        else
        {
            return RPC::make_error(rpcINVALID_PARAMS);
        }

        ret[jss::can_delete] = context.app.getSHAMapStore().setCanDelete(canDeleteSeq);
    }
    else
    {
        ret[jss::can_delete] = context.app.getSHAMapStore().getCanDelete();
    }

    return ret;
}

}  // namespace xrpl
