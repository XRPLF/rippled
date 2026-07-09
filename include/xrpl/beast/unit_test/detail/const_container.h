// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

namespace beast::unit_test::detail {

/** Adapter to constrain a container interface.
    The interface allows for limited read only operations. Derived classes
    provide additional behavior.
*/
template <class Container>
class ConstContainer
{
private:
    using cont_type = Container;

    cont_type cont_;

protected:
    cont_type&
    cont()
    {
        return cont_;
    }

    [[nodiscard]] cont_type const&
    cont() const
    {
        return cont_;
    }

public:
    using value_type = cont_type::value_type;
    using size_type = cont_type::size_type;
    using difference_type = cont_type::difference_type;
    using iterator = cont_type::const_iterator;
    using const_iterator = cont_type::const_iterator;

    /** Returns `true` if the container is empty. */
    [[nodiscard]] bool
    empty() const
    {
        return cont_.empty();
    }

    /** Returns the number of items in the container. */
    [[nodiscard]] size_type
    size() const
    {
        return cont_.size();
    }

    /** Returns forward iterators for traversal. */
    /** @{ */
    [[nodiscard]] const_iterator
    begin() const
    {
        return cont_.cbegin();
    }

    [[nodiscard]] const_iterator
    cbegin() const
    {
        return cont_.cbegin();
    }

    [[nodiscard]] const_iterator
    end() const
    {
        return cont_.cend();
    }

    [[nodiscard]] const_iterator
    cend() const
    {
        return cont_.cend();
    }
    /** @} */
};

}  // namespace beast::unit_test::detail
