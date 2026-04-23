#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/PublicKey.h>

#include <cstdint>
#include <string>
#include <utility>

namespace xrpl {

class ClusterNode
{
public:
    ClusterNode() = delete;

    ClusterNode(
        PublicKey const& identity,
        std::string name,
        std::uint32_t fee = 0,
        NetClock::time_point rtime = NetClock::time_point{})
        : identity_(identity), name_(name), loadFee_(fee), reportTime_(rtime)
    {
    }

    std::string const&
    name() const
    {
        return name_;
    }

    std::uint32_t
    getLoadFee() const
    {
        return loadFee_;
    }

    NetClock::time_point
    getReportTime() const
    {
        return reportTime_;
    }

    PublicKey const&
    identity() const
    {
        return identity_;
    }

private:
    PublicKey const identity_;
    std::string name_;
    std::uint32_t loadFee_ = 0;
    NetClock::time_point reportTime_ = {};
};

}  // namespace xrpl
