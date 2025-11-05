#ifndef XRPL_APP_MAIN_NODEIDENTITY_H_INCLUDED
#define XRPL_APP_MAIN_NODEIDENTITY_H_INCLUDED

#include <xrpld/app/main/Application.h>

#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/program_options.hpp>

namespace ripple {

/** The cryptographic credentials identifying this server instance.

    @param app The application object
    @param cmdline The command line parameters passed into the application.
 */
std::pair<PublicKey, SecretKey>
getNodeIdentity(
    Application& app,
    boost::program_options::variables_map const& cmdline);

}  // namespace ripple

#endif
