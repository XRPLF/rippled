#pragma once

namespace xrpl {

/**
 * Whether the absence of a config file must end the run.
 *
 * Config::setup throws for a file it found but could not read, so reaching here
 * with nothing read means the search found no file at all. That is fatal only
 * for a server start, because the RPC client runs without one.
 *
 * @param configRead Whether setup read a config file.
 * @param rpcClientMode Whether a positional argument selects the RPC client.
 * @return True when the run cannot continue.
 */
[[nodiscard]] inline bool
missingConfigIsFatal(bool configRead, bool rpcClientMode)
{
    return !configRead && !rpcClientMode;
}

}  // namespace xrpl
