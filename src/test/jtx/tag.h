#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {

namespace jtx {

/** Set the destination tag on a JTx*/
struct dtag
{
private:
    std::uint32_t value_;

public:
    explicit dtag(std::uint32_t value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

/** Set the source tag on a JTx*/
struct stag
{
private:
    std::uint32_t value_;

public:
    explicit stag(std::uint32_t value) : value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx

}  // namespace test
}  // namespace xrpl
