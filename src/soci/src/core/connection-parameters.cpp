//
// Copyright (C) 2013 Vadim Zeitlin
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#define SOCI_SOURCE
#include "connection-parameters.h"
#include "soci-backend.h"
#include "backend-loader.h"

using namespace soci;

namespace // anonymous
{

void parseConnectString(std::string const & connectString,
    std::string & backendName,
    std::string & connectionParameters)
{
    std::string const protocolSeparator = "://";

    std::string::size_type const p = connectString.find(protocolSeparator);
    if (p == std::string::npos)
    {
        throw soci_error("No backend name found in " + connectString);
    }

    backendName = connectString.substr(0, p);
    connectionParameters = connectString.substr(p + protocolSeparator.size());
}

} // namespace anonymous

connection_parameters::connection_parameters()
    : factory_(NULL)
{
}

connection_parameters::connection_parameters(backend_factory const & factory,
    std::string const & connectString)
    : factory_(&factory), connectString_(connectString)
{
}

connection_parameters::connection_parameters(std::string const & backendName,
    std::string const & connectString)
    : factory_(&dynamic_backends::get(backendName)), connectString_(connectString)
{
}

connection_parameters::connection_parameters(std::string const & fullConnectString)
{
    std::string backendName;
    std::string connectString;

    parseConnectString(fullConnectString, backendName, connectString);

    factory_ = &dynamic_backends::get(backendName);
    connectString_ = connectString;
}
