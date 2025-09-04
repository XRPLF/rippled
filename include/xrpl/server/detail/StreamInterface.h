#ifndef RIPPLE_SERVER_STREAMINTERFACE_H_INCLUDED
#define RIPPLE_SERVER_STREAMINTERFACE_H_INCLUDED

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <functional>
#include <optional>

namespace ripple {

// Forward declarations
using socket_type = boost::beast::tcp_stream;
using concrete_stream_type = boost::beast::ssl_stream<socket_type>;

/**
 * @brief Minimal interface for stream operations needed by PeerImp
 */
class StreamInterface
{
public:
    virtual ~StreamInterface() = default;

    // Executor access for ASIO operations
    virtual boost::asio::any_io_executor
    get_executor() = 0;

    // Connection status checking
    virtual bool
    is_open() const = 0;

    // Stream lifecycle operations
    virtual void
    close() = 0;

    virtual void
    cancel() = 0;

    // Async I/O operations
    virtual void
    async_read_some(
        boost::beast::multi_buffer::mutable_buffers_type const& buffers,
        std::function<void(boost::beast::error_code, std::size_t)> handler) = 0;

    virtual void
    async_write_some(
        boost::asio::const_buffer buffer,
        std::function<void(boost::beast::error_code, std::size_t)> handler) = 0;

    virtual void
    async_write(
        boost::asio::const_buffer buffer,
        std::function<void(boost::beast::error_code, std::size_t)> handler) = 0;

    virtual void
    async_write(
        boost::beast::multi_buffer::const_buffers_type const& buffers,
        std::function<void(boost::beast::error_code, std::size_t)> handler) = 0;

    virtual void
    async_shutdown(std::function<void(boost::beast::error_code)> handler) = 0;

    // SSL handshake support
    virtual std::optional<base_uint<256>>
    makeSharedValue(beast::Journal journal) = 0;
};

/**
 * @brief Production implementation wrapping boost::beast::ssl_stream
 */
class ProductionStream : public StreamInterface
{
private:
    std::unique_ptr<concrete_stream_type> stream_;

public:
    explicit ProductionStream(std::unique_ptr<concrete_stream_type> stream)
        : stream_(std::move(stream))
    {
    }

    boost::asio::any_io_executor
    get_executor() override
    {
        return stream_->get_executor();
    }

    bool
    is_open() const override
    {
        return stream_->next_layer().socket().is_open();
    }

    void
    close() override
    {
        stream_->lowest_layer().close();
    }

    void
    cancel() override
    {
        stream_->lowest_layer().cancel();
    }

    void
    async_read_some(
        boost::beast::multi_buffer::mutable_buffers_type const& buffers,
        std::function<void(boost::beast::error_code, std::size_t)> handler)
        override
    {
        stream_->async_read_some(buffers, std::move(handler));
    }

    void
    async_write_some(
        boost::asio::const_buffer buffer,
        std::function<void(boost::beast::error_code, std::size_t)> handler)
        override
    {
        stream_->async_write_some(buffer, std::move(handler));
    }

    void
    async_write(
        boost::asio::const_buffer buffer,
        std::function<void(boost::beast::error_code, std::size_t)> handler)
        override
    {
        boost::asio::async_write(*stream_, buffer, std::move(handler));
    }

    void
    async_write(
        boost::beast::multi_buffer::const_buffers_type const& buffers,
        std::function<void(boost::beast::error_code, std::size_t)> handler)
        override
    {
        boost::asio::async_write(*stream_, buffers, std::move(handler));
    }

    void
    async_shutdown(
        std::function<void(boost::beast::error_code)> handler) override
    {
        stream_->async_shutdown(std::move(handler));
    }

    std::optional<base_uint<256>>
    makeSharedValue(beast::Journal journal) override;
};

}  // namespace ripple

#endif
