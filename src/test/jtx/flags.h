#pragma once

#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

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
            case kASF_REQUIRE_DEST:
                mask_ |= LsfRequireDestTag;
                break;
            case kASF_REQUIRE_AUTH:
                mask_ |= LsfRequireAuth;
                break;
            case kASF_DISALLOW_XRP:
                mask_ |= LsfDisallowXrp;
                break;
            case kASF_DISABLE_MASTER:
                mask_ |= LsfDisableMaster;
                break;
            // case asfAccountTxnID: // ???
            case kASF_NO_FREEZE:
                mask_ |= LsfNoFreeze;
                break;
            case kASF_GLOBAL_FREEZE:
                mask_ |= LsfGlobalFreeze;
                break;
            case kASF_DEFAULT_RIPPLE:
                mask_ |= LsfDefaultRipple;
                break;
            case kASF_DEPOSIT_AUTH:
                mask_ |= LsfDepositAuth;
                break;
            case kASF_ALLOW_TRUST_LINE_CLAWBACK:
                mask_ |= LsfAllowTrustLineClawback;
                break;
            case kASF_DISALLOW_INCOMING_CHECK:
                mask_ |= LsfDisallowIncomingCheck;
                break;
            case kASF_DISALLOW_INCOMING_NF_TOKEN_OFFER:
                mask_ |= LsfDisallowIncomingNfTokenOffer;
                break;
            case kASF_DISALLOW_INCOMING_PAY_CHAN:
                mask_ |= LsfDisallowIncomingPayChan;
                break;
            case kASF_DISALLOW_INCOMING_TRUSTLINE:
                mask_ |= LsfDisallowIncomingTrustline;
                break;
            case kASF_ALLOW_TRUST_LINE_LOCKING:
                mask_ |= LsfAllowTrustLineLocking;
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
            set_args(args...);
    }

protected:
    template <class... Args>
    FlagsHelper(Args... args)
    {
        set_args(args...);
    }
};

}  // namespace detail

namespace test::jtx {

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
