/** @file
 *  Factory interface for the XRPL overlay subsystem.
 *
 *  Defines the two-phase construction entry point for the peer-to-peer mesh:
 *  `setupOverlay` validates raw configuration and builds the `Overlay::Setup`
 *  value object, and `makeOverlay` accepts that object together with all live
 *  runtime dependencies and returns the concrete `OverlayImpl` behind the
 *  abstract `Overlay` pointer.
 *
 *  Separating construction here from `Overlay.h` is an intentional
 *  dependency-inversion boundary.  `Overlay.h` is included by consensus,
 *  ledger acquisition, transaction relay, and RPC — none of which need
 *  `ServerHandler`, `Resolver`, or Boost.Asio at compile time.  Only
 *  application bootstrap code includes this header and pays the full
 *  include cost.
 */

#pragma once

#include <xrpld/overlay/Overlay.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>

#include <boost/asio/io_context.hpp>

namespace xrpl {

/** Parse overlay-related config sections into an `Overlay::Setup` value object.
 *
 *  Reads four config sections and validates every field before returning:
 *
 *  - **`[overlay]`** — builds the shared TLS context via `make_SSLContext`,
 *    reads an optional `ip_limit` (must be ≥ 0), and reads an optional
 *    `public_ip` that must resolve to a non-private address.
 *  - **`[crawl]`** — assembles a `crawlOptions` bitmask controlling what the
 *    `/crawl` HTTP endpoint exposes (overlay topology, server info, counts,
 *    UNL).  A top-level `0` disables the endpoint entirely.
 *  - **`[vl]`** — toggles `vlEnabled` for the `/vl/` validator-list endpoint.
 *  - **`[network_id]`** — accepts symbolic aliases `"main"` (0),
 *    `"testnet"` (1), `"devnet"` (2), or a raw decimal integer.  An absent
 *    section leaves `networkID` empty, disabling network-ID handshake checks.
 *
 *  No live resources are allocated; the returned `Setup` is a pure value type
 *  suitable for unit testing without a running I/O context.  The function
 *  throws immediately on the first invalid field, surfacing configuration
 *  errors at startup before any I/O infrastructure is started.
 *
 *  @param config  Full application configuration to parse.
 *  @return Populated `Overlay::Setup` ready to pass directly to `makeOverlay`.
 *  @throws std::runtime_error if any configuration field is invalid.
 */
Overlay::Setup
setupOverlay(BasicConfig const& config);

/** Construct the concrete overlay implementation and return it as `Overlay`.
 *
 *  Internally a one-liner: constructs `OverlayImpl` (defined entirely within
 *  the `detail/` subdirectory) and returns it behind the abstract pointer.
 *  No caller ever sees the concrete type; the implementation is fully opaque
 *  across this boundary.
 *
 *  @param app             Central application context; used by `OverlayImpl`
 *      to reach the ledger master, job queue, hash router, and most other
 *      subsystems.
 *  @param setup           Pre-parsed overlay configuration from
 *      `setupOverlay()`.
 *  @param serverHandler   HTTP server handler through which inbound peer
 *      connections arrive as TLS upgrade requests.  The overlay does not own
 *      a separate listening socket.
 *  @param resourceManager Shared rate-limiting and resource-accounting
 *      manager, consistent with the RPC server's back-pressure strategy.
 *  @param resolver        Async DNS resolver used to bootstrap peer addresses
 *      from hostnames.  Injected so DNS behaviour is replaceable in tests.
 *  @param ioContext       Shared ASIO I/O context; `OverlayImpl` derives its
 *      own strand from this context for internal dispatch.
 *  @param config          Full application config, allowing `OverlayImpl` to
 *      read subsections (e.g., peerfinder settings) beyond what `setup`
 *      captures.
 *  @param collector       Metrics collector for overlay-specific counters and
 *      gauges; registered once at construction and stable for the lifetime of
 *      the server.
 *  @return Owning pointer to the newly constructed overlay instance.
 */
std::unique_ptr<Overlay>
makeOverlay(
    Application& app,
    Overlay::Setup const& setup,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_context& ioContext,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl
