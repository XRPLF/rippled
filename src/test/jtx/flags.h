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

#ifndef RIPPLE_TEST_JTX_FLAGS_H_INCLUDED
#define RIPPLE_TEST_JTX_FLAGS_H_INCLUDED

#include <ripple/basics/contract.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/TxFlags.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

// JSON generators

/** Add and/or remove flag. */
Json::Value
fset(Account const& account, std::uint32_t on, std::uint32_t off = 0);

/** Remove account flag. */
inline Json::Value
fclear(Account const& account, std::uint32_t off)
{
    return fset(account, 0, off);
}

namespace detail {

class flags_helper
{
protected:
    std::uint32_t mask_;

private:
    void
    set_args(std::uint32_t flag)
    {
        switch (flag)
        {
            case asfRequireDest:
                mask_ |= lsfRequireDestTag;
                break;
            case asfRequireAuth:
                mask_ |= lsfRequireAuth;
                break;
            case asfDisallowXRP:
                mask_ |= lsfDisallowXRP;
                break;
            case asfDisableMaster:
                mask_ |= lsfDisableMaster;
                break;
            // case asfAccountTxnID: // ???
            case asfNoFreeze:
                mask_ |= lsfNoFreeze;
                break;
            case asfGlobalFreeze:
                mask_ |= lsfGlobalFreeze;
                break;
            case asfDefaultRipple:
                mask_ |= lsfDefaultRipple;
                break;
            case asfDepositAuth:
                mask_ |= lsfDepositAuth;
                break;
            case asfAllowTrustLineClawback:
                mask_ |= lsfAllowTrustLineClawback;
                break;
            default:
                Throw<std::runtime_error>("unknown flag");
        }
    }

    template <class Flag, class... Args>
    void
    set_args(std::uint32_t flag, Args... args)
    {
        set_args(flag);
        if constexpr (sizeof...(args))
            set_args(args...);
    }

protected:
    template <class... Args>
    flags_helper(Args... args) : mask_(0)
    {
        set_args(args...);
    }
};

}  // namespace detail

/** Match set account flags */
class flags : private detail::flags_helper
{
private:
    Account account_;

public:
    template <class... Args>
    flags(Account const& account, Args... args)
        : flags_helper(args...), account_(account)
    {
    }

    void
    operator()(Env& env) const;
};

/** Match clear account flags */
class nflags : private detail::flags_helper
{
private:
    Account account_;

public:
    template <class... Args>
    nflags(Account const& account, Args... args)
        : flags_helper(args...), account_(account)
    {
    }

    void
    operator()(Env& env) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
