#!/usr/bin/env sh
case ${CI_COMMIT_REF_NAME} in
    develop)
        export COMPONENT="nightly"
        ;;
    release)
        export COMPONENT="unstable"
        ;;
    master)
        export COMPONENT="stable"
        ;;
    *)
        export COMPONENT="_unknown_"
        ;;
esac

