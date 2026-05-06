#pragma once

#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

/** Set the flags on a JTx. */
class Txflags
{
private:
    std::uint32_t v_;

public:
    explicit Txflags(std::uint32_t v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
