#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>

#include <boost/system/error_code.hpp>

namespace xrpl::PeerFinder {

/** Abstract provider of peer IP addresses for bootstrapping.
 *
 *  Used as a fallback when the node has no local peer cache or when all
 *  known addresses are unreachable. Concrete implementations may source
 *  addresses from the config file (`[ips]`/`[ips_fixed]` stanzas via
 *  `SourceStrings`), a local file, a remote HTTPS endpoint, or a custom
 *  DNS resolver.
 *
 *  `Logic` calls `fetch()` synchronously while holding no lock, then
 *  pipes successful results into `Bootcache::insertStatic()`. The current
 *  in-flight source is tracked in `Logic::fetchSource_` so that `cancel()`
 *  can be dispatched during shutdown.
 *
 *  @note `fetch()` is synchronous; a slow or unresponsive source will stall
 *      the bootstrap thread. `cancel()` exists as an extension point for
 *      future async implementations — all current subclasses treat it as a
 *      no-op.
 */
class Source
{
public:
    /** Carries the outcome of a single `fetch()` call. */
    struct Results
    {
        explicit Results() = default;

        /** Set on failure; default-constructed (no error) on success. */
        boost::system::error_code error;

        /** Peer endpoints collected by the fetch; empty on failure. */
        IPAddresses addresses;
    };

    virtual ~Source() = default;

    /** Returns a human-readable label used in log messages by `Logic::fetch()`.
     *
     *  The value is diagnostic only and carries no operational meaning.
     */
    virtual std::string const&
    name() = 0;

    /** Requests early termination of an in-progress fetch.
     *
     *  Called by `Logic` during shutdown when a fetch is in flight.
     *  The default implementation is a no-op; async subclasses should
     *  override to abort any pending I/O.
     */
    virtual void
    cancel()
    {
    }

    /** Synchronously fetches peer addresses into @p results.
     *
     *  Implementations must populate `results.addresses` on success or
     *  set `results.error` on failure. The call is synchronous and blocks
     *  the caller until completion or until `cancel()` is invoked from
     *  another thread (for async-capable subclasses).
     *
     *  @param results  Output struct; `addresses` is populated on success,
     *      `error` is set on failure.
     *  @param journal  Journal for diagnostic logging during the fetch.
     */
    virtual void
    fetch(Results& results, beast::Journal journal) = 0;
};

}  // namespace xrpl::PeerFinder
