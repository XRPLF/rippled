#pragma once

#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>

#include <cstdint>

namespace xrpl::test::jtx {

/** Set the destination tag on a JTx*/
struct Dtag
{
private:
    std::uint32_t value_;

public:
    explicit Dtag(std::uint32_t value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

/** Set the source tag on a JTx*/
struct Stag
{
private:
    std::uint32_t value_;

public:
    explicit Stag(std::uint32_t value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace xrpl::test::jtx
