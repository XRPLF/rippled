#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/json/JsonPropertyStream.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doPrint(RPC::JsonContext& context)
{
    JsonPropertyStream stream;
    if (context.params.isObject() && context.params[jss::params].isArray() &&
        context.params[jss::params][0u].isString())
    {
        context.app.write(stream, context.params[jss::params][0u].asString());
    }
    else
    {
        context.app.write(stream);
    }

    return stream.top();
}

}  // namespace ripple
