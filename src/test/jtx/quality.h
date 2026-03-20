#pragma once

#include <test/jtx/Env.h>

namespace xrpl {
namespace test {
namespace jtx {

/** Sets the literal QualityIn on a trust JTx. */
class QualityIn
{
private:
    std::uint32_t qIn_;

public:
    explicit QualityIn(std::uint32_t qIn) : qIn_(qIn)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityIn on a trust JTx. */
class QualityInPercent
{
private:
    std::uint32_t qIn_;

public:
    explicit QualityInPercent(double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the literal QualityOut on a trust JTx. */
class QualityOut
{
private:
    std::uint32_t qOut_;

public:
    explicit QualityOut(std::uint32_t qOut) : qOut_(qOut)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityOut on a trust JTx as a percentage. */
class QualityOutPercent
{
private:
    std::uint32_t qOut_;

public:
    explicit QualityOutPercent(double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
