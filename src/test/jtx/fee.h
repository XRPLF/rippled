#ifndef XRPL_TEST_JTX_FEE_H_INCLUDED
#define XRPL_TEST_JTX_FEE_H_INCLUDED

#include <test/jtx/Env.h>
#include <test/jtx/tags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/STAmount.h>

#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Set the fee on a JTx. */
class fee
{
private:
    bool manual_ = true;
    bool increment_ = false;
    std::optional<STAmount> amount_;

public:
    explicit fee(autofill_t) : manual_(false)
    {
    }

    explicit fee(increment_t) : increment_(true)
    {
    }

    explicit fee(none_t)
    {
    }

    explicit fee(STAmount const& amount) : amount_(amount)
    {
        if (!isXRP(*amount_))
            Throw<std::runtime_error>("fee: not XRP");
    }

    explicit fee(std::uint64_t amount, bool negative = false)
        : fee{STAmount{amount, negative}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
