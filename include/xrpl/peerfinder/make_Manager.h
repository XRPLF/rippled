#pragma once

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/peerfinder/PeerfinderManager.h>
#include <xrpl/peerfinder/Types.h>
#include <xrpl/peerfinder/detail/Store.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl::PeerFinder {

/**
 * @brief Create a new Manager.
 *
 * @param ioContext The io_context used to schedule asynchronous work.
 * @param clock The clock used for timekeeping.
 * @param journal The journal used for logging.
 * @param store The persistence backend for the bootstrap cache. The caller
 *     retains ownership and must keep it alive (and opened) for the lifetime of
 *     the returned Manager. This lets consumers supply their own Store
 *     implementation (e.g. the SQLite-backed StoreSqdb in xrpld).
 * @param collector The collector used to report metrics.
 * @return The newly created Manager.
 */
std::unique_ptr<Manager>
makeManager(
    boost::asio::io_context& ioContext,
    clock_type& clock,
    beast::Journal journal,
    Store& store,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl::PeerFinder
