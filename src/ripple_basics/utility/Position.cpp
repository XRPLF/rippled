#include "../../beast/beast/unit_test/suite.h"

#include "Position.h"

namespace ripple {

struct Position_test : beast::unit_test::suite
{
    typedef std::vector<int> Container;
    typedef ContainerPosition<Container> CPos;

    void testContainer ()
    {
        Container c{1, 2, 3};
        CPos cp(c);
        expect (cp.at() == 1, "at 1");
        expect (cp.isFirst(), "is first");
        expect (!cp.isLast(), "is last");
        expect (cp.index() == 0, "is zero");
        expect (cp.index(FIRST) == 0, "first");
        expect (cp.index(PREVIOUS) == 0, "previous");
        expect (cp.index(CURRENT) == 0, "current");
        expect (cp.index(NEXT) == 1, "next");
        expect (cp.index(LAST) == 2, "last");

        auto cp2 = cp.move(NEXT);
        expect (cp2.at() == 2, "at 2");
        expect (!cp2.isFirst(), "is first 2");
        expect (!cp2.isLast(), "is last 2");
        expect (cp2.index() == 1, "is one 2");
        expect (cp2.index(FIRST) == 0, "first 2");
        expect (cp2.index(PREVIOUS) == 0, "previous 2");
        expect (cp2.index(CURRENT) == 1, "current 2");
        expect (cp2.index(NEXT) == 2, "next 2");
        expect (cp2.index(LAST) == 2, "last 2");

        auto cp3 = cp2.move(NEXT);
        expect (cp3.at() == 3, "at 3");
        expect (!cp3.isFirst(), "is first 3");
        expect (cp3.isLast(), "is last 3");
        expect (cp3.index() == 2, "is two 3");
        expect (cp3.index(FIRST) == 0, "first 3");
        expect (cp3.index(PREVIOUS) == 1, "previous 3");
        expect (cp3.index(CURRENT) == 2, "current 3");
        expect (cp3.index(NEXT) == 2, "next 3");
        expect (cp3.index(LAST) == 2, "last 3");

        auto cp4 = cp3.move(NEXT);
        expect (cp4.at() == 3, "at 3");
        expect (!cp4.isFirst(), "is first 3");
        expect (cp4.isLast(), "isn't last 3");
        expect (cp4.index() == 2, "is two 3");
        expect (cp4.index(FIRST) == 0, "first 3");
        expect (cp4.index(PREVIOUS) == 1, "previous 3");
        expect (cp4.index(CURRENT) == 2, "current 3");
        expect (cp4.index(NEXT) == 2, "next 3");
        expect (cp4.index(LAST) == 2, "last 3");
    }

    void run ()
    {
        testContainer();
    }
};

BEAST_DEFINE_TESTSUITE(Position, ripple_basics, ripple);

} // ripple
