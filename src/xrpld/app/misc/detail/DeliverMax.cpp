#include <xrpld/app/misc/DeliverMax.h>

#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace RPC {

void
insertDeliverMax(Json::Value& txJson, TxType txnType, unsigned int apiVersion)
{
    if (txJson.isMember(jss::Amount))
    {
        if (txnType == ttPAYMENT)
        {
            txJson[jss::DeliverMax] = txJson[jss::Amount];
            if (apiVersion > 1)
                txJson.removeMember(jss::Amount);
        }
    }
}

}  // namespace RPC
}  // namespace xrpl
