#ifndef XRPL_BASICS_RESOLVER_H_INCLUDED
#define XRPL_BASICS_RESOLVER_H_INCLUDED

#include <xrpl/beast/net/IPEndpoint.h>

#include <functional>
#include <vector>

namespace ripple {

class Resolver
{
public:
    using HandlerType =
        std::function<void(std::string, std::vector<beast::IP::Endpoint>)>;

    virtual ~Resolver() = 0;

    /** Issue an asynchronous stop request. */
    virtual void
    stop_async() = 0;

    /** Issue a synchronous stop request. */
    virtual void
    stop() = 0;

    /** Issue a synchronous start request. */
    virtual void
    start() = 0;

    /** resolve all hostnames on the list
        @param names the names to be resolved
        @param handler the handler to call
    */
    /** @{ */
    template <class Handler>
    void
    resolve(std::vector<std::string> const& names, Handler handler)
    {
        resolve(names, HandlerType(handler));
    }

    virtual void
    resolve(
        std::vector<std::string> const& names,
        HandlerType const& handler) = 0;
    /** @} */
};

}  // namespace ripple

#endif
