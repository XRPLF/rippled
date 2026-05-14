/** @file
 *  Custom Boost.Beast HTTP body type that holds a `Json::Value` as the
 *  in-memory representation and serializes it to wire bytes on demand.
 *
 *  Used by `OverlayImpl` (diagnostic endpoints: `/crawl`, `/health`, `/vl/`)
 *  and `ServerHandler` (RPC dispatch layer) when building outbound HTTP
 *  responses. There is intentionally no BodyWriter (deserialization) — every
 *  call site uses `JsonBody` only for responses; incoming request bodies are
 *  parsed separately via `Json::Reader`.
 */

#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/message.hpp>

namespace xrpl {

/** Boost.Beast HTTP body type whose in-memory value is a `Json::Value`.
 *
 *  Satisfies the Beast `Body` concept by providing a `value_type` alias and
 *  nested `reader`/`writer` classes that both implement the Beast BodyReader
 *  interface (serialization only — there is no deserialization path).
 *
 *  The struct itself carries no data; Beast only instantiates `reader` and
 *  `writer` internally. The `explicit` default constructor suppresses accidental
 *  implicit construction while preserving the Beast concept requirements.
 *
 *  @note Both `reader` and `writer` serialize eagerly at construction time, so
 *      by the time Beast's async I/O layer requests bytes they are already
 *      available. Neither class defers or streams output incrementally.
 *  @see JsonBody::reader, JsonBody::writer
 */
struct JsonBody
{
    explicit JsonBody() = default;

    /** The in-memory body representation used by Beast message types. */
    using value_type = json::Value;

    /** Old-style Beast BodyReader that serializes a `Json::Value` into a
     *  `boost::beast::multi_buffer` using `Json::stream()`.
     *
     *  The entire JSON payload is written into `buffer_` at construction time
     *  via a chunked-write callback. Subsequent calls to `init()`, `get()`,
     *  and `finish()` are therefore trivial. `is_deferred = std::false_type`
     *  informs Beast that buffer preparation does not need to be deferred.
     *
     *  This older constructor form takes the full `http::message` (header +
     *  body together), as opposed to the newer split-argument form used by
     *  `writer`.
     */
    class reader  // NOLINT(readability-identifier-naming) -- Boost.Beast body concept name
    {
        using dynamic_buffer_type = boost::beast::multi_buffer;

        dynamic_buffer_type buffer_;

    public:
        /** Buffer sequence type returned by `get()`. Satisfies the
         *  Beast `ConstBufferSequence` requirement; may span multiple
         *  discontiguous memory regions. */
        using const_buffers_type = typename dynamic_buffer_type::const_buffers_type;

        /** Tells Beast that buffer preparation is not deferred; the buffer is
         *  fully populated by the constructor before `init()` is called. */
        using is_deferred = std::false_type;

        /** Construct the reader and eagerly serialize `m.body()` into an
         *  internal `multi_buffer` via `Json::stream()`.
         *
         *  @tparam IsRequest Whether the enclosing message is a request.
         *  @tparam Fields    The HTTP header fields type.
         *  @param m          The HTTP message whose body is to be serialized.
         */
        template <bool IsRequest, class Fields>
        explicit reader(boost::beast::http::message<IsRequest, JsonBody, Fields> const& m)
        {
            stream(m.body, [&](void const* data, std::size_t n) {
                buffer_.commit(
                    boost::asio::buffer_copy(buffer_.prepare(n), boost::asio::buffer(data, n)));
            });
        }

        /** No-op: the buffer is already fully populated by the constructor.
         *
         *  @param ec Unused; not modified.
         */
        void
        init(boost::beast::error_code&) noexcept
        {
        }

        /** Return all serialized bytes in a single call.
         *
         *  Returns `boost::optional` (not `std::optional`) to satisfy the
         *  Beast BodyReader concept. The `bool` element of the pair is `false`,
         *  signaling that all data is available and no further `get()` calls
         *  are needed.
         *
         *  @param ec Unused; not modified.
         *  @return An optional pair of (buffer sequence, more-data flag). The
         *      more-data flag is always `false`.
         */
        boost::optional<std::pair<const_buffers_type, bool>>
        get(boost::beast::error_code& ec)
        {
            return {{buffer_.data(), false}};
        }

        /** No-op: no resources require explicit release.
         *
         *  @param ec Unused; not modified.
         */
        void
        finish(boost::beast::error_code&)
        {
        }
    };

    /** Newer-style Beast BodyReader that serializes a `Json::Value` into a
     *  single `std::string` using `Json::to_string()`.
     *
     *  The constructor takes the HTTP header and body value as separate
     *  arguments (the newer Beast BodyReader interface), rather than the full
     *  message. The entire JSON payload is produced in one allocation at
     *  construction time and exposed as a `boost::asio::const_buffer`.
     */
    class writer  // NOLINT(readability-identifier-naming) -- Boost.Beast body concept name
    {
        std::string body_string_;

    public:
        /** Single contiguous buffer wrapping `body_string_`. */
        using const_buffers_type = boost::asio::const_buffer;

        /** Construct the writer and eagerly serialize `value` to a string.
         *
         *  @tparam IsRequest Whether the enclosing message is a request.
         *  @tparam Fields    The HTTP header fields type.
         *  @param fields     The HTTP header (unused; present to satisfy the
         *      Beast BodyReader concept).
         *  @param value      The JSON value to serialize.
         */
        template <bool IsRequest, class Fields>
        explicit writer(
            boost::beast::http::header<IsRequest, Fields> const& fields,
            value_type const& value)
            : body_string_(to_string(value))
        {
        }

        /** Clear `ec` to signal that initialization succeeded.
         *
         *  Unlike `reader::init()`, this method explicitly clears the error
         *  code rather than leaving it unmodified.
         *
         *  @param ec Set to a zero/success value.
         */
        static void
        init(boost::beast::error_code& ec)
        {
            ec.assign(0, ec.category());
        }

        /** Return the serialized body as a single const buffer.
         *
         *  Returns `boost::optional` (not `std::optional`) to satisfy the
         *  Beast BodyWriter concept. The `bool` element of the pair is `false`,
         *  signaling that all data is available in one shot.
         *
         *  @param ec Set to a zero/success value before returning.
         *  @return An optional pair of (const buffer over `body_string_`,
         *      more-data flag). The more-data flag is always `false`.
         */
        boost::optional<std::pair<const_buffers_type, bool>>
        get(boost::beast::error_code& ec)
        {
            ec.assign(0, ec.category());
            return {{const_buffers_type{body_string_.data(), body_string_.size()}, false}};
        }
    };
};

}  // namespace xrpl
