//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <beast/unit_test/suite.h>

#include <beast/utility/noexcept.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>
#include <vector>

namespace ripple {
namespace core {

/** Represents both a quality and amounts of currencies for trade.

    Quality is the ratio of output currency to input currency, where
    higher means better for the offer taker. The first element of
    the pair is the amount of currency available for input into
    the offer. The second element of the pair is the amount of currency
    that comes out if the full amount of the input currency is provided.
    This pair also represents a fraction, for example for specifying a
    minimum quality.

    Offer requirements:

        `X` is the type `Offer`
        `a`, `b` are values of type `X`

        `X::amount_type`
            The return type of `in` and `out`
        
        `X a;`
            Constructs an uninitialized offer
        
        `a.in();`
            
        `a.out();
*/

/** Returns `true` if the offer is consumed. */
template <class Offer>
bool
is_offer_consumed (Offer const& offer) noexcept
{
    assert ((offer.in() != 0 && offer.out() != 0) ||
            (offer.in() == 0 && offer.out() == 0));
    return offer.in() == 0 || offer.out() == 0;
}

//------------------------------------------------------------------------------

template <class Amount>
struct AmountTraits
{
    /** Returns `true` if `lhs` is of lower quality than `rhs`. */
    template <class Offer>
    static
    bool
    less (Offer const& lhs, Offer const& rhs) noexcept
    {
        assert (! is_offer_consumed (lhs));
        assert (! is_offer_consumed (rhs));
        assert (lhs.out() != 0);
        assert (rhs.out() != 0);
        return (lhs.out() / lhs.in()) < (rhs.out() / rhs.in());
    }

    /** Calculates the result of multiplying amount by the implied ratio. */
    template <class Offer>
    static
    typename Offer::amount_type
    multiply (typename Offer::amount_type const& amount, Offer const& rate)
    {
        // Avoid math when result is exact
        if (amount == rate.in())
            return rate.out();
        typename Offer::amount_type const result (
            amount * (rate.out() / rate.in()));
        if (result > rate.out())
            return rate.out();
        return result;
    }

    template <class Offer>
    static
    Offer
    inverse (Offer const& value) noexcept
    {
        return Offer (value.out(), value.in());
    }
};

/** Returns the offer that would result if the input amount is applied. */
template <class Offer>
Offer
consume_offer (
    typename Offer::amount_type const& input,
    Offer offer)
{
    using Amount = typename Offer::amount_type;
    // We need to calculate the most that we can take:
    Amount const input_used (std::min (input, offer.in()));
    Amount const output (AmountTraits<Amount>::multiply (input_used, offer));
    offer.in() -= input_used;
    offer.out() -= output;
    return offer;
}

/** Fills an order amount in an order book.

    @return The resulting amount of currency in and out.
*/
template <class BookIter>
typename std::iterator_traits<BookIter>::value_type
cross_offer_in (
    typename std::iterator_traits<BookIter>::value_type::amount_type const& in,
    typename std::iterator_traits<BookIter>::value_type const& minimum_quality,
    BookIter first, BookIter last)
{
    using Amount =
        typename std::iterator_traits<BookIter>::value_type::amount_type;
    using Offer =
        typename std::iterator_traits<BookIter>::value_type;
    Offer result {0, 0};
    Amount remain (in);
    for (auto iter (first); (result.in() < in) && (iter != last); ++iter)
    {
        Offer const offer (*iter);
        if (AmountTraits<Amount>::less (offer, minimum_quality))
            break;
        Offer const offer_out (consume_offer (remain, offer));
        result.in() += offer.in() - offer_out.in();
        result.out() += offer.out() - offer_out.out();
        *iter = offer_out;
    }
    return result;
}

//------------------------------------------------------------------------------

/** Returns the composite A->C from offers A->B and B->C, equal liquidity. */
template <class Offer>
Offer
make_bridged_offer (Offer const& leg1,  Offer const& leg2)
{
    using Amount = typename Offer::amount_type;

    // Skip math if both legs can be fully consumed
    if (leg1.out() == leg2.in())
        return Offer (leg1.in(), leg2.out());

    // If leg2 has less liquidity, scale down by leg2
    if (leg1.out() > leg2.in())
        return Offer (
            AmountTraits<Amount>::multiply (leg2.in(),
                AmountTraits<Amount>::inverse (leg1)),
            leg2.out());

    // leg1 has less liquidity
    return Offer (leg1.in(),
        AmountTraits<Amount>::multiply (leg1.out(), leg2));
}

namespace detail {

/** Presents a set of order books as a single bridged order book. */
template <class BookIter>
class MultiBookIterator
{
private:
    using Amount =
        typename std::iterator_traits<BookIter>::value_type::amount_type;

    using Offer =
        typename std::iterator_traits<BookIter>::value_type;

    typedef std::is_const <
        typename std::iterator_traits<BookIter>::reference
            > IsConst;

    struct forward_iterator_base_tag
        : std::output_iterator_tag
        , std::forward_iterator_tag
    {
        forward_iterator_base_tag()
        {
        }
    };

    struct forward_const_iterator_base_tag
        : std::forward_iterator_tag
    {
        forward_const_iterator_base_tag()
        {
        }
    };

    template <class>
    friend class MultiBookIterator;

    BookIter m_direct;
    BookIter m_direct_end;
    BookIter m_leg1;
    BookIter m_leg1_end;
    BookIter m_leg2;
    BookIter m_leg2_end;
    bool m_bridged;
    Offer m_offer;

    class Proxy : public Offer
    {
    private:
        bool m_bridged;
        BookIter m_direct;
        BookIter m_leg1;
        BookIter m_leg2;

    public:
        explicit Proxy (BookIter direct)
            : Offer (*direct)
            , m_bridged (false)
            , m_direct (direct)
        {
        }

        Proxy (BookIter leg1, BookIter leg2)
            : Offer (make_bridged_offer (*leg1, *leg2))
            , m_bridged (true)
            , m_leg1 (leg1)
            , m_leg2 (leg2)
        {
        }

        Proxy&
        operator= (Offer const& offer)
        {
            if (m_bridged)
            {
                Offer const result (consume_offer (
                    offer.in(), *m_leg1));
                *m_leg2 = consume_offer (
                    m_leg1->in() - result.in(), *m_leg2);
                *m_leg1 = result;
            }
            else
            {
                *m_direct = offer;
            }
            ((Offer&)*this) = offer;
            return *this;
        }
    };

    // Returns true if this iterator has reached the one past the end marker
    bool
    past_end() const noexcept
    {
        return
            (m_direct == m_direct_end) &&
            (m_leg1 == m_leg1_end || m_leg2 == m_leg2_end);
    }

    void
    throw_if_past()
    {
        if (past_end())
            throw std::out_of_range ("invalid iterator dereferenced");
    }

    // Returns true if the iterators are both equal, or both past the end
    template <class Iter1, class Iter2>
    static
    bool
    iter_eq (Iter1 iter1, Iter1 iter1_end,
             Iter2 iter2, Iter2 iter2_end) noexcept
    {
        if (iter1 == iter1_end)
            return iter2 == iter2_end;
        if (iter2 == iter2_end)
            return false;
        return iter1 == iter2;
    }

    // Stores the best offer (if any) in m_offer
    void
    calc_offer()
    {
        if (past_end())
            return;

        // FIXME rewrite this - having 3 nested if's is overkill...
        if ((m_leg1 != m_leg1_end) && (m_leg2 != m_leg2_end))
        {
            Offer const bridged (
                make_bridged_offer (*m_leg1, *m_leg2));

            if (m_direct != m_direct_end)
            {
                if (AmountTraits<Amount>::less (*m_direct, bridged))
                {
                    m_bridged = true;
                    m_offer = bridged;
                }
                else
                {
                    m_bridged = false;
                    m_offer = *m_direct;
                }
            }
            else
            {
                m_bridged = true;
                m_offer = bridged;
            }
        }
        else
        {
            m_bridged = false;
            m_offer = *m_direct;
        }
    }

public:
    typedef std::ptrdiff_t difference_type;
    typedef typename std::iterator_traits <
        BookIter>::value_type value_type;
    typedef value_type* pointer;
    typedef value_type& reference;
    typedef std::conditional_t <
        std::is_const <std::remove_reference_t <reference>>::value,
            forward_const_iterator_base_tag,
                forward_iterator_base_tag> iterator_category;

    MultiBookIterator () = default;

    template <
        class OtherBookIter,
        class = std::enable_if_t <
            std::is_same <value_type, typename OtherBookIter::value_type>::value
        >
    >
    MultiBookIterator (MultiBookIterator <OtherBookIter> const& other)
        : m_direct (other.m_direct)
        , m_direct_end (other.m_direct_end)
        , m_leg1 (other.m_leg1)
        , m_leg1_end (other.m_leg1_end)
        , m_leg2 (other.m_leg2)
        , m_leg2_end (other.m_leg2_end)
    {
    }

    template <
        class OtherBookIter,
        class = std::enable_if_t <
            std::is_same <value_type, typename OtherBookIter::value_type>::value
        >
    >
    MultiBookIterator (
        OtherBookIter direct_first, OtherBookIter direct_last,
        OtherBookIter leg1_first, OtherBookIter leg1_last,
        OtherBookIter leg2_first, OtherBookIter leg2_last)
        : m_direct (direct_first)
        , m_direct_end (direct_last)
        , m_leg1 (leg1_first)
        , m_leg1_end (leg1_last)
        , m_leg2 (leg2_first)
        , m_leg2_end (leg2_last)
    {
        calc_offer();
    }

    MultiBookIterator&
    operator++()
    {
        throw_if_past ();

        if (m_direct != m_direct_end)
            ++m_direct;
        if (m_leg1 != m_leg1_end)
            ++m_leg1;
        if (m_leg2 != m_leg2_end)
            ++m_leg2;
        calc_offer();
        return *this;
    }

    MultiBookIterator
    operator++(int)
    {
        MultiBookIterator prev (*this);
        this->operator++();
        return prev;
    }

    template <class OtherBookIter>
    bool
    operator== (
        MultiBookIterator <OtherBookIter> const& other) const noexcept
    {
        if (! iter_eq (m_direct, m_direct_end,
                       other.m_direct, other.m_direct_end))
            return false;

        if (! iter_eq (m_leg1, m_leg1_end,
                       other.m_leg1, other.m_leg1_end))
            return false;
        
        if (! iter_eq (m_leg2, m_leg2_end,
                       other.m_leg2, other.m_leg2_end))
            return false;
    
        return true;
    }

    Offer const*
    operator->() const
    {
        throw_if_past();
        return &m_offer;
    }

    Offer const&
    operator*() const
    {
        throw_if_past();
        return m_offer;
    }

#ifndef _MSC_VER
    // This blows up Visual Studio
    template <
        bool MaybeConst = IsConst::value,
        class = std::enable_if_t <! MaybeConst>
    >
#endif
    Proxy
    operator*()
    {
        static_assert (! IsConst::value,
            "invalid call of non-const member function");
        throw_if_past();
        if (m_bridged)
            return Proxy (m_leg1, m_leg2);
        return Proxy (m_direct);
    }
};

template <class LeftBookIter, class RightBookIter>
bool
operator!= (
    MultiBookIterator <LeftBookIter> const& lhs,
    MultiBookIterator <RightBookIter> const& rhs) noexcept
{
    return !(rhs == lhs);
}

} // detail

//------------------------------------------------------------------------------

// TODO Allow const Book
//
template <class Book>
class MultiBook
{
private:
    static_assert (! std::is_const <Book>::value,
        "Book cannot be const");

    std::reference_wrapper<Book> m_direct;
    std::reference_wrapper<Book> m_leg1;
    std::reference_wrapper<Book> m_leg2;

public:
    typedef typename Book::value_type value_type;
    typedef typename Book::reference reference;
    typedef typename Book::const_reference const_reference;

    typedef detail::MultiBookIterator <
        typename Book::iterator> iterator;
    
    typedef detail::MultiBookIterator <
        typename Book::const_iterator> const_iterator;

    typedef typename Book::difference_type difference_type;
    typedef typename Book::size_type size_type;

    MultiBook (Book& direct, Book& leg1, Book& leg2)
        : m_direct (direct)
        , m_leg1 (leg1)
        , m_leg2 (leg2)
    {
    }

    bool
    empty() const noexcept
    {
        return cbegin() == cend();
    }

    // Complexity: linear
    size_type
    size() const noexcept
    {
        return std::distance (cbegin(), cend());
    }

    iterator
    begin() noexcept
    {
        return iterator (
            m_direct.get().begin(), m_direct.get().end(),
            m_leg1.get().begin(), m_leg1.get().end(),
            m_leg2.get().begin(), m_leg2.get().end());
    }

    iterator
    end() noexcept
    {
        return iterator (
            m_direct.get().end(), m_direct.get().end(),
            m_leg1.get().end(), m_leg1.get().end(),
            m_leg2.get().end(), m_leg2.get().end());
    }

    const_iterator
    begin() const noexcept
    {
        return const_iterator (
            m_direct.get().cbegin(), m_direct.get().cend(),
            m_leg1.get().cbegin(), m_leg1.get().cend(),
            m_leg2.get().cbegin(), m_leg2.get().cend());
    }

    const_iterator
    end() const noexcept
    {
        return const_iterator (
            m_direct.get().cend(), m_direct.get().cend(),
            m_leg1.get().cend(), m_leg1.get().cend(),
            m_leg2.get().cend(), m_leg2.get().cend());
    }

    const_iterator
    cbegin() const noexcept
    {
        return const_iterator (
            m_direct.get().cbegin(), m_direct.get().cend(),
            m_leg1.get().cbegin(), m_leg1.get().cend(),
            m_leg2.get().cbegin(), m_leg2.get().cend());
    }

    const_iterator
    cend() const noexcept
    {
        return const_iterator (
            m_direct.get().cend(), m_direct.get().cend(),
            m_leg1.get().cend(), m_leg1.get().cend(),
            m_leg2.get().cend(), m_leg2.get().cend());
    }
};

//------------------------------------------------------------------------------

template <class Book>
typename std::iterator_traits<typename Book::iterator>::value_type
cross_offer_in (
    typename std::iterator_traits<
        typename Book::iterator>::value_type::amount_type const& in,
    typename std::iterator_traits <
        typename Book::iterator>::value_type const& minimum_quality,
    Book& book)
{
    return cross_offer_in (in, minimum_quality, book.begin(), book.end());
}

template <class Book>
typename std::iterator_traits<typename Book::iterator>::value_type
cross_offer_in (
    typename std::iterator_traits<
        typename Book::iterator>::value_type::amount_type const& in,
    typename std::iterator_traits<typename Book::iterator>::value_type
        const& minimum_quality,
    Book& direct, Book& leg1, Book& leg2)
{
    MultiBook <Book> book (direct, leg1, leg2);
    return cross_offer_in (in, minimum_quality, book);
}

//------------------------------------------------------------------------------

class cross_offers_test : public beast::unit_test::suite
{
public:
    template <class Amount>
    class OfferType
    {
    private:
        Amount m_in;
        Amount m_out;

    public:
        typedef Amount amount_type;

        OfferType() = default;

        OfferType (Amount const& in, Amount const& out) noexcept
            : m_in (in)
            , m_out (out)
        {
            assert ((m_in != 0 && m_out != 0) || (m_in == 0 && m_out == 0));
        }

        Amount const&
        in() const noexcept
        {
            return m_in;
        }

        Amount&
        in() noexcept
        {
            return m_in;
        }

        Amount const& 
        out() const noexcept
        {
            return m_out;
        }

        Amount&
        out() noexcept
        {
            return m_out;
        }
    };

    typedef double Amount;
    typedef OfferType<Amount> Offer;
    typedef std::vector <Offer> Book;

    template <class Amount>
    OfferType<Amount>
    static make_offer (Amount from, Amount rate)
    {
        return OfferType<Amount> (from, from * rate);
    }

    template <class Book>
    void
    check_iterators (Book& b)
    {
        using Offer = typename Book::value_type;
        // These make sure that expressions are well-formed
        std::for_each (b.begin(), b.end(), [](Offer){});
        std::for_each (b.cbegin(), b.cend(), [](Offer){});
        {
            Book const& cb (b);
            std::for_each (cb.begin(), cb.end(), [](Offer){});
            // Should not compile
            //*cb.begin() = Book::value_type();
            expect (cb.begin() == cb.end());
            expect (cb.begin() == cb.cend());
        }
        expect (b.cbegin() == b.cend());
        expect (b.begin() == b.cend());
        typename Book::iterator iter;
        typename Book::const_iterator citer (iter);
        citer = typename Book::iterator();
    }

    void
    test_iterators()
    {
        {
            Book b;
            check_iterators (b);
        }

        {
            Book b1, b2, b3;
            MultiBook <Book> b (b1, b2, b3);
            check_iterators (b);
        }
    }

    void
    test_full_cross_auto_direct ()
    {
        testcase ("Autobridge (Full Direct Crossing)");

        Book a_to_b;

        a_to_b.push_back(make_offer(300., 2.0));

        Book a_to_x;

        a_to_x.push_back(make_offer(300., 0.5));

        Book x_to_b;

        x_to_b.push_back(make_offer(150., 0.5));

        auto const rate = make_offer(50.0, 1.5);

        Offer result = cross_offer_in (
            50.0,
            rate,
            a_to_b, a_to_x, x_to_b);

        expect ((result.in() == 50.0) && (result.out() == 100.0),
            "Expected { 50.0 : 100.0 }");
    }

    void
    test_full_cross_auto_bridge ()
    {
        testcase ("Autobridge (Full Bridge Crossing)");

        Book a_to_b;

        a_to_b.push_back(make_offer(300.00, 1.0));

        Book a_to_x;

        a_to_x.push_back(make_offer(300.00, 2.0));

        Book x_to_b;

        x_to_b.push_back(make_offer(300.00, 1.0));

        auto const rate = make_offer(50.0, 1.5);

        Offer result = cross_offer_in (
            50.0,
            rate,
            a_to_b, a_to_x, x_to_b);

        expect ((result.in() == 50.0) && (result.out() == 100.0),
            "Expected { 50.0 : 100.0 }");
    }

    void
    test_full_cross_direct ()
    {
        testcase ("Direct (Full Crossing)");

        Book a_to_b;

        a_to_b.push_back(make_offer(300.00, 2.0));

        auto const rate = make_offer(50.0, 1.5);

        Offer result = cross_offer_in (
            50.0,
            rate,
            a_to_b);

        expect ((result.in() == 50.0) && (result.out() == 100.0),
            "Expected { 50.0 : 100.0 }");
    }

    void
    test_partial_cross_direct ()
    {
        testcase ("Direct (Partial Crossing)");

        Book a_to_b;

        a_to_b.push_back(make_offer(25.00, 2.0));

        auto const rate = make_offer(50.0, 1.5);

        Offer result = cross_offer_in (
            50.0,
            rate,
            a_to_b);

        expect ((result.in() == 25.0) && (result.out() == 50.0),
            "Expected { 25.0 : 50.0 }");
    }

    void
    run()
    {
        test_iterators();
        
        test_full_cross_direct ();
        test_full_cross_auto_direct ();
        test_full_cross_auto_bridge ();

        test_partial_cross_direct ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(cross_offers,orderbook_logic,ripple);

}
}
