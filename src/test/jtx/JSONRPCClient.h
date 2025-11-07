#ifndef XRPL_TEST_HTTPCLIENT_H_INCLUDED
#define XRPL_TEST_HTTPCLIENT_H_INCLUDED

#include <test/jtx/AbstractClient.h>

#include <xrpld/core/Config.h>

#include <memory>

namespace ripple {
namespace test {

/** Returns a client using JSON-RPC over HTTP/S. */
std::unique_ptr<AbstractClient>
makeJSONRPCClient(Config const& cfg, unsigned rpc_version = 2);

}  // namespace test
}  // namespace ripple

#endif
