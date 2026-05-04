#pragma once

#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

/** Set the domain on a JTx. */
class Domain
{
private:
    uint256 v_;

public:
    explicit Domain(uint256 const& v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
