//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLED_GRPCTESTCLIENTBASE_H
#define RIPPLED_GRPCTESTCLIENTBASE_H

#include <org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>
#include <test/jtx/envconfig.h>

namespace ripple {
namespace test {

struct GRPCTestClientBase
{
    explicit GRPCTestClientBase(std::string const& port)
        : stub_(org::xrpl::rpc::v1::XRPLedgerAPIService::NewStub(grpc::CreateChannel(
              beast::IP::Endpoint(
                  boost::asio::ip::make_address(getEnvLocalhostAddr()),
                  std::stoi(port))
                  .to_string(),
              grpc::InsecureChannelCredentials())))
    {
    }

    grpc::Status status;
    grpc::ClientContext context;
    std::unique_ptr<org::xrpl::rpc::v1::XRPLedgerAPIService::Stub> stub_;
};

}  // namespace test
}  // namespace ripple
#endif  // RIPPLED_GRPCTESTCLIENTBASE_H
