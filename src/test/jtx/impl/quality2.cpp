#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/quality.h>

#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>

#include <cassert>
#include <cstdint>

namespace xrpl::test::jtx {

QualityInPercent::QualityInPercent(double percent)
    : qIn_(static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE))
{
    assert(percent <= 400 && percent >= 0);
}

QualityOutPercent::QualityOutPercent(double percent)
    : qOut_(static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE))
{
    assert(percent <= 400 && percent >= 0);
}

static void
insertQualityIntoJtx(SField const& field, std::uint32_t value, JTx& jt)
{
    jt.jv[field.jsonName] = value;
}

void
QualityIn::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityIn, qIn_, jt);
}

void
QualityInPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityIn, qIn_, jt);
}

void
QualityOut::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityOut, qOut_, jt);
}

void
QualityOutPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityOut, qOut_, jt);
}

}  // namespace xrpl::test::jtx
