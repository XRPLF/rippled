#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/ledger/ReadView.h>

namespace ripple {

class LoadFeeTrack_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        Config d;  // get a default configuration object
        LoadFeeTrack l;
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee;
                f.reserve = 200 * DROPS_PER_XRP;
                f.increment = 50 * DROPS_PER_XRP;
                return f;
            }();

            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{10000}, l, fees, false) ==
                XRPAmount{10000});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee * 10;
                f.reserve = 200 * DROPS_PER_XRP;
                f.increment = 50 * DROPS_PER_XRP;
                return f;
            }();

            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{10000}, l, fees, false) ==
                XRPAmount{10000});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee;
                f.reserve = 200 * DROPS_PER_XRP;
                f.increment = 50 * DROPS_PER_XRP;
                return f;
            }();

            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{10000}, l, fees, false) ==
                XRPAmount{10000});
            BEAST_EXPECT(
                scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
    }
};

BEAST_DEFINE_TESTSUITE(LoadFeeTrack, app, ripple);

}  // namespace ripple
