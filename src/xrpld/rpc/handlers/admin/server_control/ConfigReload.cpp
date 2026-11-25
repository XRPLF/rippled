#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <exception>
#include <set>
#include <string>
#include <system_error>

namespace xrpl {

// Reads the config file again and applies the requested sections. Each
// requested section must be one that can change while the server runs.
json::Value
doConfigReload(rpc::JsonContext& context)
{
    auto const& params = context.params;
    if (!params.isMember(jss::sections))
        return rpc::missingFieldError(jss::sections);

    auto const& requested = params[jss::sections];
    if (!requested.isArray() || requested.size() == 0)
        return rpc::invalidFieldError(jss::sections);

    std::set<std::string> sections;
    for (auto const& section : requested)
    {
        if (!section.isString())
            return rpc::invalidFieldError(jss::sections);
        auto const name = section.asString();
        if (name != Sections::kTransactionQueue)
        {
            return rpc::makeParamError(
                "Section '" + name +
                "' in 'sections' cannot be reloaded without a restart. Sections that can be "
                "reloaded: " +
                Sections::kTransactionQueue + ".");
        }
        sections.insert(name);
    }

    Config const& running = context.app.config();
    if (running.configFile().empty())
        return rpc::makeError(RpcNotSupported, "The server did not load a config file.");

    std::error_code ec;
    auto const contents = getFileContents(ec, running.configFile());
    if (ec)
        return rpc::makeError(RpcInternal, "Unable to read the config file: " + ec.message());

    // Check the full file before anything is applied.
    Config updated;
    updated.configDir = running.configDir;
    TxQ::Setup txQSetup;
    try
    {
        updated.loadFromString(contents);
        txQSetup = setupTxQ(updated);
    }
    catch (std::exception const& e)
    {
        return rpc::makeError(RpcInternal, std::string("Invalid config file: ") + e.what());
    }
    txQSetup.standAlone = running.standalone();

    context.app.getTxQ().reconfigure(txQSetup);

    json::Value result(json::ValueType::Object);
    auto& applied = (result[jss::applied] = json::ValueType::Array);
    for (auto const& name : sections)
        applied.append(name);
    return result;
}

}  // namespace xrpl
