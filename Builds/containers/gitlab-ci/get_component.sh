#!/usr/bin/env sh
case ${CI_COMMIT_REF_NAME} in
    unstable)
        export COMPONENT="nightly"
        ;;
    release)
        export COMPONENT="unstable"
        ;;
    stable)
        export COMPONENT="stable"
        ;;
    *)
        export COMPONENT="_unknown_"
        ;;
esac

