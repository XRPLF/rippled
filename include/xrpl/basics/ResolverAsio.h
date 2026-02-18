#pragma once

#include <xrpl/basics/Resolver.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/io_context.hpp>

namespace xrpl {

class ResolverAsio : public Resolver
{
public:
    explicit ResolverAsio() = default;

    static std::unique_ptr<ResolverAsio>
    New(boost::asio::io_context&, beast::Journal);
};

}  // namespace xrpl
