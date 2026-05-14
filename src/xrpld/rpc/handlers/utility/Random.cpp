#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/rngfill.h>
#include <xrpl/crypto/csprng.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

#include <exception>

namespace xrpl {

namespace RPC {
struct JsonContext;
}  // namespace RPC

/** Handler for the `random` RPC command.
 *
 *  Draws 256 bits of entropy from the node's cryptographically secure PRNG
 *  and returns the value to the caller as a 64-character lowercase hex string
 *  under the `random` key.
 *
 *  Registered in `Handler.cpp` as:
 *  @code
 *  {"random", byRef(&doRandom), Role::USER, NO_CONDITION}
 *  @endcode
 *
 *  The `Role::USER` designation allows any connected client — not just
 *  admins — to invoke it. `NO_CONDITION` means no ledger state is required;
 *  the handler runs unconditionally because it never touches ledger data.
 *
 *  **Algorithm:**
 *  1. Declare a zero-initialised `uint256` (32 bytes).
 *  2. Fill it via `beast::rngfill` using `cryptoPrng()`, which returns a
 *     reference to the process-wide mutex-protected `csprng_engine` singleton.
 *     The engine seeds from `std::random_device` and satisfies the C++
 *     `UniformRandomNumberEngine` named requirement.
 *  3. Serialise the filled value to hex via `to_string()` and return it under
 *     `jss::random`.
 *
 *  **Exception handling:** The body is wrapped in a `try/catch` that returns
 *  `rpcError(RpcInternal)` on any `std::exception`. This guard is a legacy
 *  defensive pattern — the RPC dispatch layer already has a top-level catch —
 *  and the branch is marked `LCOV_EXCL_LINE` because it is never expected to
 *  execute in practice.
 *
 *  **Use case:** Clients that want a trusted external entropy source (e.g., for
 *  combined randomness schemes) can call this endpoint and mix the result with
 *  locally generated entropy. The trust model is that the client trusts the
 *  node operator's CSPRNG seeding.
 *
 *  @param context The RPC dispatch context. Its contents are not read by this
 *      handler; the parameter exists only to satisfy the handler signature
 *      contract.
 *  @return A JSON object `{ "random": "<64-char hex string>" }` on success,
 *      or `rpcError(RpcInternal)` if an unexpected exception propagates out
 *      of `beast::rngfill` (effectively unreachable).
 */
json::Value
doRandom(RPC::JsonContext& context)
{
    try
    {
        uint256 rand;
        beast::rngfill(rand.begin(), rand.size(), cryptoPrng());

        json::Value jvResult;
        jvResult[jss::random] = to_string(rand);
        return jvResult;
    }
    catch (std::exception const&)
    {
        return rpcError(RpcInternal);  // LCOV_EXCL_LINE
    }
}

}  // namespace xrpl
