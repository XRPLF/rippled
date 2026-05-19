#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/server/LoadFeeTrack.h>

namespace xrpl {

class LoadFeeTrack_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        Config d;  // get a default configuration object
        LoadFeeTrack const l;
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee;
                f.reserve = 200 * kDropsPerXrp;
                f.increment = 50 * kDropsPerXrp;
                return f;
            }();

            BEAST_EXPECT(scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{10000}, l, fees, false) == XRPAmount{10000});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee * 10;
                f.reserve = 200 * kDropsPerXrp;
                f.increment = 50 * kDropsPerXrp;
                return f;
            }();

            BEAST_EXPECT(scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{10000}, l, fees, false) == XRPAmount{10000});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
        {
            Fees const fees = [&]() {
                Fees f;
                f.base = d.FEES.reference_fee;
                f.reserve = 200 * kDropsPerXrp;
                f.increment = 50 * kDropsPerXrp;
                return f;
            }();

            BEAST_EXPECT(scaleFeeLoad(XRPAmount{0}, l, fees, false) == XRPAmount{0});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{10000}, l, fees, false) == XRPAmount{10000});
            BEAST_EXPECT(scaleFeeLoad(XRPAmount{1}, l, fees, false) == XRPAmount{1});
        }
    }
};

BEAST_DEFINE_TESTSUITE(LoadFeeTrack, app, xrpl);

}  // namespace xrpl
