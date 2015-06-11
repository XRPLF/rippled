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

#ifndef RIPPLE_TEST_ENV_H_INCLUDED
#define RIPPLE_TEST_ENV_H_INCLUDED

#include <ripple/test/Account.h>
#include <ripple/test/amounts.h>
#include <ripple/test/JTx.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/json/json_value.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <beast/is_call_possible.h>
#include <beast/unit_test/suite.h>
#include <boost/logic/tribool.hpp>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>
#include <unordered_map>

namespace ripple {
namespace test {

/** A view to an account's account root. */
class AccountInfo
{
private:
    Account account_;
    std::shared_ptr<Ledger> ledger_;
    boost::optional<SLE const> root_;

public:
    AccountInfo(Account const& account,
            std::shared_ptr<Ledger> ledger)
        : account_(account)
        , ledger_(std::move(ledger))
        , root_(ledger_->fetch(
            getAccountRootIndex(account.id())))
    {
    }

    STAmount
    balance (Issue const& issue) const;

    std::uint32_t
    seq() const;

    std::uint32_t
    flags() const;
};

//------------------------------------------------------------------------------

/** A transaction testing environment. */
class Env
{
public:
    beast::unit_test::suite& test;

    /** The master account. */
    Account const master;

    /** The open ledger. */
    std::shared_ptr<Ledger> ledger;

public:
    Env (beast::unit_test::suite& test_);

    /** Associate AccountID with account. */
    void
    memoize (Account const& account);

    /** Returns the Account given the AccountID. */
    /** @{ */
    Account const&
    lookup (std::string const& base58ID) const;

    Account const&
    lookup (ripple::Account const& id) const;
    /** @} */

    /** Returns info on an Account. */
    /** @{ */
    AccountInfo
    info (Account const& account) const
    {
        return AccountInfo(account, ledger);
    }

    AccountInfo
    operator[](Account const& account) const
    {
        return info(account);
    }
    /** @} */

    /** Return an account root.
        @return empty if the account does not exist.
    */
    std::shared_ptr<SLE const>
    le (Account const& account) const
    {
        // VFALCO NOTE This hack should be removed
        //             when fetch returns shared_ptr again
        auto const st = ledger->fetch(
            getAccountRootIndex(account.id()));
        if (! st)
            return nullptr;
        return std::make_shared<SLE const>(*st);
    }

    /** Return a ledger entry.
        @return empty if the ledger entry does not exist
    */
    // VFALCO NOTE Use Keylet here
    std::shared_ptr<SLE const>
    le (uint256 const& key) const
    {
        // VFALCO NOTE This hack should be removed
        //             when fetch returns shared_ptr again
        auto const st = ledger->fetch(key);
        if (! st)
            return nullptr;
        return std::make_shared<SLE const>(*st);
    }

    void auto_fee (bool value)
    {
        fill_fee_ = value;
    }

    void auto_seq (bool value)
    {
        fill_seq_ = value;
    }

    void auto_sig (bool value)
    {
        fill_sig_ = value;
    }

    /** Create a JTx from parameters. */
    template <class JsonValue,
        class... FN>
    JTx
    jt (JsonValue&& jv, FN const&... fN)
    {
        JTx jt(std::forward<JsonValue>(jv));
        invoke(jt, fN...);
        autofill(jt);
        return jt;
    }

    /** Create JSON from parameters.
        This will apply funclets and autofill.
    */
    template <class JsonValue,
        class... FN>
    Json::Value
    json (JsonValue&&jv, FN const&... fN)
    {
        auto tj = jt(
            std::forward<JsonValue>(jv),
                fN...);
        return std::move(tj.jv);
    }

    /** Check a set of requirements.
        
        The requirements are formed
        from condition functors.
    */
    template <class... Args>
    void
    require (Args const&... args) const
    {
        jtx::required(args...)(*this);
    }

    /** Submit an existing JTx.
        This calls postconditions.
    */
    void
    submit (JTx const& tx);

    /** Apply funclets and submit. */
    /** @{ */
    template <class JsonValue, class... FN>
    void
    apply (JsonValue&& jv, FN const&... fN)
    {
        submit(jt(std::forward<
            JsonValue>(jv), fN...));
    }

    template <class JsonValue,
        class... FN>
    void
    operator()(JsonValue&& jv,
        FN const&... fN)
    {
        apply(std::forward<
            JsonValue>(jv), fN...);
    }
    /** @} */

    /** Create a new account with some XRP.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings. The XRP is transferred from the master
        account.
        
        @param amount The amount of XRP to transfer.
    */
    /** @{ */
    void
    fund (STAmount const& amount, Account const& account);

    template<class... Accounts>
    void
    fund (STAmount const& amount, Account const& account0,
        Account const& account1, Accounts const&... accountN)
    {
        fund(amount, account0);
        fund(amount, account1, accountN...);
    }
    /** @} */

    /** Establish trust lines.

        These convenience functions are for easy set-up
        of the environment, they bypass fee, seq, and sig
        settings.
    */
    /** @{ */
    void
    trust (STAmount const& amount,
        Account const& account);

    template<class... Accounts>
    void
    trust (STAmount const& amount, Account const& to0,
        Account const& to1, Accounts const&... toN)
    {
        trust(amount, to0);
        trust(amount, to1, toN...);
    }
    /** @} */

private:
    void
    autofill (JTx& jt);

    inline
    void
    invoke (STTx& stx)
    {
    }

    template <class F>
    inline
    void
    maybe_invoke (STTx& stx, F const& f,
        std::false_type)
    {
    }

    template <class F>
    void
    maybe_invoke (STTx& stx, F const& f,
        std::true_type)
    {
        f(*this, stx);
    }

    // Invoke funclets on stx
    // Note: The STTx may not be modified
    template <class F, class... FN>
    void
    invoke (STTx& stx, F const& f,
        FN const&... fN)
    {
        maybe_invoke(stx, f,
            beast::is_call_possible<F,
                void(Env&, STTx const&)>());
        invoke(stx, fN...);
    }

    inline
    void
    invoke (JTx&)
    {
    }

    template <class F>
    inline
    void
    maybe_invoke (JTx& jt, F const& f,
        std::false_type)
    {
    }

    template <class F>
    void
    maybe_invoke (JTx& jt, F const& f,
        std::true_type)
    {
        f(*this, jt);
    }

    // Invoke funclets on jt
    template <class F, class... FN>
    void
    invoke (JTx& jt, F const& f,
        FN const&... fN)
    {
        maybe_invoke(jt, f,
            beast::is_call_possible<F,
                void(Env&, JTx&)>());
        invoke(jt, fN...);
    }

    // Map of account IDs to Account
    std::unordered_map<
        ripple::Account, Account> map_;

    bool fill_fee_ = true;
    bool fill_seq_ = true;
    bool fill_sig_ = true;
};

} // test
} // ripple

#endif
