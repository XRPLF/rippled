/** @file
 *  Factory declaration for the PeerFinder subsystem.
 *
 *  This is the sole mechanism by which a `Manager` is constructed. The
 *  concrete implementation type (`ManagerImp`) is never exposed in any public
 *  header, enforcing a hard compile-time boundary between consumers and the
 *  implementation.
 */

#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl::PeerFinder {

/** Construct and return an owned PeerFinder Manager instance.
 *
 *  Hides the concrete `ManagerImp` type entirely; callers hold only a
 *  `unique_ptr<Manager>` and interact exclusively through the `Manager`
 *  abstract interface. The returned object must be started via
 *  `Manager::start()` before use and stopped via `Manager::stop()` before
 *  destruction.
 *
 *  @param ioContext  Asio executor used for all asynchronous timer and I/O
 *      operations inside the manager (endpoint fetching, cache flushes,
 *      periodic logic runs).
 *  @param clock      Abstract steady clock injected for all time-based
 *      decisions. Accepting an abstract clock rather than calling
 *      `std::chrono::steady_clock::now()` directly makes the manager
 *      unit-testable with a controlled fake clock.
 *  @param journal    Diagnostic output sink; labelled `"PeerFinder"` by the
 *      caller in production.
 *  @param config     Raw server configuration used during construction; the
 *      manager converts it into its own `PeerFinder::Config` via
 *      `Config::makeConfig()`.
 *  @param collector  Metrics collector against which the manager registers
 *      internal stats counters (active inbound/outbound peer counts, etc.).
 *  @return A `unique_ptr<Manager>` holding exclusive ownership of the newly
 *      constructed manager.
 *  @see Manager, PeerfinderManager.h
 */
std::unique_ptr<Manager>
makeManager(
    boost::asio::io_context& ioContext,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl::PeerFinder
