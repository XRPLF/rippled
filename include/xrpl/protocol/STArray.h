#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

class STArray final : public STBase, public CountedObject<STArray>
{
private:
    using ListType = std::vector<STObject>;

    ListType v_;

public:
    // Required by std::back_insert_iterator.
    // NOLINTNEXTLINE(readability-identifier-naming)
    using value_type = STObject;
    using SizeType = ListType::size_type;
    using Iterator = ListType::iterator;
    using ConstIterator = ListType::const_iterator;

    STArray() = default;
    STArray(STArray const&) = default;

    template <class Iter>
    explicit STArray(Iter first, Iter last)
        requires(std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>);

    template <class Iter>
    STArray(SField const& f, Iter first, Iter last)
        requires(std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>);

    STArray&
    operator=(STArray const&) = default;
    STArray(STArray&&);
    STArray&
    operator=(STArray&&);

    STArray(SField const& f, std::size_t n);
    STArray(SerialIter& sit, SField const& f, int depth = 0);
    explicit STArray(int n);
    explicit STArray(SField const& f);

    STObject&
    operator[](std::size_t j);

    STObject const&
    operator[](std::size_t j) const;

    STObject&
    back();

    [[nodiscard]] STObject const&
    back() const;

    template <class... Args>
    void
    emplaceBack(Args&&... args);

    void
    pushBack(STObject const& object);

    void
    pushBack(STObject&& object);

    // STL-compatible alias required by std::back_insert_iterator
    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject const& object)
    {
        pushBack(object);
    }

    void
    // NOLINTNEXTLINE(readability-identifier-naming)
    push_back(STObject&& object)
    {
        pushBack(std::move(object));
    }

    Iterator
    begin();

    Iterator
    end();

    [[nodiscard]] ConstIterator
    begin() const;

    [[nodiscard]] ConstIterator
    end() const;

    [[nodiscard]] SizeType
    size() const;

    [[nodiscard]] bool
    empty() const;

    void
    clear();

    void
    reserve(std::size_t n);

    void
    swap(STArray& a) noexcept;

    [[nodiscard]] std::string
    getFullText() const override;

    [[nodiscard]] std::string
    getText() const override;

    [[nodiscard]] json::Value
    getJson(JsonOptions index) const override;

    void
    add(Serializer& s) const override;

    void
    sort(bool (*compare)(STObject const& o1, STObject const& o2));

    bool
    operator==(STArray const& s) const;

    Iterator
    erase(Iterator pos);

    Iterator
    erase(ConstIterator pos);

    Iterator
    erase(Iterator first, Iterator last);

    Iterator
    erase(ConstIterator first, ConstIterator last);

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <class Iter>
STArray::STArray(Iter first, Iter last)
    requires(std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>)
    : v_(first, last)
{
}

template <class Iter>
STArray::STArray(SField const& f, Iter first, Iter last)
    requires(std::is_convertible_v<typename std::iterator_traits<Iter>::reference, STObject>)
    : STBase(f), v_(first, last)
{
}

inline STObject&
STArray::operator[](std::size_t j)
{
    return v_[j];
}

inline STObject const&
STArray::operator[](std::size_t j) const
{
    return v_[j];
}

inline STObject&
STArray::back()
{
    return v_.back();
}

inline STObject const&
STArray::back() const
{
    return v_.back();
}

template <class... Args>
inline void
STArray::emplaceBack(Args&&... args)
{
    v_.emplace_back(std::forward<Args>(args)...);
}

inline void
STArray::pushBack(STObject const& object)
{
    v_.push_back(object);
}

inline void
STArray::pushBack(STObject&& object)
{
    v_.push_back(std::move(object));
}

inline STArray::Iterator
STArray::begin()
{
    return v_.begin();
}

inline STArray::Iterator
STArray::end()
{
    return v_.end();
}

inline STArray::ConstIterator
STArray::begin() const
{
    return v_.begin();
}

inline STArray::ConstIterator
STArray::end() const
{
    return v_.end();
}

inline STArray::SizeType
STArray::size() const
{
    return v_.size();
}

inline bool
STArray::empty() const
{
    return v_.empty();
}

inline void
STArray::clear()
{
    v_.clear();
}

inline void
STArray::reserve(std::size_t n)
{
    v_.reserve(n);
}

inline void
STArray::swap(STArray& a) noexcept
{
    v_.swap(a.v_);
}

inline bool
STArray::operator==(STArray const& s) const
{
    return v_ == s.v_;
}

inline STArray::Iterator
STArray::erase(Iterator pos)
{
    return v_.erase(pos);
}

inline STArray::Iterator
STArray::erase(ConstIterator pos)
{
    return v_.erase(pos);
}

inline STArray::Iterator
STArray::erase(Iterator first, Iterator last)
{
    return v_.erase(first, last);
}

inline STArray::Iterator
STArray::erase(ConstIterator first, ConstIterator last)
{
    return v_.erase(first, last);
}

}  // namespace xrpl
