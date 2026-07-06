#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <stdexcept>
#include <utility>

namespace xrpl {
namespace detail {

class FlagsHelper
{
protected:
    std::uint32_t mask_{0};

private:
    void
    setArgs(std::uint32_t flag)
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
            case asfDisallowIncomingCheck:
                mask_ |= lsfDisallowIncomingCheck;
                break;
            case asfDisallowIncomingNFTokenOffer:
                mask_ |= lsfDisallowIncomingNFTokenOffer;
                break;
            case asfDisallowIncomingPayChan:
                mask_ |= lsfDisallowIncomingPayChan;
                break;
            case asfDisallowIncomingTrustline:
                mask_ |= lsfDisallowIncomingTrustline;
                break;
            case asfAllowTrustLineLocking:
                mask_ |= lsfAllowTrustLineLocking;
                break;
            default:
                Throw<std::runtime_error>("unknown flag");
        }
    }

    template <class Flag, class... Args>
    void
    setArgs(std::uint32_t flag, Args... args)
    {
        setArgs(flag);
        if constexpr (sizeof...(args))
            setArgs(args...);
    }

protected:
    template <class... Args>
    FlagsHelper(Args... args)
    {
        setArgs(args...);
    }
};

}  // namespace detail

namespace test::jtx {

// JSON generators

/** Add and/or remove flag. */
json::Value
fset(Account const& account, std::uint32_t on, std::uint32_t off = 0);

/** Remove account flag. */
inline json::Value
fclear(Account const& account, std::uint32_t off)
{
    return fset(account, 0, off);
}

/** Match set account flags */
class Flags : private xrpl::detail::FlagsHelper
{
private:
    Account account_;

public:
    template <class... Args>
    Flags(Account account, Args... args) : FlagsHelper(args...), account_(std::move(account))
    {
    }

    void
    operator()(Env& env) const;
};

/** Match clear account flags */
class Nflags : private xrpl::detail::FlagsHelper
{
private:
    Account account_;

public:
    template <class... Args>
    Nflags(Account account, Args... args) : FlagsHelper(args...), account_(std::move(account))
    {
    }

    void
    operator()(Env& env) const;
};

}  // namespace test::jtx

}  // namespace xrpl
