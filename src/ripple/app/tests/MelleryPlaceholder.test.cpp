#include <BeastConfig.h>
#include <ripple/beast/unit_test.h>


namespace ripple {

class MelleryPlaceholder_test : public beast::unit_test::suite
{
public:
    void
    test()
    {
        log << "Hello\n";
        pass();
    }

    void
    run()
    {
        test();
    }
};

BEAST_DEFINE_TESTSUITE(MelleryPlaceholder_test,tx,ripple);

}
