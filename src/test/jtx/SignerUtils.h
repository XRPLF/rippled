#ifndef XRPL_TEST_JTX_SIGNERUTILS_H_INCLUDED
#define XRPL_TEST_JTX_SIGNERUTILS_H_INCLUDED

#include <test/jtx/Account.h>

#include <vector>

namespace ripple {
namespace test {
namespace jtx {

struct Reg
{
    Account acct;
    Account sig;

    Reg(Account const& masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(Account const& acct_, Account const& regularSig)
        : acct(acct_), sig(regularSig)
    {
    }

    Reg(char const* masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(char const* acct_, char const* regularSig)
        : acct(acct_), sig(regularSig)
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
    std::sort(
        signers.begin(), signers.end(), [](Reg const& lhs, Reg const& rhs) {
            return lhs.acct < rhs.acct;
        });
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
