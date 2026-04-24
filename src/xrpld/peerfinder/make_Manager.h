#pragma once

#include <xrpld/peerfinder/PeerfinderManager.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl::PeerFinder {

/** Create a new Manager. */
std::unique_ptr<Manager>
make_Manager(
    boost::asio::io_context& io_context,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);

}  // namespace xrpl::PeerFinder
