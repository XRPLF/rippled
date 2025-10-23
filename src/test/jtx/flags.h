#ifndef XRPL_TEST_JTX_FLAGS_H_INCLUDED
#define XRPL_TEST_JTX_FLAGS_H_INCLUDED

#include <test/jtx/Env.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {
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
