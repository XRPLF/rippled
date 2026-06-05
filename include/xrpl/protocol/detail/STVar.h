#pragma once

#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <type_traits>

namespace xrpl::detail {

struct DefaultObjectT
{
    explicit DefaultObjectT() = default;
};

struct NonPresentObjectT
{
    explicit NonPresentObjectT() = default;
};

extern DefaultObjectT gDefaultObject;
extern NonPresentObjectT gNonPresentObject;

// Concept to constrain STVar constructors, which
// instantiate ST* types from SerializedTypeID
template <typename... Args>
concept ValidConstructSTArgs =
    (std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SField>> ||
     std::is_same_v<std::tuple<std::remove_cvref_t<Args>...>, std::tuple<SerialIter, SField>>);

// "variant" that can hold any type of serialized object
// and includes a small-object allocation optimization.
class STVar
{
private:
    // The largest "small object" we can accommodate
    static constexpr std::size_t kMaxSize = 72;

    std::aligned_storage<kMaxSize>::type d_ = {};
    STBase* p_ = nullptr;

public:
    ~STVar();
    STVar(STVar const& other);
    STVar(STVar&& other);
    STVar&
    operator=(STVar const& rhs);
    STVar&
    operator=(STVar&& rhs);

    STVar(STBase&& t)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    {
        p_ = t.move(kMaxSize, &d_);
    }

    STVar(STBase const& t)
    {
        p_ = t.copy(kMaxSize, &d_);
    }

    STVar(DefaultObjectT, SField const& name);
    STVar(NonPresentObjectT, SField const& name);
    STVar(SerialIter& sit, SField const& name, int depth = 0);

    STBase&
    get()
    {
        return *p_;
    }
    STBase&
    operator*()
    {
        return get();
    }
    STBase*
    operator->()
    {
        return &get();
    }
    [[nodiscard]] STBase const&
    get() const
    {
        return *p_;
    }
    STBase const&
    operator*() const
    {
        return get();
    }
    STBase const*
    operator->() const
    {
        return &get();
    }

    template <class T, class... Args>
    friend STVar
    makeStvar(Args&&... args);

private:
    STVar() = default;

    STVar(SerializedTypeID id, SField const& name);

    void
    destroy();

    template <class T, class... Args>
    void
    construct(Args&&... args)
    {
        if constexpr (sizeof(T) > kMaxSize)
        {
            p_ = new T(std::forward<Args>(args)...);
        }
        else
        {
            p_ = new (&d_) T(std::forward<Args>(args)...);
        }
    }

    /** Construct requested Serializable Type according to id.
     * The variadic args are: (SField), or (SerialIter, SField).
     * depth is ignored in former case.
     */
    template <typename... Args>
        requires ValidConstructSTArgs<Args...>
    void
    constructST(SerializedTypeID id, int depth, Args&&... arg);

    [[nodiscard]] bool
    onHeap() const
    {
        return static_cast<void const*>(p_) != static_cast<void const*>(&d_);
    }
};

template <class T, class... Args>
inline STVar
makeStvar(Args&&... args)
{
    STVar st;
    st.construct<T>(std::forward<Args>(args)...);
    return st;
}

inline bool
operator==(STVar const& lhs, STVar const& rhs)
{
    return lhs.get().isEquivalent(rhs.get());
}

inline bool
operator!=(STVar const& lhs, STVar const& rhs)
{
    return !(lhs == rhs);
}

}  // namespace xrpl::detail
