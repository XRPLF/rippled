#pragma once

#include <xrpld/app/main/Application.h>  // IWYU pragma: keep
#include <xrpld/rpc/Context.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <rpcspec/Errors.hpp>

#include <cstdint>
#include <expected>
#include <functional>

namespace xrpl::rpc {

/**
 * @brief Handles the `version` RPC method.
 */
class VersionHandler
{
public:
    /**
     * @brief The data a `version` request produces, written out by writeResult().
     */
    struct Output
    {
        std::uint32_t apiVersion;  ///< The API version of the request.
        bool betaEnabled;          ///< Whether the beta API version is enabled.
    };

    /**
     * @brief Construct a handler for a single request.
     *
     * @param context The context of the request being handled.
     */
    explicit VersionHandler(JsonContext& context) : context_(context)
    {
    }

    /**
     * @brief Process the request.
     *
     * @return The output to write, or the status of the error.
     */
    [[nodiscard]] std::expected<Output, ::rpc::Status>
    process() const
    {
        return Output{
            .apiVersion = context_.get().apiVersion,
            .betaEnabled = context_.get().app.config().betaRpcApi,
        };
    }

    /**
     * @brief Write the output of process() into the response.
     *
     * @param obj The response to write into.
     * @param output The output process() returned.
     */
    static void
    writeResult(json::Value& obj, Output const& output)
    {
        setVersion(obj, output.apiVersion, output.betaEnabled);
    }

private:
    std::reference_wrapper<JsonContext> context_;
};

}  // namespace xrpl::rpc
