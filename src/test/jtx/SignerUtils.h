#ifndef XRPL_TEST_JTX_SIGNERUTILS_H_INCLUDED
#define XRPL_TEST_JTX_SIGNERUTILS_H_INCLUDED

#include <test/jtx/Account.h>

#include <optional>
#include <vector>

namespace xrpl {
namespace test {
namespace jtx {

struct Reg
{
    Account acct;
    std::optional<Account> sig;
    std::vector<std::shared_ptr<Reg>> nested;  // For nested signers

    Reg(Account const& masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(Account const& acct_, Account const& regularSig) : acct(acct_), sig(regularSig)
    {
    }

    Reg(char const* masterSig) : acct(masterSig), sig(masterSig)
    {
    }

    Reg(char const* acct_, char const* regularSig) : acct(acct_), sig(regularSig)
    {
    }

    // Nested signer constructor
    Reg(Account const& acct_, std::vector<std::shared_ptr<Reg>> nested_) : acct(acct_), nested(std::move(nested_))
    {
    }

    bool
    isNested() const
    {
        return !nested.empty();
    }

    AccountID
    id() const
    {
        return acct.id();
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
    std::sort(signers.begin(), signers.end(), [](Reg const& lhs, Reg const& rhs) { return lhs.acct < rhs.acct; });
}

inline void
sortSigners(std::vector<std::shared_ptr<Reg>>& signers)
{
    std::sort(signers.begin(), signers.end(), [](std::shared_ptr<Reg> const& lhs, std::shared_ptr<Reg> const& rhs) {
        return lhs->acct < rhs->acct;
    });
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl

#endif
