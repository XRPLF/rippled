#include <xrpld/app/misc/DeliverMax.h>

#include <xrpl/protocol/jss.h>

namespace ripple {
namespace RPC {

void
insertDeliverMax(Json::Value& tx_json, TxType txnType, unsigned int apiVersion)
{
    if (tx_json.isMember(jss::Amount))
    {
        if (txnType == ttPAYMENT)
        {
            tx_json[jss::DeliverMax] = tx_json[jss::Amount];
            if (apiVersion > 1)
                tx_json.removeMember(jss::Amount);
        }
    }
}

}  // namespace RPC
}  // namespace ripple
