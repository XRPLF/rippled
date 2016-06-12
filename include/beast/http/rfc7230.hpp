//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_RFC7230_HPP
#define BEAST_HTTP_RFC7230_HPP

#include <beast/http/detail/rfc7230.hpp>

namespace beast {
namespace http {

/** A list of parameters in a HTTP extension field value.

    This container allows iteration of the parameter list
    in a HTTP extension. The parameter list is a series
    of "name = value" pairs with each pair starting with
    a semicolon.

    BNF:
    @code
        param-list  = *( OWS ";" OWS param )
        param       = token OWS "=" OWS ( token / quoted-string )
    @endcode

    If a parsing error is encountered while iterating the string,
    the behavior of the container will be as if a string containing
    only characters up to but excluding the first invalid character
    was used to construct the list.

    @code
    for(auto const& param : param_list{";level=9;no_context_takeover;bits=15"})
    {
        std::cout << ";" << param.first;
        if(! param.second.empty())
            std::cout << "=" << param.second;
        std::cout << "\n";
    }
    @endcode
*/
class param_list
{
    boost::string_ref s_;

public:
    /** The type of each element in the list.

        The first string in the pair is the name of the
        parameter, and the second string in the pair is its value.
    */
    using value_type =
        std::pair<boost::string_ref, boost::string_ref>;

    /// A constant iterator to the list
#if GENERATING_DOCS
    using const_iterator = implementation_defined;
#else
    class const_iterator;
#endif

    /// Default constructor.
    param_list() = default;

    /** Construct a list.

        @param s A string containing the list contents. The string
        must remain valid for the lifetime of the container.
    */
    explicit
    param_list(boost::string_ref const& s)
        : s_(s)
    {
    }

    /// Return a const iterator to the beginning of the list
    const_iterator begin() const;

    /// Return a const iterator to the end of the list
    const_iterator end() const;

    /// Return a const iterator to the beginning of the list
    const_iterator cbegin() const;

    /// Return a const iterator to the end of the list
    const_iterator cend() const;
};

//------------------------------------------------------------------------------

/** A list of extensions in a comma separated HTTP field value.

    This container allows iteration of the extensions in a HTTP
    field value. The extension list is a comma separated list of
    token parameter list pairs.

    BNF:
    @code
        ext-list    = *( "," OWS ) ext *( OWS "," [ OWS ext ] )
        ext         = token param-list
        param-list  = *( OWS ";" OWS param )
        param       = token OWS "=" OWS ( token / quoted-string )
    @endcode

    If a parsing error is encountered while iterating the string,
    the behavior of the container will be as if a string containing
    only characters up to but excluding the first invalid character
    was used to construct the list.

    To use this class, construct with the string to be parsed and
    then use @ref begin and @end, or range-for to iterate each
    item:

    @code
    for(auto const& ext : ext_list{"none, 7z;level=9, zip;no_context_takeover;bits=15"})
    {
        std::cout << ext.first << "\n";
        for(auto const& param : ext.second)
        {
            std::cout << ";" << param.first;
            if(! param.second.empty())
                std::cout << "=" << param.second;
            std::cout << "\n";
        }
    }
    @endcode
*/
class ext_list
{
    using iter_type = boost::string_ref::const_iterator;

    boost::string_ref s_;

public:
    /** The type of each element in the list.

        The first element of the pair is the extension token, and the
        second element of the pair is an iterable container holding the
        extension's name/value parameters.
    */
    using value_type = std::pair<boost::string_ref, param_list>;

    /// A constant iterator to the list
#if GENERATING_DOCS
    using const_iterator = implementation_defined;
#else
    class const_iterator;
#endif

    /** Construct a list.

        @param s A string containing the list contents. The string
        must remain valid for the lifetime of the container.
    */
    explicit
    ext_list(boost::string_ref const& s)
        : s_(s)
    {
    }

    /// Return a const iterator to the beginning of the list
    const_iterator begin() const;

    /// Return a const iterator to the end of the list
    const_iterator end() const;

    /// Return a const iterator to the beginning of the list
    const_iterator cbegin() const;

    /// Return a const iterator to the end of the list
    const_iterator cend() const;

    /** Find a token in the list.

        @param s The token to find. A case-insensitive comparison is used.

        @return An iterator to the matching token, or `end()` if no
        token exists.
    */
    template<class T>
    const_iterator
    find(T const& s);

    /** Return `true` if a token is present in the list.

        @param s The token to find. A case-insensitive comparison is used.
    */
    template<class T>
    bool
    exists(T const& s);
};

//------------------------------------------------------------------------------

/** A list of tokens in a comma separated HTTP field value.

    This container allows iteration of the extensions in a HTTP
    field value. The extension list is a comma separated list of
    token parameter list pairs.

    BNF:
    @code
        token-list  = *( "," OWS ) token *( OWS "," [ OWS ext ] )
    @endcode

    If a parsing error is encountered while iterating the string,
    the behavior of the container will be as if a string containing
    only characters up to but excluding the first invalid character
    was used to construct the list.

    To use this class, construct with the string to be parsed and
    then use @ref begin and @end, or range-for to iterate each
    item:

    @code
    for(auto const& token : token_list{"apple, pear, banana"})
        std::cout << token << "\n";
    @endcode
*/
class token_list
{
    using iter_type = boost::string_ref::const_iterator;

    boost::string_ref s_;

public:
    /** The type of each element in the token list.

        The first element of the pair is the extension token, and the
        second element of the pair is an iterable container holding the
        extension's name/value parameters.
    */
    using value_type = boost::string_ref;

    /// A constant iterator to the list
#if GENERATING_DOCS
    using const_iterator = implementation_defined;
#else
    class const_iterator;
#endif

    /** Construct a list.

        @param s A string containing the list contents. The string
        must remain valid for the lifetime of the container.
    */
    explicit
    token_list(boost::string_ref const& s)
        : s_(s)
    {
    }

    /// Return a const iterator to the beginning of the list
    const_iterator begin() const;

    /// Return a const iterator to the end of the list
    const_iterator end() const;

    /// Return a const iterator to the beginning of the list
    const_iterator cbegin() const;

    /// Return a const iterator to the end of the list
    const_iterator cend() const;

    /** Return `true` if a token is present in the list.

        @param s The token to find. A case-insensitive comparison is used.
    */
    template<class T>
    bool
    exists(T const& s);
};

} // http
} // beast

#include <beast/http/impl/rfc7230.ipp>

#endif

