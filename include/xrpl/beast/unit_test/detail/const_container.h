// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

namespace beast::unit_test::detail {

/**
 * Adapter to constrain a container interface.
 * The interface allows for limited read only operations. Derived classes
 * provide additional behavior.
 */
template <class Container>
class ConstContainer
{
private:
    using ContType = Container;

    ContType cont_;

protected:
    ContType&
    cont()
    {
        return cont_;
    }

    [[nodiscard]] ContType const&
    cont() const
    {
        return cont_;
    }

public:
    using ValueType = ContType::value_type;
    using SizeType = ContType::size_type;
    using DifferenceType = ContType::difference_type;
    using Iterator = ContType::const_iterator;
    using ConstIterator = ContType::const_iterator;

    /**
     * Returns `true` if the container is empty.
     */
    [[nodiscard]] bool
    empty() const
    {
        return cont_.empty();
    }

    /**
     * Returns the number of items in the container.
     */
    [[nodiscard]] SizeType
    size() const
    {
        return cont_.size();
    }

    /**
     * Returns forward iterators for traversal.
     */
    /** @{ */
    [[nodiscard]] ConstIterator
    begin() const
    {
        return cont_.cbegin();
    }

    [[nodiscard]] ConstIterator
    cbegin() const
    {
        return cont_.cbegin();
    }

    [[nodiscard]] ConstIterator
    end() const
    {
        return cont_.cend();
    }

    [[nodiscard]] ConstIterator
    cend() const
    {
        return cont_.cend();
    }
    /** @} */
};

}  // namespace beast::unit_test::detail
