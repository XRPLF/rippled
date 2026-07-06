#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/tags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <optional>
#include <stdexcept>

namespace xrpl::test::jtx {

/** Set the fee on a JTx. */
class Fee
{
private:
    bool manual_ = true;
    bool increment_ = false;
    std::optional<STAmount> amount_;

public:
    explicit Fee(AutofillT) : manual_(false)
    {
    }

    explicit Fee(IncrementT) : increment_(true)
    {
    }

    explicit Fee(NoneT)
    {
    }

    explicit Fee(STAmount const& amount) : amount_(amount)
    {
        if (!isXRP(*amount_))
            Throw<std::runtime_error>("fee: not XRP");
    }

    explicit Fee(std::uint64_t amount, bool negative = false) : Fee{STAmount{amount, negative}}
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
