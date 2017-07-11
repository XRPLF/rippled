//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/core/error.hpp>
#include <beast/core/file_base.hpp>
#include <beast/http/message.hpp>
#include <beast/http/type_traits.hpp>
#include <boost/optional.hpp>
#include <cstdint>
#include <utility>

namespace beast {
namespace http {

class BodyReader;
class BodyWriter;

//[concept_Body

struct Body
{
    // The type of message::body when used
    struct value_type;

    /// The algorithm used for extracting buffers
    using reader = BodyReader;

    /// The algorithm used for inserting buffers
    using writer = BodyWriter;

    /// Returns the body's payload size
    static
    std::uint64_t
    size(value_type const& v);
};

static_assert(is_body<Body>::value, "");

//]

struct Body_BodyReader {
    struct value_type{};
//[concept_BodyReader

struct BodyReader
{
public:
    /// The type of buffer returned by `get`.
    using const_buffers_type = boost::asio::const_buffers_1;

    /** Construct the reader.

        @param msg The message whose body is to be retrieved.

        @param ec Set to the error, if any occurred.
    */
    template<bool isRequest, class Body, class Fields>
    explicit
    BodyReader(message<isRequest, Body, Fields> const& msg);

    /** Initialize the reader

        This is called after construction and before the first
        call to `get`. The message is valid and complete upon
        entry.

        @param ec Set to the error, if any occurred.
    */
    void
    init(error_code& ec)
    {
        // The specification requires this to indicate "no error"
        ec.assign(0, ec.category());
    }

    /** Returns the next buffer in the body.

        @li If the return value is `boost::none` (unseated optional) and
            `ec` does not contain an error, this indicates the end of the
            body, no more buffers are present.

        @li If the optional contains a value, the first element of the
            pair represents a @b ConstBufferSequence containing one or
            more octets of the body data. The second element indicates
            whether or not there are additional octets of body data.
            A value of `true` means there is more data, and that the
            implementation will perform a subsequent call to `get`.
            A value of `false` means there is no more body data.

        @li If `ec` contains an error code, the return value is ignored.

        @param ec Set to the error, if any occurred.
    */
    boost::optional<std::pair<const_buffers_type, bool>>
    get(error_code& ec)
    {
        // The specification requires this to indicate "no error"
        ec.assign(0, ec.category());

        return boost::none; // for exposition only
    }
};

//]
    using reader = BodyReader;
};

static_assert(is_body_reader<Body_BodyReader>::value, "");

struct Body_BodyWriter {
    struct value_type{};
//[concept_BodyWriter

struct BodyWriter
{
    /** Construct the writer.

        @param msg The message whose body is to be stored.
    */
    template<bool isRequest, class Body, class Fields>
    explicit
    BodyWriter(message<isRequest, Body, Fields>& msg);

    /** Initialize the writer

        This is called after construction and before the first
        call to `put`. The message is valid and complete upon
        entry.

        @param ec Set to the error, if any occurred.
    */
    void
    init(
        boost::optional<std::uint64_t> const& content_length,
        error_code& ec)
    {
        boost::ignore_unused(content_length);

        // The specification requires this to indicate "no error"
        ec.assign(0, ec.category());
    }

    /** Store buffers.

        This is called zero or more times with parsed body octets.

        @param buffers The constant buffer sequence to store.

        @param ec Set to the error, if any occurred.

        @return The number of bytes transferred from the input buffers.
    */
    template<class ConstBufferSequence>
    std::size_t
    put(ConstBufferSequence const& buffers, error_code& ec)
    {
        // The specification requires this to indicate "no error"
        ec = {};

        return boost::asio::buffer_size(buffers);
    }

    /** Called when the body is complete.

        @param ec Set to the error, if any occurred.
    */
    void
    finish(error_code& ec)
    {
        // The specification requires this to indicate "no error"
        ec = {};
    }
};

//]
    using writer = BodyWriter;
};

static_assert(is_body_writer<Body_BodyWriter>::value, "");

//[concept_Fields

class Fields
{
public:
    struct reader;

protected:
    /** Returns the request-method string.

        @note Only called for requests.
    */
    string_view
    get_method_impl() const;

    /** Returns the request-target string.

        @note Only called for requests.
    */
    string_view
    get_target_impl() const;

    /** Returns the response reason-phrase string.

        @note Only called for responses.
    */
    string_view
    get_reason_impl() const;

    /** Returns the chunked Transfer-Encoding setting
    */
    bool
    get_chunked_impl() const;

    /** Returns the keep-alive setting
    */
    bool
    get_keep_alive_impl(unsigned version) const;

    /** Set or clear the method string.

        @note Only called for requests.
    */
    void
    set_method_impl(string_view s);

    /** Set or clear the target string.

        @note Only called for requests.
    */
    void
    set_target_impl(string_view s);

    /** Set or clear the reason string.

        @note Only called for responses.
    */
    void
    set_reason_impl(string_view s);

    /** Sets or clears the chunked Transfer-Encoding value
    */
    void
    set_chunked_impl(bool value);

    /** Sets or clears the Content-Length field
    */
    void
    set_content_length_impl(boost::optional<std::uint64_t>);

    /** Adjusts the Connection field
    */
    void
    set_keep_alive_impl(unsigned version, bool keep_alive);
};

static_assert(is_fields<Fields>::value,
    "Fields requirements not met");

//]

struct Fields_FieldsReader {
    using F = Fields_FieldsReader;
//[concept_FieldsReader

struct FieldsReader
{
    // The type of buffers returned by `get`
    struct const_buffers_type;

    // Constructor for requests
    FieldsReader(F const& f, unsigned version, verb method);

    // Constructor for responses
    FieldsReader(F const& f, unsigned version, unsigned status);

    // Returns `true` if keep-alive is indicated
    bool
    keep_alive();

    // Returns the serialized header buffers
    const_buffers_type
    get();
};

//]
};

//[concept_File

struct File
{
    /** Default constructor

        There is no open file initially.
    */
    File();

    /** Destructor

        If the file is open it is first closed.
    */
    ~File();

    /// Returns `true` if the file is open
    bool
    is_open() const;

    /// Close the file if open
    void
    close(error_code& ec);

    /// Open a file at the given path with the specified mode
    void
    open(char const* path, file_mode mode, error_code& ec);

    /// Return the size of the open file
    std::uint64_t
    size(error_code& ec) const;

    /// Return the current position in the open file
    std::uint64_t
    pos(error_code& ec) const;

    /// Adjust the current position in the open file
    void
    seek(std::uint64_t offset, error_code& ec);

    /// Read from the open file
    std::size_t
    read(void* buffer, std::size_t n, error_code& ec) const;

    /// Write to the open file
    std::size_t
    write(void const* buffer, std::size_t n, error_code& ec);
};

//]

} // http
} // beast
