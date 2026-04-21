#pragma once

#include <test/jtx/Env.h>

#include <tuple>

namespace xrpl {
namespace test {
namespace jtx {

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class ter
{
private:
    std::optional<TER> v_;

public:
    explicit ter(decltype(std::ignore))
    {
    }

    explicit ter(TER v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.ter = v_;
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
