#ifndef XRPL_TEST_JTX_SEQ_H_INCLUDED
#define XRPL_TEST_JTX_SEQ_H_INCLUDED

#include <test/jtx/Env.h>
#include <test/jtx/tags.h>

#include <optional>

namespace ripple {
namespace test {
namespace jtx {

/** Set the sequence number on a JTx. */
struct seq
{
private:
    bool manual_ = true;
    std::optional<std::uint32_t> num_;

public:
    explicit seq(autofill_t) : manual_(false)
    {
    }

    explicit seq(none_t)
    {
    }

    explicit seq(std::uint32_t num) : num_(num)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
