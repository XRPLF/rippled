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
        : identity_(identity), name_(std::move(name)), mLoadFee(fee), mReportTime(rtime)
    {
    }

    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    [[nodiscard]] std::uint32_t
    getLoadFee() const
    {
        return mLoadFee;
    }

    [[nodiscard]] NetClock::time_point
    getReportTime() const
    {
        return mReportTime;
    }

    [[nodiscard]] PublicKey const&
    identity() const
    {
        return identity_;
    }

private:
    PublicKey const identity_;
    std::string name_;
    std::uint32_t mLoadFee = 0;
    NetClock::time_point mReportTime;
};

}  // namespace xrpl
