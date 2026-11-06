# shellcheck shell=bash

# Helpers shared by the release checks in this directory, see RELEASING.md.

# Sets BRANCHES to all release and staging branches, and fails if there are none.
load_release_branches() {
    mapfile -t BRANCHES < <(git for-each-ref --format='%(refname)' 'refs/remotes/*/release/*' 'refs/remotes/*/staging/*')
    if [ "${#BRANCHES[@]}" -eq 0 ]; then
        echo "Error: No release or staging branches found."
        exit 1
    fi
}
