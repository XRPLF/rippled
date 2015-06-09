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

#ifndef RIPPLE_APP_TESTS_JTX_H_INCLUDED
#define RIPPLE_APP_TESTS_JTX_H_INCLUDED

#include <ripple/app/tests/Common.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <beast/unit_test/suite.h>
#include <boost/logic/tribool.hpp>
#include <map>
#include <set>
#include <utility>
#include <unordered_map>
#include <vector>

namespace ripple {
namespace test {

class Env;

BOOST_TRIBOOL_THIRD_STATE(use_default)

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

namespace jtx {

struct none_t { none_t() { } };
static none_t const none;

struct autofill_t { autofill_t() { } };
static autofill_t const autofill;

struct disabled_t { disabled_t() { } };
static disabled_t const disabled;

struct MaybeAnyAmount;

struct any_t
{
    any_t() { }

    inline
    MaybeAnyAmount
    operator()(STAmount const& sta) const;
};

// This wrapper helps pay destinations
// in their own issue using generic syntax
struct MaybeAnyAmount
{
    bool is_any;
    STAmount value;

    MaybeAnyAmount() = delete;
    MaybeAnyAmount (MaybeAnyAmount const&) = default;
    MaybeAnyAmount& operator= (MaybeAnyAmount const&) = default;

    MaybeAnyAmount (STAmount const& amount)
        : is_any(false)
        , value(amount)
    {
    }

    MaybeAnyAmount (STAmount const& amount,
            any_t const*)
        : is_any(true)
        , value(amount)
    {
    }

    // Reset the issue to a specific account
    void
    to (ripple::Account const& id)
    {
        if (! is_any)
            return;
        value.setIssuer(id);
    }
};

inline
MaybeAnyAmount
any_t::operator()(STAmount const& sta) const
{
    return MaybeAnyAmount(sta, this);
}

/** Returns an amount representing "any issuer"
    @note With respect to what the recipient will accept
*/
static any_t const any;

//
// JSON generators
//

/** Create a payment. */
Json::Value
pay (Account const& account,
    Account const& to,
        MaybeAnyAmount amount);

/** Create an offer. */
Json::Value
offer (Account const& account,
    STAmount const& in, STAmount const& out);

/** Set a transfer rate. */
Json::Value
rate (Account const& account,
    double multiplier);

/** Disable the regular key. */
Json::Value
regkey (Account const& account,
    disabled_t);

/** Set a regular key. */
Json::Value
regkey (Account const& account,
    Account const& signer);

/** Add and/or remove flag. */
Json::Value
set (Account const& account,
    std::uint32_t on, std::uint32_t off = 0);

/** Remove account flag. */
inline
Json::Value
clear (Account const& account,
    std::uint32_t off)
{
    return set(account, 0, off);
}

/** The null transaction. */
inline
Json::Value
noop (Account const& account)
{
    return set(account, 0);
}

struct signer
{
    std::uint32_t weight;
    Account account;

    signer (Account account_,
            std::uint32_t weight_ = 1)
        : weight(weight_)
        , account(std::move(account_))
    {
    }
};

Json::Value
signers (Account const& account,
    std::uint32_t quorum,
        std::vector<signer> const& v);

/** Remove a signer list. */
Json::Value
signers (Account const& account, none_t);

/** Modify a trust line. */
Json::Value
trust (Account const& account,
    STAmount const& amount);

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

//
// Funclets
//

/** Set the fee on a JTx. */
class fee
{
private:
    STAmount v_;
    boost::tribool b_ =
        boost::logic::indeterminate;

public:
    explicit
    fee (autofill_t)
        : b_(true)
    {
    }

    explicit
    fee (none_t)
        : b_(false)
    {
    }

    explicit
    fee (STAmount const& v)
        : v_(v)
    {
        if (! isXRP(v_))
            throw std::runtime_error(
                "fee: not XRP");
    }

    void
    operator()(Env const&, JTx& jt) const;
};

/** Set the flags on a JTx. */
class flags
{
private:
    std::uint32_t v_;

public:
    explicit
    flags (std::uint32_t v)
        : v_(v)
    {
    }

    void
    operator()(Env const&, JTx& jt) const;
};

/** Set Paths, SendMax on a JTx. */
class paths
{
private:
    Issue in_;
    int depth_;
    unsigned int limit_;

public:
    paths (Issue const& in,
            int depth = 7, unsigned int limit = 4)
        : in_(in)
        , depth_(depth)
        , limit_(limit)
    {
    }

    void
    operator()(Env const&, JTx& jtx) const;
};


/** Sets the SendMax on a JTx. */
class sendmax
{
private:
    STAmount amount_;

public:
    sendmax (STAmount const& amount)
        : amount_(amount)
    {
    }

    void
    operator()(Env const&, JTx& jtx) const;
};

/** Set a multisignature on a JTx. */
class msig
{
private:
    std::vector<Account> accounts_;

public:
    msig (std::vector<Account> accounts)
        : accounts_(std::move(accounts))
    {
    }

    template <class AccountType, class... Accounts>
    msig (AccountType&& a0, Accounts&&... aN)
        : msig(make_vector(
            std::forward<AccountType>(a0),
                std::forward<Accounts>(aN)...))
    {
    }

    void
    operator()(Env const&, JTx& jt) const;

private:
    template <class AccountType>
    static
    void
    helper (std::vector<Account>& v,
        AccountType&& account)
    {
        v.emplace_back(std::forward<
            Account>(account));
    }

    template <class AccountType, class... Accounts>
    static
    void
    helper (std::vector<Account>& v,
        AccountType&& a0, Accounts&&... aN)
    {
        helper(v, std::forward<AccountType>(a0));
        helper(v, std::forward<Accounts>(aN)...);
    }

    template <class... Accounts>
    static
    std::vector<Account>
    make_vector(Accounts&&... accounts)
    {
        std::vector<Account> v;
        v.reserve(sizeof...(accounts));
        helper(v, std::forward<
            Accounts>(accounts)...);
        return v;
    }
};

/** Set a multisignature on a JTx. */
class msig2_t
{
private:
    std::map<Account,
        std::set<Account>> sigs_;

public:
    msig2_t (std::vector<std::pair<
        Account, Account>> sigs);

    void
    operator()(Env const&, JTx& jt) const;
};

inline
msig2_t
msig2 (std::vector<std::pair<
    Account, Account>> sigs)
{
    return msig2_t(std::move(sigs));
}

/** Set the sequence number on a JTx. */
struct seq
{
private:
    std::uint32_t v_;
    boost::tribool b_ =
        boost::logic::indeterminate;

public:
    explicit
    seq (autofill_t)
        : b_(true)
    {
    }

    explicit
    seq (none_t)
        : b_(false)
    {
    }

    explicit
    seq (std::uint32_t v)
        : v_(v)
    {
    }

    void
    operator()(Env const&, JTx& jt) const;
};

/** Set the regular signature on a JTx.
    @note For multisign, use msig.
*/
class sig
{
private:
    Account account_;
    boost::tribool b_ =
        boost::logic::indeterminate;

public:
    explicit
    sig (autofill_t)
        : b_(true)
    {
    }

    explicit
    sig (none_t)
        : b_(false)
    {
    }

    explicit
    sig (Account const& account)
        : account_(account)
    {
    }

    void
    operator()(Env const&, JTx& jt) const;
};

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class ter
{
private:
    TER v_;

public:
    explicit
    ter (TER v)
        : v_(v)
    {
    }

    void
    operator()(Env const&, JTx& jt) const
    {
        jt.ter = v_;
    }
};

} // jtx

} // test
} // ripple

#endif
