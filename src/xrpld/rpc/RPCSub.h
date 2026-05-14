#pragma once

#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/server/InfoSub.h>

#include <boost/asio/io_context.hpp>

namespace xrpl {

/** Outbound push-subscription handle for delivering ledger events to a remote
 *  HTTP/HTTPS endpoint.
 *
 *  When a client subscribes with a `url` field, the server creates an
 *  `RPCSub` and uses it to POST JSON events to that URL as they occur — a
 *  "webhook" delivery model. `RPCSub` is the `InfoSub` subclass that
 *  implements this push path; WebSocket clients use a different subclass.
 *
 *  The concrete implementation (`RPCSubImp`) is hidden in the `.cpp` file.
 *  Callers interact only through this interface and obtain instances via
 *  `makeRPCSub()`. Events are queued internally and drained by a single
 *  `jtCLIENT_SUBSCRIBE` job at a time; failed deliveries are logged and
 *  silently dropped without retry.
 *
 *  @note This feature exists for one specific integration. New subscribers
 *      should use WebSocket instead. The `InfoSub::Source` registry methods
 *      (`findRpcSub`, `addRpcSub`, `tryRemoveRpcSub`) deduplicate
 *      subscriptions by URL so only one `RPCSub` per URL is active.
 *  @see makeRPCSub()
 */
class RPCSub : public InfoSub
{
public:
    /** Update the HTTP Basic Auth username sent with each event POST.
     *
     *  May be called at any time after construction; the change takes effect
     *  on the next outbound delivery. Thread-safe.
     *
     *  @param strUsername New username for the remote endpoint.
     */
    virtual void
    setUsername(std::string const& strUsername) = 0;

    /** Update the HTTP Basic Auth password sent with each event POST.
     *
     *  May be called at any time after construction; the change takes effect
     *  on the next outbound delivery. Thread-safe.
     *
     *  @param strPassword New password for the remote endpoint.
     */
    virtual void
    setPassword(std::string const& strPassword) = 0;

protected:
    /** Construct the base, associating this subscription with its source.
     *
     *  @param source The `InfoSub::Source` (typically `NetworkOPs`) that
     *      owns the URL registry for outbound subscriptions.
     */
    explicit RPCSub(InfoSub::Source& source);
};

/** Construct an outbound push-subscription and return it as a shared handle.
 *
 *  Parses `strUrl`, resolves the target host/port/path, and creates an
 *  `RPCSubImp` ready to accept `send()` calls. The returned object should
 *  be registered with `InfoSub::Source::addRpcSub()` so duplicate
 *  subscriptions to the same URL can be detected.
 *
 *  @param source    The subscription source (typically `NetworkOPs`) used
 *      to register and look up outbound subscriptions by URL.
 *  @param ioContext The ASIO I/O context used by `RPCCall::fromNetwork()` to
 *      schedule the async HTTP POST for each event delivery.
 *  @param jobQueue  The job queue on which `jtCLIENT_SUBSCRIBE` drain jobs
 *      are enqueued when new events arrive.
 *  @param strUrl      Destination URL; must use `http` or `https` scheme.
 *  @param strUsername HTTP Basic Auth username for the remote endpoint.
 *  @param strPassword HTTP Basic Auth password for the remote endpoint.
 *  @param registry  Service registry used to obtain a `beast::Journal` and
 *      the global `Logs&` reference required by `RPCCall::fromNetwork()`.
 *  @return A `shared_ptr<RPCSub>` backed by the concrete `RPCSubImp`.
 *  @throws std::runtime_error if `strUrl` cannot be parsed or uses an
 *      unsupported scheme (anything other than `http` or `https`).
 */
std::shared_ptr<RPCSub>
makeRPCSub(
    InfoSub::Source& source,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    ServiceRegistry& registry);

}  // namespace xrpl
