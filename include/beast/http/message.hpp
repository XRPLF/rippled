//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_MESSAGE_HPP
#define BEAST_HTTP_MESSAGE_HPP

#include <beast/http/fields.hpp>
#include <beast/core/detail/integer_sequence.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace beast {
namespace http {

#if GENERATING_DOCS
/** A container for a HTTP request or response header.

    A header includes the Start Line and Fields.

    Some use-cases:

    @li When the message has no body, such as a response to a HEAD request.

    @li When the caller wishes to defer instantiation of the body.

    @li Invoke algorithms which operate on the header only.
*/
template<bool isRequest, class Fields>
struct header

#else
template<bool isRequest, class Fields>
struct header;

template<class Fields>
struct header<true, Fields>
#endif
{
    /// Indicates if the header is a request or response.
#if GENERATING_DOCS
    static bool constexpr is_request = isRequest;

#else
    static bool constexpr is_request = true;
#endif

    /// The type representing the fields.
    using fields_type = Fields;

    /** The HTTP version.

        This holds both the major and minor version numbers,
        using these formulas:
        @code
            major = version / 10;
            minor = version % 10;
        @endcode
    */
    int version;

    /** The Request Method

        @note This field is present only if `isRequest == true`.
    */
    std::string method;

    /** The Request URI

        @note This field is present only if `isRequest == true`.
    */
    std::string url;

    /// The HTTP field values.
    fields_type fields;

    /// Default constructor
    header() = default;

    /// Move constructor
    header(header&&) = default;

    /// Copy constructor
    header(header const&) = default;

    /// Move assignment
    header& operator=(header&&) = default;

    /// Copy assignment
    header& operator=(header const&) = default;

    /** Construct the header.

        All arguments are forwarded to the constructor
        of the `fields` member.

        @note This constructor participates in overload resolution
        if and only if the first parameter is not convertible to
        `header`.
    */
#if GENERATING_DOCS
    template<class... Args>
    explicit
    header(Args&&... args);

#else
    template<class Arg1, class... ArgN,
        class = typename std::enable_if<
            (sizeof...(ArgN) > 0) || ! std::is_convertible<
                typename std::decay<Arg1>::type,
                    header>::value>::type>
    explicit
    header(Arg1&& arg1, ArgN&&... argn)
        : fields(std::forward<Arg1>(arg1),
            std::forward<ArgN>(argn)...)
    {
    }
};

/** A container for a HTTP request or response header.

    A header includes the Start Line and Fields.

    Some use-cases:

    @li When the message has no body, such as a response to a HEAD request.

    @li When the caller wishes to defer instantiation of the body.

    @li Invoke algorithms which operate on the header only.
*/
template<class Fields>
struct header<false, Fields>
{
    /// Indicates if the header is a request or response.
    static bool constexpr is_request = false;

    /// The type representing the fields.
    using fields_type = Fields;

    /** The HTTP version.

        This holds both the major and minor version numbers,
        using these formulas:
        @code
            major = version / 10;
            minor = version % 10;
        @endcode
    */
    int version;

    /// The HTTP field values.
    fields_type fields;

    /// Default constructor
    header() = default;

    /// Move constructor
    header(header&&) = default;

    /// Copy constructor
    header(header const&) = default;

    /// Move assignment
    header& operator=(header&&) = default;

    /// Copy assignment
    header& operator=(header const&) = default;

    /** Construct the header.

        All arguments are forwarded to the constructor
        of the `fields` member.

        @note This constructor participates in overload resolution
        if and only if the first parameter is not convertible to
        `header`.
    */
    template<class Arg1, class... ArgN,
        class = typename std::enable_if<
            (sizeof...(ArgN) > 0) || ! std::is_convertible<
                typename std::decay<Arg1>::type,
                    header>::value>::type>
    explicit
    header(Arg1&& arg1, ArgN&&... argn)
        : fields(std::forward<Arg1>(arg1),
            std::forward<ArgN>(argn)...)
    {
    }
#endif

    /** The Response Status-Code.

        @note This field is present only if `isRequest == false`.
    */
    int status;

    /** The Response Reason-Phrase.

        The Reason-Phrase is obsolete as of rfc7230.

        @note This field is present only if `isRequest == false`.
    */
    std::string reason;
};

/** A container for a complete HTTP message.

    A message can be a request or response, depending on the
    `isRequest` template argument value. Requests and responses
    have different types; functions may be overloaded based on
    the type if desired.

    The `Body` template argument type determines the model used
    to read or write the content body of the message.

    @tparam isRequest `true` if this represents a request,
    or `false` if this represents a response. Some class data
    members are conditionally present depending on this value.

    @tparam Body A type meeting the requirements of Body.

    @tparam Fields The type of container used to hold the
    field value pairs.
*/
template<bool isRequest, class Body, class Fields>
struct message : header<isRequest, Fields>
{
    /// The base class used to hold the header portion of the message.
    using base_type = header<isRequest, Fields>;

    /** The type providing the body traits.

        The @ref message::body member will be of type `body_type::value_type`.
    */
    using body_type = Body;

    /// A value representing the body.
    typename Body::value_type body;

    /// Default constructor
    message() = default;

    /// Move constructor
    message(message&&) = default;

    /// Copy constructor
    message(message const&) = default;

    /// Move assignment
    message& operator=(message&&) = default;

    /// Copy assignment
    message& operator=(message const&) = default;

    /** Construct a message from a header.

        Additional arguments, if any, are forwarded to
        the constructor of the body member.
    */
    template<class... Args>
    explicit
    message(base_type&& base, Args&&... args)
        : base_type(std::move(base))
        , body(std::forward<Args>(args)...)
    {
    }

    /** Construct a message from a header.

        Additional arguments, if any, are forwarded to
        the constructor of the body member.
    */
    template<class... Args>
    explicit
    message(base_type const& base, Args&&... args)
        : base_type(base)
        , body(std::forward<Args>(args)...)
    {
    }

    /** Construct a message.

        @param u An argument forwarded to the body constructor.

        @note This constructor participates in overload resolution
        only if `u` is not convertible to `base_type`.
    */
    template<class U
#if ! GENERATING_DOCS
        , class = typename std::enable_if<
            ! std::is_convertible<typename
                std::decay<U>::type, base_type>::value>::type
#endif
    >
    explicit
    message(U&& u)
        : body(std::forward<U>(u))
    {
    }

    /** Construct a message.

        @param u An argument forwarded to the body constructor.

        @param v An argument forwarded to the fields constructor.

        @note This constructor participates in overload resolution
        only if `u` is not convertible to `base_type`.
    */
    template<class U, class V
#if ! GENERATING_DOCS
        ,class = typename std::enable_if<! std::is_convertible<
            typename std::decay<U>::type, base_type>::value>::type
#endif
    >
    message(U&& u, V&& v)
        : base_type(std::forward<V>(v))
        , body(std::forward<U>(u))
    {
    }

    /** Construct a message.

        @param un A tuple forwarded as a parameter pack to the body constructor.
    */
    template<class... Un>
    message(std::piecewise_construct_t, std::tuple<Un...> un)
        : message(std::piecewise_construct, un,
            beast::detail::make_index_sequence<sizeof...(Un)>{})
    {
    }

    /** Construct a message.

        @param un A tuple forwarded as a parameter pack to the body constructor.

        @param vn A tuple forwarded as a parameter pack to the fields constructor.
    */
    template<class... Un, class... Vn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>&& un, std::tuple<Vn...>&& vn)
        : message(std::piecewise_construct, un, vn,
            beast::detail::make_index_sequence<sizeof...(Un)>{},
            beast::detail::make_index_sequence<sizeof...(Vn)>{})
    {
    }

    /// Returns the header portion of the message
    base_type&
    base()
    {
        return *this;
    }

    /// Returns the header portion of the message
    base_type const&
    base() const
    {
        return *this;
    }

private:
    template<class... Un, size_t... IUn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>& tu, beast::detail::index_sequence<IUn...>)
        : body(std::forward<Un>(std::get<IUn>(tu))...)
    {
    }

    template<class... Un, class... Vn,
        std::size_t... IUn, std::size_t... IVn>
    message(std::piecewise_construct_t,
            std::tuple<Un...>& tu, std::tuple<Vn...>& tv,
                beast::detail::index_sequence<IUn...>,
                    beast::detail::index_sequence<IVn...>)
        : base_type(std::forward<Vn>(std::get<IVn>(tv))...)
        , body(std::forward<Un>(std::get<IUn>(tu))...)
    {
    }
};

//------------------------------------------------------------------------------

#if GENERATING_DOCS
/** Swap two header objects.

    @par Requirements
    `Fields` is @b Swappable.
*/
template<bool isRequest, class Fields>
void
swap(
    header<isRequest, Fields>& m1,
    header<isRequest, Fields>& m2);
#endif

/** Swap two message objects.

    @par Requirements:
    `Body::value_type` and `Fields` are @b Swappable.
*/
template<bool isRequest, class Body, class Fields>
void
swap(
    message<isRequest, Body, Fields>& m1,
    message<isRequest, Body, Fields>& m2);

/// A typical HTTP request header
using request_header = header<true, fields>;

/// Typical HTTP response header
using response_header = header<false, fields>;

/// A typical HTTP request
template<class Body, class Fields = fields>
using request = message<true, Body, Fields>;

/// A typical HTTP response
template<class Body, class Fields = fields>
using response = message<false, Body, Fields>;

//------------------------------------------------------------------------------

/** Returns `true` if the HTTP/1 message indicates a keep alive.

    Undefined behavior if version is greater than 11.
*/
template<bool isRequest, class Fields>
bool
is_keep_alive(header<isRequest, Fields> const& msg);

/** Returns `true` if the HTTP/1 message indicates an Upgrade request or response.

    Undefined behavior if version is greater than 11.
*/
template<bool isRequest, class Fields>
bool
is_upgrade(header<isRequest, Fields> const& msg);

/** HTTP/1 connection prepare options.

    @note These values are used with @ref prepare.
*/
enum class connection
{
    /// Specify Connection: close.
    close,

    /// Specify Connection: keep-alive where possible.
    keep_alive,

    /// Specify Connection: upgrade.
    upgrade
};

/** Prepare a HTTP message.

    This function will adjust the Content-Length, Transfer-Encoding,
    and Connection fields of the message based on the properties of
    the body and the options passed in.

    @param msg The message to prepare. The fields may be modified.

    @param options A list of prepare options.
*/
template<
    bool isRequest, class Body, class Fields,
    class... Options>
void
prepare(message<isRequest, Body, Fields>& msg,
    Options&&... options);

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
