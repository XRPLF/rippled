#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/tags.h>

#include <cstdint>
#include <optional>

namespace xrpl::test::jtx {

/** Set the sequence number on a JTx. */
struct Seq
{
private:
    bool manual_ = true;
    std::optional<std::uint32_t> num_;

public:
    explicit Seq(AutofillT) : manual_(false)
    {
    }

    explicit Seq(NoneT)
    {
    }

    explicit Seq(std::uint32_t num) : num_(num)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
