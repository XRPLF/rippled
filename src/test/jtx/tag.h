#ifndef XRPL_TEST_JTX_TAG_H_INCLUDED
#define XRPL_TEST_JTX_TAG_H_INCLUDED

#include <test/jtx/Env.h>

namespace ripple {
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
}  // namespace ripple

#endif
