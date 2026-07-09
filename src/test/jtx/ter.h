#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <xrpl/protocol/TER.h>

#include <optional>
#include <tuple>

namespace xrpl::test::jtx {

/** Set the expected result code for a JTx
    The test will fail if the code doesn't match.
*/
class Ter
{
private:
    std::optional<TER> v_;

public:
    explicit Ter(decltype(std::ignore))
    {
    }

    explicit Ter(TER v) : v_(v)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.ter = v_;
    }
};

}  // namespace xrpl::test::jtx
