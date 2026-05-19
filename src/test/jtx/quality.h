#pragma once

#include <test/jtx/Env.h>

namespace xrpl::test::jtx {

/** Sets the literal QualityIn on a trust JTx. */
class qualityIn
{
private:
    std::uint32_t qIn_;

public:
    explicit qualityIn(std::uint32_t qIn) : qIn_(qIn)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityIn on a trust JTx. */
class qualityInPercent
{
private:
    std::uint32_t qIn_;  // NOLINT(cppcoreguidelines-use-default-member-init)

public:
    explicit qualityInPercent(double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the literal QualityOut on a trust JTx. */
class qualityOut
{
private:
    std::uint32_t qOut_;

public:
    explicit qualityOut(std::uint32_t qOut) : qOut_(qOut)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the QualityOut on a trust JTx as a percentage. */
class qualityOutPercent
{
private:
    std::uint32_t qOut_;  // NOLINT(cppcoreguidelines-use-default-member-init)

public:
    explicit qualityOutPercent(double percent);

    void
    operator()(Env&, JTx& jtx) const;
};

}  // namespace xrpl::test::jtx
