/** @file
 *  Client-side RPC origination: CLI entry point and async network dispatch.
 *
 *  This header is the counterpart to RPCHandler.h. Where RPCHandler.h handles
 *  calls arriving at a running node, this file provides the machinery for
 *  initiating those calls — from the `xrpld` binary or a test harness.
 *
 *  @note This is a trusted interface. The caller (typically a node
 *      administrator) is expected to supply valid input. Error catching and
 *      rich diagnostics are intentionally minimal here; validation belongs
 *      in the server-side handler pipeline, which operates in an adversarial
 *      environment.
 */

#pragma once

#include <xrpld/core/Config.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/io_context.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xrpl {

/** Client-side RPC dispatch: CLI entry point and async network submission. */
namespace RPCCall {

/** Parse and dispatch a CLI command to a running node, printing the result.
 *
 *  Translates raw CLI tokens into a JSON-RPC request via `rpcCmdToJson()`,
 *  connects to the node described in @p config, sends the request
 *  synchronously (runs a local `io_context` to completion), and writes the
 *  formatted response to `stdout`.
 *
 *  @param config Node configuration providing server connection parameters
 *      (host, port, TLS, admin credentials). If no config is available,
 *      connection setup failures are silently swallowed so the CLI still
 *      works without a config file.
 *  @param vCmd Raw argument vector; `vCmd[0]` is the method name and
 *      subsequent elements are positional parameters.
 *  @param logs Logging sink used throughout the dispatch pipeline.
 *  @return Shell exit code: 0 (`rpcSUCCESS`) on success, 1 (`rpcBAD_SYNTAX`)
 *      for parse errors, or the numeric `error_code` from the server response
 *      on application-level errors.
 */
int
fromCommandLine(Config const& config, std::vector<std::string> const& vCmd, Logs& logs);

/** Register an async JSON-RPC request against an already-running io_context.
 *
 *  Encodes HTTP Basic Auth from @p strUsername / @p strPassword into the
 *  `Authorization` header, then delegates to `HTTPClient::request()` with a
 *  256 MB response cap and a 30-second timeout. The function returns
 *  immediately; actual I/O is driven by the caller's @p ioContext event loop.
 *
 *  @param ioContext Caller-owned Boost.Asio event loop; the async operation
 *      is registered on it but not driven — the caller must call
 *      `ioContext.run()` (or equivalent) to complete the request.
 *  @param strIp Target server IP address or hostname.
 *  @param iPort Target server port.
 *  @param strUsername HTTP Basic Auth username (may be empty).
 *  @param strPassword HTTP Basic Auth password (may be empty).
 *  @param strPath HTTP request path (empty string sends to "/").
 *  @param strMethod JSON-RPC method name.
 *  @param jvParams JSON-RPC parameters array to include in the request body.
 *  @param bSSL Whether to use TLS for the connection.
 *  @param quiet Suppresses connection log messages when true.
 *  @param logs Logging sink.
 *  @param callbackFuncP Optional callback invoked with the parsed
 *      `Json::Value` response. When omitted (default-constructed
 *      `std::function`), the response is discarded.
 *  @param headers Additional HTTP headers merged with the auto-generated
 *      `Authorization` header before the request is sent.
 */
void
fromNetwork(
    boost::asio::io_context& ioContext,
    std::string const& strIp,
    std::uint16_t const iPort,
    std::string const& strUsername,
    std::string const& strPassword,
    std::string const& strPath,
    std::string const& strMethod,
    json::Value const& jvParams,
    bool const bSSL,
    bool quiet,
    Logs& logs,
    std::function<void(json::Value const& jvInput)> callbackFuncP =
        std::function<void(json::Value const& jvInput)>(),
    std::unordered_map<std::string, std::string> headers = {});
}  // namespace RPCCall

/** Translate a raw CLI argument vector into a structured JSON-RPC request.
 *
 *  Constructs an `RPCParser` for the given @p apiVersion and dispatches to
 *  the per-method parse function found in the internal static command table.
 *  The `api_version` field is injected into the returned object unless the
 *  parse produced an error or the request already carries a version.
 *
 *  The dual-output design separates the translated request body (return
 *  value) from the raw invocation record (@p retParams), which is attached
 *  as `"rpc"` in error responses so callers can see exactly what was sent.
 *
 *  @param args Argument vector; `args[0]` is the method name, remaining
 *      elements are positional parameters.
 *  @param retParams Out-parameter filled with the raw invocation record:
 *      `{ "method": args[0], "params": [ args[1..] ] }`.
 *  @param apiVersion API version to embed in the request and use for
 *      version-sensitive parsing (e.g., `account_tx` ledger-range errors
 *      differ between v1 and v2+).
 *  @param j Journal for trace/debug logging inside the parser.
 *  @return Translated `Json::Value` request body ready for network
 *      submission, or an error object if the method was not recognised or
 *      parameter validation failed.
 */
json::Value
rpcCmdToJson(
    std::vector<std::string> const& args,
    json::Value& retParams,
    unsigned int apiVersion,
    beast::Journal j);

/** Shared RPC client path used by the CLI binary and unit tests.
 *
 *  Calls `rpcCmdToJson()` to translate @p args, reads server connection
 *  parameters from @p config (falling back gracefully if no config is
 *  available), then spins a local `io_context`, calls
 *  `RPCCall::fromNetwork()`, and blocks until the async operation completes.
 *  On error, the returned `Json::Value` includes `"rpc"` (the raw invocation
 *  record) and `"request_sent"` (the translated request) for diagnostics.
 *
 *  @note A `static_assert` in the implementation guards that `rpcBAD_SYNTAX
 *      == 1` and `rpcSUCCESS == 0`, preserving the shell exit-code contract.
 *
 *  @param args Raw argument vector; `args[0]` is the method name.
 *  @param config Node configuration used to locate the target server. Config
 *      setup failures are silently ignored so the call works without a file.
 *  @param logs Logging sink.
 *  @param apiVersion API version injected into the request and passed to the
 *      CLI uses `RPC::kAPI_COMMAND_LINE_VERSION`; tests may supply others.
 *  @param headers Additional HTTP headers forwarded to `fromNetwork()`.
 *  @return A pair of `{ exit_code, response_payload }`. The exit code is 0
 *      on success, 1 for syntax errors, or the numeric `error_code` from the
 *      server response. The JSON value is the unwrapped result object, or an
 *      `rpcJSON_RPC` error object on transport failure.
 */
std::pair<int, json::Value>
rpcClient(
    std::vector<std::string> const& args,
    Config const& config,
    Logs& logs,
    unsigned int apiVersion,
    std::unordered_map<std::string, std::string> const& headers = {});

}  // namespace xrpl
