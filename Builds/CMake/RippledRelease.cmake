#[===================================================================[
   package/container targets - (optional)
#]===================================================================]

if (is_root_project)
  if (NOT DOCKER)
    find_program (DOCKER docker)
  endif ()

  if (DOCKER)
    # if no container label is provided, use current git hash
    git_hash (commit_hash)
    if (NOT container_label)
      set (container_label ${commit_hash})
    endif ()
    message (STATUS "using [${container_label}] as build container tag...")

    file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/packages)

    #[===================================================================[
        rpm
    #]===================================================================]
    add_custom_target (rpm_container
      docker build
        --pull
        --build-arg GIT_COMMIT=${commit_hash}
        -t rippleci/rippled-rpm-builder:${container_label}
        $<$<BOOL:${rpm_cache_from}>:--cache-from=${rpm_cache_from}>
        -f centos-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/centos-builder/Dockerfile
        Builds/containers/centos-builder/centos_setup.sh
        Builds/containers/shared/update-rippled.sh
        Builds/containers/shared/update_sources.sh
        Builds/containers/shared/rippled.service
        Builds/containers/shared/rippled-reporting.service
        Builds/containers/packaging/rpm/rippled.spec
        Builds/containers/packaging/rpm/build_rpm.sh
        Builds/containers/packaging/rpm/50-rippled.preset
        Builds/containers/packaging/rpm/50-rippled-reporting.preset
        bin/getRippledInfo
    )
    exclude_from_default (rpm_container)
    add_custom_target (rpm
      docker run
        -v ${CMAKE_CURRENT_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
        -v ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
        -t rippleci/rippled-rpm-builder:${container_label}
        /bin/bash -c "cp -fpu rippled/Builds/containers/packaging/rpm/build_rpm.sh . && ./build_rpm.sh"
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/packaging/rpm/rippled.spec
    )
    exclude_from_default (rpm)
    if (NOT have_package_container)
      add_dependencies(rpm rpm_container)
    endif ()
    #[===================================================================[
        dpkg
    #]===================================================================]
    # currently use ubuntu 16.04 as a base b/c it has one of
    # the lower versions of libc among ubuntu and debian releases.
    # we could change this in the future and build with some other deb
    # based system.
    add_custom_target (dpkg_container
      docker build
        --pull
        --build-arg DIST_TAG=20.04
        --build-arg GIT_COMMIT=${commit_hash}
        -t rippleci/rippled-dpkg-builder:${container_label}
        $<$<BOOL:${dpkg_cache_from}>:--cache-from=${dpkg_cache_from}>
        -f ubuntu-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/packaging/dpkg/debian/rippled-reporting.links
        Builds/containers/packaging/dpkg/debian/copyright
        Builds/containers/packaging/dpkg/debian/rules
        Builds/containers/packaging/dpkg/debian/rippled-reporting.install
        Builds/containers/packaging/dpkg/debian/rippled-reporting.postinst
        Builds/containers/packaging/dpkg/debian/rippled.links
        Builds/containers/packaging/dpkg/debian/rippled.prerm
        Builds/containers/packaging/dpkg/debian/rippled.postinst
        Builds/containers/packaging/dpkg/debian/rippled-dev.install
        Builds/containers/packaging/dpkg/debian/dirs
        Builds/containers/packaging/dpkg/debian/rippled.postrm
        Builds/containers/packaging/dpkg/debian/rippled.conffiles
        Builds/containers/packaging/dpkg/debian/compat
        Builds/containers/packaging/dpkg/debian/source/format
        Builds/containers/packaging/dpkg/debian/source/local-options
        Builds/containers/packaging/dpkg/debian/README.Debian
        Builds/containers/packaging/dpkg/debian/rippled.install
        Builds/containers/packaging/dpkg/debian/rippled.preinst
        Builds/containers/packaging/dpkg/debian/docs
        Builds/containers/packaging/dpkg/debian/control
        Builds/containers/packaging/dpkg/debian/rippled-reporting.dirs
        Builds/containers/packaging/dpkg/build_dpkg.sh
        Builds/containers/ubuntu-builder/Dockerfile
        Builds/containers/ubuntu-builder/ubuntu_setup.sh
        bin/getRippledInfo
        Builds/containers/shared/install_cmake.sh
        Builds/containers/shared/update-rippled.sh
        Builds/containers/shared/update_sources.sh
        Builds/containers/shared/rippled.service
        Builds/containers/shared/rippled-reporting.service
        Builds/containers/shared/rippled-logrotate
        Builds/containers/shared/update-rippled-cron
    )
    exclude_from_default (dpkg_container)
    add_custom_target (dpkg
      docker run
        -v ${CMAKE_CURRENT_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
        -v ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
        -t rippleci/rippled-dpkg-builder:${container_label}
        /bin/bash -c "cp -fpu rippled/Builds/containers/packaging/dpkg/build_dpkg.sh . && ./build_dpkg.sh"
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/packaging/dpkg/debian/control
    )
    exclude_from_default (dpkg)
    if (NOT have_package_container)
      add_dependencies(dpkg dpkg_container)
    endif ()
    #[===================================================================[
        ci container
    #]===================================================================]
    # now use the same ubuntu image for our travis-ci docker images,
    # but we use a newer distro (18.04 vs 16.04).
    #
    # the following steps assume the github pkg repo, but it's possible to
    # adapt these for other docker hub repositories.
    #
    # steps for publishing a new CI image when you make changes:
    #
    #   mkdir bld.ci && cd bld.ci && cmake -Dpackages_only=ON -Dcontainer_label=CI_LATEST
    #   cmake --build . --target ci_container --verbose
    #   docker tag rippled-ci-builder:CI_LATEST <HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD
    #      (NOTE: change YYYY-MM-DD to match current date, or use a different
    #             tag/version scheme if you prefer)
    #   docker push <HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD
    #      (NOTE: <HUB REPO PATH> is probably your user or org name if using
    #             docker hub, or it might be something like
    #             docker.pkg.github.com/ripple/rippled if using the github pkg
    #             registry. for any registry, you will need to be logged-in via
    #             docker and have push access.)
    #
    # ...then change the DOCKER_IMAGE line in .travis.yml :
    #     - DOCKER_IMAGE="<HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD"
    add_custom_target (ci_container
      docker build
        --pull
        --build-arg DIST_TAG=20.04
        --build-arg GIT_COMMIT=${commit_hash}
        --build-arg CI_USE=true
        -t rippled-ci-builder:${container_label}
        $<$<BOOL:${ci_cache_from}>:--cache-from=${ci_cache_from}>
        -f ubuntu-builder/Dockerfile .
      WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        Builds/containers/ubuntu-builder/Dockerfile
        Builds/containers/ubuntu-builder/ubuntu_setup.sh
    )
    exclude_from_default (ci_container)
  else ()
    message (STATUS "docker NOT found -- won't be able to build containers for packaging")
  endif ()
endif ()
