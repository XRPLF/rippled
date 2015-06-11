//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_TESTS_COMMON_H_INCLUDED
#define RIPPLE_APP_TESTS_COMMON_H_INCLUDED

#include <ripple/protocol/Issue.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/crypto/KeyType.h>
#include <cstdint>
#include <string>

namespace ripple {
namespace test {

namespace detail {

struct XRP_t
{
    XRP_t() = default;

    /** Implicit conversion to Issue.

        This allows passing XRP where
        an Issue is expected.
    */
    operator Issue() const
    {
        return xrpIssue();
    }

    /** Returns an amount of XRP as STAmount

        @param v The number of XRP (not drops)
    */
    STAmount operator()(double v) const;
};

} // detail

/** Converts to XRP Issue or STAmount.

    Examples:
        XRP         Converts to the XRP Issue
        XRP(10)     Returns STAmount of 10 XRP
*/
extern detail::XRP_t XRP;

/** Returns an XRP STAmount.

    Example:
        drops(10)   Returns STAmount of 10 drops
*/
inline
STAmount
drops (std::uint64_t v)
{
    return STAmount(v, false);
}

namespace detail {

struct epsilon_multiple
{
    std::size_t n;
};

} // detail

// The smallest possible IOU STAmount
struct epsilon_t
{
    epsilon_t()
    {
    }

    detail::epsilon_multiple
    operator()(std::size_t n) const
    {
        return { n };
    }
};

static epsilon_t const epsilon;

/** Converts to IOU Issue or STAmount.

    Examples:
        IOU         Converts to the underlying Issue
        IOU(10)     Returns STAmount of 10 of
                        the underlying Issue.
*/
class IOU
{
private:
    Issue issue_;

public:
    IOU(Issue const& issue)
        : issue_(issue)
    {
    }

    /** Implicit conversion to Issue.

        This allows passing an IOU
        value where an Issue is expected.
    */
    operator Issue() const
    {
        return issue_;
    }

    STAmount operator()(double v) const;
    STAmount operator()(epsilon_t) const;
    STAmount operator()(detail::epsilon_multiple) const;

    // VFALCO TODO
    // STAmount operator()(char const* s) const;
};

//------------------------------------------------------------------------------

/** Immutable cryptographic account descriptor. */
class Account
{
private:
    std::string name_;
    // VFALCO TODO use AnyPublicKey, AnySecretKey
    //             instead of RippleAddress
    RippleAddress pk_;
    RippleAddress sk_;
    ripple::Account id_;
    std::string human_; // base58 public key string

public:
    Account() = default;
    Account (Account const&) = default;
    Account& operator= (Account const&) = default;

#ifdef _MSC_VER
    Account (Account&&);
    Account& operator= (Account&&);
#else
    Account (Account&&) = default;
    Account& operator= (Account&&) = default;
#endif

    /** Create an account from a key pair. */
    Account (std::string name, KeyPair&& keys);

    /** Create an account from a simple string name. */
    /** @{ */
    Account (std::string name,
        KeyType type = KeyType::secp256k1);
    Account (char const* name,
        KeyType type = KeyType::secp256k1)
        : Account(std::string(name), type)
    {
    }
    /** @} */

    /** Return the public key. */
    RippleAddress const&
    pk() const
    {
        return pk_;
    }

    /** Return the secret key. */
    RippleAddress const&
    sk() const
    {
        return sk_;
    }

    /** Returns the Account ID.

        The Account ID is the uint160 hash of the public key.
    */
    ripple::Account
    id() const
    {
        return id_;
    }

    /** Returns the human readable public key. */
    std::string const&
    human() const
    {
        return human_;
    }

    /** Implicit conversion to AccountID.

        This allows passing an Account
        where a ripple::Account is expected.
    */
    operator ripple::Account() const
    {
        return id_;
    }

    /** Returns an IOU for the specified gateway currency. */
    IOU
    operator[](std::string const& s) const;

    /** Meet the requirements of StrictWeakOrdering. */
    friend
    bool
    operator< (Account const& lhs, Account const& rhs)
    {
        return lhs.id() < rhs.id();
    }
};

} // test
} // ripple

#endif
