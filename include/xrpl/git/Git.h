#pragma once

#include <string>

namespace xrpl::git {

std::string const&
getCommitHash();

std::string const&
getBuildBranch();

}  // namespace xrpl::git
