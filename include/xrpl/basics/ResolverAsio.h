#ifndef XRPL_BASICS_RESOLVERASIO_H_INCLUDED
#define XRPL_BASICS_RESOLVERASIO_H_INCLUDED

#include <xrpl/basics/Resolver.h>
#include <xrpl/beast/utility/Journal.h>

#include <boost/asio/io_context.hpp>

namespace ripple {

class ResolverAsio : public Resolver
{
public:
    explicit ResolverAsio() = default;

    static std::unique_ptr<ResolverAsio>
    New(boost::asio::io_context&, beast::Journal);
};

}  // namespace ripple

#endif
