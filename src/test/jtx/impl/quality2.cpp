#include <test/jtx/quality.h>

#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>

namespace ripple {
namespace test {
namespace jtx {

qualityInPercent::qualityInPercent(double percent)
    : qIn_(static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE))
{
    assert(percent <= 400 && percent >= 0);
}

qualityOutPercent::qualityOutPercent(double percent)
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
qualityIn::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityIn, qIn_, jt);
}

void
qualityInPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityIn, qIn_, jt);
}

void
qualityOut::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityOut, qOut_, jt);
}

void
qualityOutPercent::operator()(Env&, JTx& jt) const
{
    insertQualityIntoJtx(sfQualityOut, qOut_, jt);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
