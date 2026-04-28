#include <xrpl/git/Git.h>

#include <string>

#ifndef GIT_COMMIT_HASH
#error "GIT_COMMIT_HASH must be defined"
#endif
#ifndef GIT_BUILD_BRANCH
#error "GIT_BUILD_BRANCH must be defined"
#endif

namespace xrpl::git {

static constexpr char kGIT_COMMIT_HASH[] = GIT_COMMIT_HASH;
static constexpr char kGIT_BUILD_BRANCH[] = GIT_BUILD_BRANCH;

std::string const&
getCommitHash()
{
    static std::string const kVALUE = kGIT_COMMIT_HASH;
    return kVALUE;
}

std::string const&
getBuildBranch()
{
    static std::string const kVALUE = kGIT_BUILD_BRANCH;
    return kVALUE;
}

}  // namespace xrpl::git
