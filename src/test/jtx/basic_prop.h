#pragma once

#include <memory>

namespace xrpl::test::jtx {

struct BasicProp
{
    virtual ~BasicProp() = default;
    [[nodiscard]] virtual std::unique_ptr<BasicProp>
    clone() const = 0;
    virtual bool
    assignable(BasicProp const*) const = 0;
};

template <class T>
struct PropType : BasicProp
{
    T t;

    template <class... Args>
    PropType(Args&&... args) : t(std::forward<Args>(args)...)
    {
    }

    [[nodiscard]] std::unique_ptr<BasicProp>
    clone() const override
    {
        return std::make_unique<PropType<T>>(t);
    }

    bool
    assignable(BasicProp const* src) const override
    {
        return dynamic_cast<PropType<T> const*>(src);
    }
};

}  // namespace xrpl::test::jtx
