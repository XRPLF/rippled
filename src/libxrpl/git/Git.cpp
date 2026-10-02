#include <xrpl/git/Git.h>

#include <string>

#ifndef GIT_COMMIT_HASH
#error "GIT_COMMIT_HASH must be defined"
#endif
#ifndef GIT_BUILD_BRANCH
#error "GIT_BUILD_BRANCH must be defined"
#endif

namespace xrpl::git {

static constexpr char kGitCommitHash[] = GIT_COMMIT_HASH;
static constexpr char kGitBuildBranch[] = GIT_BUILD_BRANCH;

std::string const&
getCommitHash()
{
    static std::string const kValue = kGitCommitHash;
    return kValue;
}

std::string const&
getBuildBranch()
{
    static std::string const kValue = kGitBuildBranch;
    return kValue;
}

}  // namespace xrpl::git
