//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_NET_DATABASEBODY_H
#define RIPPLE_NET_DATABASEBODY_H

#include <ripple/core/DatabaseCon.h>
#include <boost/asio/io_service.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/http/message.hpp>
#include <soci/sqlite3/soci-sqlite3.h>

namespace ripple {

// DatabaseBody needs to meet requirements
// from asio which is why some conventions
// used elsewhere in this code base are not
// followed.
struct DatabaseBody
{
    // Algorithm for storing buffers when parsing.
    class reader;

    // The type of the @ref message::body member.
    class value_type;

    /** Returns the size of the body

        @param body The database body to use
    */
    static std::uint64_t
    size(value_type const& body);
};

class DatabaseBody::value_type
{
    // This body container holds a connection to the
    // database, and also caches the size when set.

    friend class reader;
    friend struct DatabaseBody;

    // The cached file size
    std::uint64_t fileSize_ = 0;
    boost::filesystem::path path_;
    std::unique_ptr<DatabaseCon> conn_;
    std::string batch_;
    std::shared_ptr<boost::asio::io_service::strand> strand_;
    std::mutex m_;
    std::condition_variable c_;
    std::uint64_t handlerCount_ = 0;
    std::uint64_t part_ = 0;
    bool closing_ = false;

public:
    /// Destructor
    ~value_type() = default;

    /// Constructor
    value_type() = default;

    /// Returns `true` if the file is open
    bool
    is_open() const
    {
        return static_cast<bool>(conn_);
    }

    /// Returns the size of the file if open
    std::uint64_t
    size() const
    {
        return fileSize_;
    }

    /// Close the file if open
    void
    close();

    /** Open a file at the given path with the specified mode

        @param path The utf-8 encoded path to the file

        @param config The configuration settings

        @param io_service The asio context for running a strand.

        @param ec Set to the error, if any occurred
    */
    void
    open(
        boost::filesystem::path path,
        Config const& config,
        boost::asio::io_service& io_service,
        boost::system::error_code& ec);
};

/** Algorithm for storing buffers when parsing.

    Objects of this type are created during parsing
    to store incoming buffers representing the body.
*/
class DatabaseBody::reader
{
    value_type& body_;  // The body we are writing to

    static constexpr std::uint32_t FLUSH_SIZE = 50000000;
    static constexpr std::uint8_t MAX_HANDLERS = 3;
    static constexpr std::uint16_t MAX_ROW_SIZE_PAD = 500;

public:
    // Constructor.
    //
    // This is called after the header is parsed and
    // indicates that a non-zero sized body may be present.
    // `h` holds the received message headers.
    // `b` is an instance of `DatabaseBody`.
    //
    template <bool isRequest, class Fields>
    explicit reader(
        boost::beast::http::header<isRequest, Fields>& h,
        value_type& b);

    // Initializer
    //
    // This is called before the body is parsed and
    // gives the reader a chance to do something that might
    // need to return an error code. It informs us of
    // the payload size (`content_length`) which we can
    // optionally use for optimization.
    //
    void
    init(boost::optional<std::uint64_t> const&, boost::system::error_code& ec);

    // This function is called one or more times to store
    // buffer sequences corresponding to the incoming body.
    //
    template <class ConstBufferSequence>
    std::size_t
    put(ConstBufferSequence const& buffers, boost::system::error_code& ec);

    void
    do_put(std::string data);

    // This function is called when writing is complete.
    // It is an opportunity to perform any final actions
    // which might fail, in order to return an error code.
    // Operations that might fail should not be attempted in
    // destructors, since an exception thrown from there
    // would terminate the program.
    //
    void
    finish(boost::system::error_code& ec);
};

}  // namespace ripple

#include <ripple/net/impl/DatabaseBody.ipp>

#endif  // RIPPLE_NET_DATABASEBODY_H
