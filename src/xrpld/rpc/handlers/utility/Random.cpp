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

// Result:
// {
//   random: <uint256>
// }
json::Value
doRandom(RPC::JsonContext& context)
{
    // TODO(tom): the try/catch is almost certainly redundant, we catch at the
    // top level too.
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
