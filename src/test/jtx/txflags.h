#ifndef XRPL_TEST_JTX_TXFLAGS_H_INCLUDED
#define XRPL_TEST_JTX_TXFLAGS_H_INCLUDED

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Set the flags on a JTx. */
class txflags
{
private:
    std::uint32_t v_;

public:
    explicit txflags(std::uint32_t v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl

#endif
