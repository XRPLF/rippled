#pragma once

#include <test/jtx/Account.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

struct Reg
{
    Account acct;
    Account sig;

    Reg(Account const& masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(Account acct, Account regularSig) : acct(std::move(acct)), sig(std::move(regularSig))
    {
    }

    Reg(char const* masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(char const* acct, char const* regularSig) : acct(acct), sig(regularSig)
    {
    }

    bool
    operator<(Reg const& rhs) const
    {
        return acct < rhs.acct;
    }
};

// Utility function to sort signers
inline void
sortSigners(std::vector<Reg>& signers)
{
    std::ranges::sort(signers, [](Reg const& lhs, Reg const& rhs) { return lhs.acct < rhs.acct; });
}

}  // namespace xrpl::test::jtx
