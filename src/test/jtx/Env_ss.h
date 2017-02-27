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

#ifndef RIPPLE_TEST_JTX_ENV_SS_H_INCLUDED
#define RIPPLE_TEST_JTX_ENV_SS_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** A transaction testing environment wrapper.
    Transactions submitted in sign-and-submit mode
    by default.
*/
class Env_ss
{
private:
    Env& env_;

private:

    class SignSubmitRunner
    {
    public:
        SignSubmitRunner(SignSubmitRunner&&) = default;
        SignSubmitRunner& operator= (SignSubmitRunner&&) = delete;

        SignSubmitRunner(Env& env, JTx&& jt)
            : env_(env)
            , jt_(jt)
        {
        }

        void operator()(Json::Value const& params = Json::Value{})
        {
            env_.sign_and_submit(jt_, params);
        }

    private:
        Env& env_;
        JTx const jt_;
    };

public:
    Env_ss (Env_ss const&) = delete;
    Env_ss& operator= (Env_ss const&) = delete;

    Env_ss (Env& env)
        : env_(env)
    {
    }

    template <class JsonValue,
        class... FN>
    SignSubmitRunner
    operator()(JsonValue&& jv, FN const&... fN)
    {
        auto jtx = env_.jt(std::forward<
            JsonValue>(jv), fN...);
        return SignSubmitRunner(env_, std::move(jtx));
    }
};

} // jtx
} // test
} // ripple

#endif
