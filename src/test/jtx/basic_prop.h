#ifndef XRPL_TEST_JTX_BASIC_PROP_H_INCLUDED
#define XRPL_TEST_JTX_BASIC_PROP_H_INCLUDED

#include <memory>

namespace ripple {
namespace test {
namespace jtx {

struct basic_prop
{
    virtual ~basic_prop() = default;
    virtual std::unique_ptr<basic_prop>
    clone() const = 0;
    virtual bool
    assignable(basic_prop const*) const = 0;
};

template <class T>
struct prop_type : basic_prop
{
    T t;

    template <class... Args>
    prop_type(Args&&... args) : t(std::forward<Args>(args)...)
    {
    }

    std::unique_ptr<basic_prop>
    clone() const override
    {
        return std::make_unique<prop_type<T>>(t);
    }

    bool
    assignable(basic_prop const* src) const override
    {
        return dynamic_cast<prop_type<T> const*>(src);
    }
};

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
