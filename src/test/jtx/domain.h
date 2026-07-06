#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/basics/base_uint.h>

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
