/** @file
 *  Concrete implementation of the outbound HTTP/HTTPS push-subscription
 *  mechanism. Exposes only `RPCSub` (abstract) and `makeRPCSub()` to callers;
 *  the concrete class `RPCSubImp` is entirely private to this translation unit.
 *
 *  Events are queued in-order and drained by a single `jtCLIENT_SUBSCRIBE`
 *  job at a time. Failed deliveries are logged and silently dropped; there
 *  is no retry or back-pressure mechanism.
 */
#include <xrpld/rpc/RPCSub.h>

#include <xrpld/rpc/RPCCall.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/server/InfoSub.h>

#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace xrpl {

/** Concrete implementation of `RPCSub` that delivers events via outbound
 *  HTTP/HTTPS POST.
 *
 *  Entirely hidden from callers; only the `RPCSub` interface and the
 *  `makeRPCSub()` factory are exposed in the header. The target URL is
 *  parsed eagerly at construction time — the object either comes out fully
 *  ready to use or throws before any state is committed.
 *
 *  Threading: `send()` may be called from any thread. At most one
 *  `jtCLIENT_SUBSCRIBE` drain job (`sendThread()`) is active at a time;
 *  the `sending_` flag enforces this invariant under `lock_`.
 *
 *  @note `username_`/`password_` are written under `lock_` but read in
 *      `sendThread()` without the lock — a minor data race. Credential
 *      updates are rare, so observable corruption is unlikely in practice.
 */
class RPCSubImp : public RPCSub
{
public:
    /** Construct and validate the push-subscription endpoint.
     *
     *  Parses `strUrl` immediately, validates the scheme, and resolves
     *  the default port (80 for HTTP, 443 for HTTPS) if none is specified.
     *
     *  @param source      `InfoSub::Source` that owns the URL registry.
     *  @param ioContext   ASIO I/O context forwarded to `RPCCall::fromNetwork()`.
     *  @param jobQueue    Job queue on which drain jobs are enqueued.
     *  @param strUrl      Destination URL; must use `http` or `https` scheme.
     *  @param strUsername HTTP Basic Auth username for the remote endpoint.
     *  @param strPassword HTTP Basic Auth password for the remote endpoint.
     *  @param registry    Service registry for journal and logs access.
     *  @throws std::runtime_error if the URL cannot be parsed or uses an
     *      unsupported scheme.
     */
    RPCSubImp(
        InfoSub::Source& source,
        boost::asio::io_context& ioContext,
        JobQueue& jobQueue,
        std::string const& strUrl,
        std::string strUsername,
        std::string strPassword,
        ServiceRegistry& registry)
        : RPCSub(source)
        , io_context_(ioContext)
        , jobQueue_(jobQueue)
        , url_(strUrl)
        , username_(std::move(strUsername))
        , password_(std::move(strPassword))
        , j_(registry.getJournal("RPCSub"))
        , logs_(registry.getLogs())
    {
        ParsedUrl pUrl;

        if (!parseUrl(pUrl, strUrl))
        {
            Throw<std::runtime_error>("Failed to parse url.");
        }
        else if (pUrl.scheme == "https")
        {
            ssl_ = true;
        }
        else if (pUrl.scheme != "http")
        {
            Throw<std::runtime_error>("Only http and https is supported.");
        }

        seq_ = 1;

        ip_ = pUrl.domain;
        if (!pUrl.port)
        {
            port_ = ssl_ ? 443 : 80;
        }
        else
        {
            port_ = *pUrl.port;
        }
        path_ = pUrl.path;

        JLOG(j_.info()) << "RPCCall::fromNetwork sub: ip=" << ip_ << " port=" << port_
                        << " ssl= " << (ssl_ ? "yes" : "no") << " path='" << path_ << "'";
    }

    ~RPCSubImp() override = default;

    /** Enqueue an event for delivery and ensure the drain job is running.
     *
     *  Pushes `jvObj` onto the back of `deque_` with a monotonically
     *  increasing sequence number, then submits a `jtCLIENT_SUBSCRIBE` job
     *  to drain the queue if one is not already active. If a drain job is
     *  already running, the new item will be picked up by the existing job's
     *  loop without spawning a second job.
     *
     *  @param jvObj      The JSON event payload to deliver.
     *  @param broadcast  When true, the event is logged at debug level rather
     *      than info level (i.e., it was broadcast to multiple subscribers).
     */
    void
    send(json::Value const& jvObj, bool broadcast) override
    {
        std::scoped_lock const sl(lock_);

        auto jm = broadcast ? j_.debug() : j_.info();
        JLOG(jm) << "RPCCall::fromNetwork push: " << jvObj;

        deque_.emplace_back(seq_++, jvObj);

        if (!sending_)
        {
            // Start a sending thread.
            JLOG(j_.info()) << "RPCCall::fromNetwork start";

            sending_ =
                jobQueue_.addJob(JtClientSubscribe, "RPCSubSendThr", [this]() { sendThread(); });
        }
    }

    /** @copydoc RPCSub::setUsername */
    void
    setUsername(std::string const& strUsername) override
    {
        std::scoped_lock const sl(lock_);

        username_ = strUsername;
    }

    /** @copydoc RPCSub::setPassword */
    void
    setPassword(std::string const& strPassword) override
    {
        std::scoped_lock const sl(lock_);

        password_ = strPassword;
    }

private:
    /** Drain the event queue, delivering each item via `RPCCall::fromNetwork()`.
     *
     *  Runs on a `jtCLIENT_SUBSCRIBE` worker thread. Each iteration acquires
     *  `lock_` only long enough to pop one item (or detect the queue is empty
     *  and clear `sending_`). The actual HTTP call is made outside the lock so
     *  that `send()` can continue enqueuing events during a slow round-trip.
     *
     *  Each outgoing payload has a monotonically increasing `"seq"` field
     *  injected so the remote endpoint can detect dropped or reordered events.
     *  Delivery is fire-and-forget: exceptions from `fromNetwork()` are caught
     *  and logged; there is no retry.
     *
     *  @note `username_` and `password_` are read here without holding `lock_`,
     *      creating a potential data race with `setUsername()`/`setPassword()`.
     *      In practice, credential updates are rare and string assignment is
     *      unlikely to produce observable corruption.
     */
    void
    sendThread()
    {
        json::Value jvEvent;
        bool bSend = false;

        do
        {
            {
                std::scoped_lock const sl(lock_);

                if (deque_.empty())
                {
                    sending_ = false;
                    bSend = false;
                }
                else
                {
                    auto const [seq, env] = deque_.front();

                    deque_.pop_front();

                    jvEvent = env;
                    jvEvent["seq"] = seq;

                    bSend = true;
                }
            }

            if (bSend)
            {
                try
                {
                    JLOG(j_.info()) << "RPCCall::fromNetwork: " << ip_;

                    RPCCall::fromNetwork(
                        io_context_,
                        ip_,
                        port_,
                        username_,
                        password_,
                        path_,
                        "event",
                        jvEvent,
                        ssl_,
                        true,
                        logs_);
                }
                catch (std::exception const& e)
                {
                    JLOG(j_.info()) << "RPCCall::fromNetwork exception: " << e.what();
                }
            }
        } while (bSend);
    }

private:
    boost::asio::io_context& io_context_; /**< ASIO context for `RPCCall::fromNetwork()`. */
    JobQueue& jobQueue_;                  /**< Queue on which drain jobs are submitted. */

    std::string url_;         /**< Original URL string (stored for diagnostics). */
    std::string ip_;          /**< Resolved hostname from the parsed URL. */
    std::uint16_t port_;      /**< Resolved port; defaults to 80 (HTTP) or 443 (HTTPS). */
    bool ssl_{false};         /**< True when the target scheme is `https`. */
    std::string username_;    /**< HTTP Basic Auth username; written under `lock_`. */
    std::string password_;    /**< HTTP Basic Auth password; written under `lock_`. */
    std::string path_;        /**< URL path component forwarded to `fromNetwork()`. */

    int seq_;           /**< Sequence counter; incremented for each enqueued event. */

    bool sending_{false}; /**< True while a drain job is active; guards single-worker invariant. */

    /** Pending events: (sequence_number, json_payload) pairs in delivery order. */
    std::deque<std::pair<int, json::Value>> deque_;

    beast::Journal const j_;
    Logs& logs_;
};

//------------------------------------------------------------------------------

RPCSub::RPCSub(InfoSub::Source& source) : InfoSub(source, Consumer())
{
}

std::shared_ptr<RPCSub>
makeRPCSub(
    InfoSub::Source& source,
    boost::asio::io_context& ioContext,
    JobQueue& jobQueue,
    std::string const& strUrl,
    std::string const& strUsername,
    std::string const& strPassword,
    ServiceRegistry& registry)
{
    return std::make_shared<RPCSubImp>(
        std::ref(source),
        std::ref(ioContext),
        std::ref(jobQueue),
        strUrl,
        strUsername,
        strPassword,
        registry);
}

}  // namespace xrpl
