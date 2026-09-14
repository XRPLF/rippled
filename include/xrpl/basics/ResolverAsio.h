#pragma once

#include <xrpl/basics/Resolver.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/io_context.hpp>

#include <memory>

namespace xrpl {

class ResolverAsio : public Resolver
{
public:
    explicit ResolverAsio() = default;

    static std::unique_ptr<ResolverAsio>
    make(boost::asio::io_context&, beast::Journal);
};

}  // namespace xrpl
