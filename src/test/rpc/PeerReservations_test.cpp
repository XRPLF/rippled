#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

#include <string>

namespace xrpl::test {

class PeerReservations_test : public beast::unit_test::suite
{
    void
    testDescriptionLengthValidation()
    {
        testcase("peer_reservations_add description length validation");

        using namespace test::jtx;

        Env env(*this);

        std::string const publicKey = "nHBt9fsb4849WmZiCds4r5TXyBeQjqnH5kzPtqgMAQMgi39YZRPa";
        std::size_t const maxDescriptionLength = 64;
        std::string const validDescription(maxDescriptionLength, 'a');
        std::string const invalidDescription(maxDescriptionLength + 1, 'b');

        auto const addResult = env.rpc("peer_reservations_add", publicKey, validDescription);
        BEAST_EXPECT(!addResult[jss::result].isMember(jss::error));

        auto const listResult = env.rpc("peer_reservations_list")[jss::result];
        BEAST_EXPECT(listResult[jss::reservations].size() == 1);
        BEAST_EXPECT(listResult[jss::reservations][0u][jss::node] == publicKey);
        BEAST_EXPECT(listResult[jss::reservations][0u][jss::description] == validDescription);

        auto const rejectResult = env.rpc("peer_reservations_add", publicKey, invalidDescription);
        BEAST_EXPECT(rejectResult[jss::result][jss::error] == "invalidParams");

        auto const listAfterReject = env.rpc("peer_reservations_list")[jss::result];
        BEAST_EXPECT(listAfterReject[jss::reservations].size() == 1);
        BEAST_EXPECT(listAfterReject[jss::reservations][0u][jss::description] == validDescription);
    }

public:
    void
    run() override
    {
        testDescriptionLengthValidation();
    }
};

BEAST_DEFINE_TESTSUITE(PeerReservations, rpc, xrpl);

}  // namespace xrpl::test
