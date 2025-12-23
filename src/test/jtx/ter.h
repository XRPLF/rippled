#ifndef XRPL_TEST_JTX_TER_H_INCLUDED
#define XRPL_TEST_JTX_TER_H_INCLUDED

#include <test/jtx/Env.h>

#include <tuple>

namespace ripple {
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
}  // namespace ripple

#endif
