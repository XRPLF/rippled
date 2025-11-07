#include <xrpld/app/tx/detail/OfferStream.h>

#include <xrpl/beast/unit_test.h>

namespace ripple {

class OfferStream_test : public beast::unit_test::suite
{
public:
    void
    test()
    {
        pass();
    }

    void
    run() override
    {
        test();
    }
};

BEAST_DEFINE_TESTSUITE(OfferStream, app, ripple);

}  // namespace ripple
