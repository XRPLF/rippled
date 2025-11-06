#pragma once

#include <test/jtx/Env.h>

namespace ripple {
namespace test {
namespace jtx {

/** Set the domain on a JTx. */
class domain
{
private:
    uint256 v_;

public:
    explicit domain(uint256 const& v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple
