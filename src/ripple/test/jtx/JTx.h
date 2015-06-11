//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_JTX_H_INCLUDED
#define RIPPLE_TEST_JTX_JTX_H_INCLUDED

#include <ripple/test/jtx/Account.h>
#include <ripple/test/jtx/amounts.h>
#include <ripple/test/jtx/any.h>
#include <ripple/test/jtx/json.h>
#include <ripple/test/jtx/tags.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TxFlags.h>
#include <beast/unit_test/suite.h>
#include <boost/logic/tribool.hpp>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <unordered_map>
#include <vector>

namespace ripple {
namespace test {
namespace jtx {

class Env;

BOOST_TRIBOOL_THIRD_STATE(use_default)

namespace detail {
using require_t = std::function<void(Env const&)>;
using requires_t = std::vector<require_t>;
} // detail

/** Execution context for applying a JSON transaction.
    This augments the transaction with various settings.
*/
struct JTx
{
    Json::Value jv;
    boost::tribool fill_fee = boost::logic::indeterminate;
    boost::tribool fill_seq = boost::logic::indeterminate;
    boost::tribool fill_sig = boost::logic::indeterminate;
    std::function<void(Env&, JTx&)> signer;
    jtx::detail::requires_t requires;
    TER ter = tesSUCCESS;

    JTx() = default;

    JTx (Json::Value&& jv_)
        : jv(std::move(jv_))
    {
    }

    JTx (Json::Value const& jv_)
        : jv(jv_)
    {
    }

    template <class Key>
    Json::Value&
    operator[](Key const& key)
    {
        return jv[key];
    }
};

//------------------------------------------------------------------------------
//
// Utilities
//
//------------------------------------------------------------------------------

/** Set the fee automatically. */
void
fill_fee (Json::Value& jv,
    Ledger const& ledger);

/** Set the sequence number automatically. */
void
fill_seq (Json::Value& jv,
    Ledger const& ledger);

/** Sign automatically.
    @note This only works on accounts with multi-signing off.
*/
void
sign (Json::Value& jv,
    Account const& account);

/** Thrown when parse fails. */
struct parse_error : std::logic_error
{
    template <class String>
    explicit
    parse_error (String const& s)
        : logic_error(s)
    {
    }
};

/** Convert JSON to STObject.
    This throws on failure, the JSON must be correct.
    @note Testing malformed JSON is beyond the scope of
          this set of unit test routines.
*/
STObject
parse (Json::Value const& jv);

//------------------------------------------------------------------------------
//
// Conditions
//
//------------------------------------------------------------------------------

namespace detail {

inline
void
require_args (requires_t& vec)
{
}

template <class Cond, class... Args>
inline
void
require_args (requires_t& vec,
    Cond const& cond, Args const&... args)
{
    vec.push_back(cond);
    require_args(vec, args...);
}

} // detail

// Standalone function composes
// one condition functor from many.
template <class...Args>
detail::require_t
required (Args const&... args)
{
    detail::requires_t vec;
    detail::require_args(vec, args...);
    return [vec](Env const& env)
    {
        for(auto const& f : vec)
            f(env);
    };
}

/** Check a set of conditions.

    The conditions are checked after a JTx is
    applied, and only if the resulting TER
    matches the expected TER.
*/
class require
{
private:
    detail::require_t cond_;

public:
    template<class... Args>
    require(Args const&... args)
        : cond_(required(args...))
    {
    }

    void
    operator()(Env const&, JTx& jt) const
    {
        jt.requires.emplace_back(cond_);
    }
};

//
// Conditions
//

/** A balance matches.

    This allows "none" which means either the account
    doesn't exist (no XRP) or the trust line does not
    exist. If an amount is specified, the SLE must
    exist even if the amount is 0, or else the test
    fails.
*/
class balance
{
private:
    bool none_;
    Account account_;
    STAmount value_;

public:
    balance (Account const& account, none_t,
            Issue const& issue = XRP)
        : account_(account)
        , none_(true)
        , value_(issue)
    {
    }

    balance (Account const& account,
            STAmount const& value)
        : none_(false)
        , account_(account)
        , value_(value)
    {
    }

    void
    operator()(Env const&) const;
};

namespace detail {

class flags_helper
{
protected:
    std::uint32_t mask_;

private:
    inline
    void
    set_args()
    {
    }

    void
    set_args (std::uint32_t flag)
    {
        switch(flag)
        {
        case asfRequireDest:    mask_ |= lsfRequireDestTag; break;
        case asfRequireAuth:    mask_ |= lsfRequireAuth; break;
        case asfDisallowXRP:    mask_ |= lsfDisallowXRP; break;
        case asfDisableMaster:  mask_ |= lsfDisableMaster; break;
        //case asfAccountTxnID: // ???
        case asfNoFreeze:       mask_ |= lsfNoFreeze; break;
        case asfGlobalFreeze:   mask_ |= lsfGlobalFreeze; break;
        case asfDefaultRipple:  mask_ |= lsfDefaultRipple; break;
        default:
        throw std::runtime_error(
            "unknown flag");
        }
    }

    template <class Flag,
        class... Args>
    void
    set_args (std::uint32_t flag,
        Args... args)
    {
        set_args(flag, args...);
    }

protected:
    template <class... Args>
    flags_helper (Args... args)
        : mask_(0)
    {
        set_args(args...);
    }
};

} // detail

/** Certain account flags are set */
class flags : private detail::flags_helper
{
private:
    Account account_;
   
public:
    template <class... Args>
    flags (Account const& account,
            Args... args)
        : flags_helper(args...)
        , account_(account)
    {
    }

    void
    operator()(Env const& env) const;
};

/** Certain account flags are clear */
class nflags : private detail::flags_helper
{
private:
    Account account_;
   
public:
    template <class... Args>
    nflags (Account const& account,
            Args... args)
        : flags_helper(args...)
        , account_(account)
    {
    }

    void
    operator()(Env const& env) const;
};

} // jtx
} // test
} // ripple

#endif
