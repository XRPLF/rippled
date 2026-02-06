#pragma once

#include <test/jtx/envconfig.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>

#include <grpcpp/grpcpp.h>

namespace xrpl {
namespace test {

struct GRPCTestClientBase
{
    explicit GRPCTestClientBase(std::string const& port)
        : stub_(org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(grpc::CreateChannel(
              beast::IP::Endpoint(boost::asio::ip::make_address(getEnvLocalhostAddr()), std::stoi(port)).to_string(),
              grpc::InsecureChannelCredentials())))
    {
    }

    grpc::Status status;
    grpc::ClientContext context;
    std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub> stub_;
};

}  // namespace test
}  // namespace xrpl
