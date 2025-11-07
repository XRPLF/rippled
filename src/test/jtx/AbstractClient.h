#ifndef XRPL_TEST_ABSTRACTCLIENT_H_INCLUDED
#define XRPL_TEST_ABSTRACTCLIENT_H_INCLUDED

#include <xrpl/json/json_value.h>

namespace ripple {
namespace test {

/* Abstract Ripple Client interface.

   This abstracts the transport layer, allowing
   commands to be submitted to a rippled server.
*/
class AbstractClient
{
public:
    virtual ~AbstractClient() = default;
    AbstractClient() = default;
    AbstractClient(AbstractClient const&) = delete;
    AbstractClient&
    operator=(AbstractClient const&) = delete;

    /** Submit a command synchronously.

        The arguments to the function and the returned JSON
        are in a normalized format, the same whether the client
        is using the JSON-RPC over HTTP/S or WebSocket transport.

        @param cmd The command to execute
        @param params Json::Value of null or object type
                      with zero or more key/value pairs.
        @return The server response in normalized format.
    */
    virtual Json::Value
    invoke(std::string const& cmd, Json::Value const& params = {}) = 0;

    /// Get RPC 1.0 or RPC 2.0
    virtual unsigned
    version() const = 0;
};

}  // namespace test
}  // namespace ripple

#endif
