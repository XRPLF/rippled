#ifndef XRPL_TEST_JTX_SIG_H_INCLUDED
#define XRPL_TEST_JTX_SIG_H_INCLUDED

#include <test/jtx/Env.h>

#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Set the regular signature on a JTx.
    @note For multisign, use msig.
*/
class sig
{
private:
    bool manual_ = true;
    std::optional<Account> account_;

public:
    explicit sig(autofill_t) : manual_(false)
    {
    }

    explicit sig(none_t)
    {
    }

    explicit sig(Account const& account) : account_(account)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
