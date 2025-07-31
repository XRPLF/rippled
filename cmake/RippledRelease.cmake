
option(have_package_container
  "Sometimes you already have the tagged container you want to use for package \
   building and you don't want docker to rebuild it. This flag will detach the \
   dependency of the package build from the container build. It's an advanced \
   use case and most likely you should not be touching this flag." OFF)


if(NOT packager_image_name)
  set(packager_image_name rippled_packager)
endif()
#[===================================================================[
   package builder image target -(optional)
#]===================================================================]

if(is_root_project)
  if(NOT DOCKER)
    find_program(DOCKER docker)
  endif()

  if(DOCKER)
    # if no container label is provided, use current git hash
    if(NOT container_label)
      git_branch(branch)
      message("git_branch: ${branch}")
      set(container_label ${GIT_COMMIT_HASH})
    endif()
    message(STATUS "Using [ ${container_label} ] as packager image tag")
    message(STATUS "Using [ ${packager_image_name} ] as packager image name")
    set(image "${packager_image_name}:${container_label}")
    message(STATUS "Final image name --> [ ${image} ] ")
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/packages)
    set(CONAN_CACHE_VOLUME rippled_conan_cache)
    # if(is_linux)
    #   execute_process(COMMAND id -u
    #     OUTPUT_VARIABLE DOCKER_USER_ID
    #     OUTPUT_STRIP_TRAILING_WHITESPACE)
    #   message(STATUS "docker local user id: ${DOCKER_USER_ID}")
    #   execute_process(COMMAND id -g
    #     OUTPUT_VARIABLE DOCKER_GROUP_ID
    #     OUTPUT_STRIP_TRAILING_WHITESPACE)
    #   message(STATUS "docker local group id: ${DOCKER_GROUP_ID}")
    # endif()
    # if(DOCKER_USER_ID AND DOCKER_GROUP_ID)
    #   set(map_user TRUE)
    # endif()

    # #[===================================================================[
    #     rpm
    # #]===================================================================]
    # add_custom_target(rpm_container
    #   docker build
    #     --pull
    #     --build-arg GIT_COMMIT=${commit_hash}
    #     -t rippled-rpm-builder:${container_label}
    #     $<$<BOOL:${rpm_cache_from}>:--cache-from=${rpm_cache_from}>
    #     -f centos-builder/Dockerfile .
    #   WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
    #   VERBATIM
    #   USES_TERMINAL
    #   COMMAND_EXPAND_LISTS
    #   SOURCES
    #     Builds/containers/centos-builder/Dockerfile
    #     Builds/containers/centos-builder/centos_setup.sh
    #     Builds/containers/centos-builder/extras.sh
    #     Builds/containers/shared/update-rippled.sh
    #     Builds/containers/shared/update_sources.sh
    #     Builds/containers/shared/rippled.service
    #     Builds/containers/shared/rippled-reporting.service
    #     Builds/containers/shared/build_deps.sh
    #     Builds/containers/packaging/rpm/rippled.spec
    #     Builds/containers/packaging/rpm/build_rpm.sh
    #     Builds/containers/packaging/rpm/50-rippled.preset
    #     Builds/containers/packaging/rpm/50-rippled-reporting.preset
    #     bin/getRippledInfo
    # )
    # exclude_from_default(rpm_container)
    # add_custom_target(rpm
    #   docker run
    #     -e NIH_CACHE_ROOT=/opt/rippled_bld/pkg/.nih_c
    #     -v ${NIH_CACHE_ROOT}/pkgbuild:/opt/rippled_bld/pkg/.nih_c
    #     -v ${CMAKE_CURRENT_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
    #     -v ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
    #     "$<$<BOOL:${map_user}>:--volume=/etc/passwd:/etc/passwd;--volume=/etc/group:/etc/group;--user=${DOCKER_USER_ID}:${DOCKER_GROUP_ID}>"
    #     -t rippled-rpm-builder:${container_label}
    #     /bin/bash -c "cp -fpu rippled/Builds/containers/packaging/rpm/build_rpm.sh . && ./build_rpm.sh"
    #   VERBATIM
    #   USES_TERMINAL
    #   COMMAND_EXPAND_LISTS
    #   SOURCES
    #     Builds/containers/packaging/rpm/rippled.spec
    # )
    # exclude_from_default(rpm)
    # if(NOT have_package_container)
    #   add_dependencies(rpm rpm_container)
    # endif()
    #[===================================================================[
        dpkg
    #]===================================================================]

    set(DEFAULT_DISTRO "debian")
    set(DEFAULT_DISTRO_VERSION "11-slim")

    message("***************************************")
    message("Currently in ${CMAKE_CURRENT_SOURCE_DIR}")
    message("WORKING_DIR should be: ${CMAKE_CURRENT_SOURCE_DIR}/cmake/package/}")
    set(image_labels "cool=beans")
    list(APPEND labels "more=beans")
    list(APPEND labels "peas=too")
    message("***************************************")

    add_custom_target(packager_image
      docker build
        --debug
        --build-arg DISTRO=${DEFAULT_DISTRO}
        --build-arg DISTRO_VERSION=${DEFAULT_DISTRO_VERSION}
        --build-arg COMMIT_ID=${commit_hash}
        --label "my_label_is=cool"
        --label ${image_labels}
        --tag ${image}
        .
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cmake/package/
      VERBATIM
      USES_TERMINAL
      COMMAND_EXPAND_LISTS
      SOURCES
        cmake/package/Dockerfile
        cmake/package/pkg_build_dependencies.sh
        # deb/copyright
        # deb/rules
        # deb/rippled.links
        # deb/rippled.prerm
        # deb/rippled.postinst
        # deb/dirs
        # deb/rippled.postrm
        # deb/rippled.conffiles
        # deb/compat
        # deb/source/format
        # deb/source/local-options
        # deb/README.Debian
        # deb/rippled.install
        # deb/rippled.preinst
        # deb/docs
        # deb/control
        # pkg_files/packaging/dpkg/build_dpkg.sh
        # pkg_files/ubuntu-builder/ubuntu_setup.sh
        # pkg_files/bin/getRippledInfo
        # pkg_files/update-rippled.sh
        # pkg_files/update_sources.sh
        # pkg_files/build_deps.sh
        # pkg_files/rippled.service
        # pkg_files/rippled-logrotate
        # pkg_files/update-rippled-cron
    )
    set_target_properties(packager_image PROPERTIES EXCLUDE_FROM_ALL ON)
    set_target_properties(packager_image PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)

    add_custom_target(pkg
      docker run
        --env NIH_CACHE_ROOT=/opt/rippled_bld/pkg/.nih_c
        --volume ${NIH_CACHE_ROOT}/pkgbuild:/opt/rippled_bld/pkg/.nih_c
        --volume ${CMAKE_CURRENT_SOURCE_DIR}:/opt/rippled_bld/pkg/rippled
        --volume ${CMAKE_CURRENT_BINARY_DIR}/packages:/opt/rippled_bld/pkg/out
        --tty ${image}
        COMMENT "Building packages"
      # VERBATIM
      # USES_TERMINAL
      # COMMAND_EXPAND_LISTS
      # SOURCES
      #   pkg_files/control
    )
    set_target_properties(packager_image PROPERTIES EXCLUDE_FROM_ALL ON)
    set_target_properties(packager_image PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)

    if(NOT have_package_container)
      add_dependencies(pkg packager_image)
    endif()

    # #[===================================================================[
    #     ci container
    # #]===================================================================]
    # # now use the same ubuntu image for our travis-ci docker images,
    # # but we use a newer distro(18.04 vs 16.04).
    # #
    # # the following steps assume the github pkg repo, but it's possible to
    # # adapt these for other docker hub repositories.
    # #
    # # steps for publishing a new CI image when you make changes:
    # #
    # #   mkdir bld.ci && cd bld.ci && cmake -Dpackages_only=ON -Dcontainer_label=CI_LATEST
    # #   cmake --build . --target ci_container --verbose
    # #   docker tag rippled-ci-builder:CI_LATEST <HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD
    # #     (NOTE: change YYYY-MM-DD to match current date, or use a different
    # #             tag/version scheme if you prefer)
    # #   docker push <HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD
    # #     (NOTE: <HUB REPO PATH> is probably your user or org name if using
    # #             docker hub, or it might be something like
    # #             docker.pkg.github.com/ripple/rippled if using the github pkg
    # #             registry. for any registry, you will need to be logged-in via
    # #             docker and have push access.)
    # #
    # # ...then change the DOCKER_IMAGE line in .travis.yml :
    # #     - DOCKER_IMAGE="<HUB REPO PATH>/rippled-ci-builder:YYYY-MM-DD"
    # add_custom_target(ci_container
    #   docker build
    #     --pull
    #     --build-arg DISTRO=18.04
    #     --build-arg DISTRO_VERSION=
    #     --build-arg GIT_COMMIT=${commit_hash}
    #     --build-arg CI_USE=true
    #     -t rippled-ci-builder:${container_label}
    #     $<$<BOOL:${ci_cache_from}>:--cache-from=${ci_cache_from}>
    #     -f ubuntu-builder/Dockerfile .
    #   WORKING_DIRECTORY  ${CMAKE_CURRENT_SOURCE_DIR}/Builds/containers
    #   VERBATIM
    #   USES_TERMINAL
    #   COMMAND_EXPAND_LISTS
    #   SOURCES
    #     Builds/containers/ubuntu-builder/Dockerfile
    #     Builds/containers/ubuntu-builder/ubuntu_setup.sh
    #     Builds/containers/shared/build_deps.sh
    # )
    # exclude_from_default(ci_container)
  else()
    message(STATUS "docker NOT found -- won't be able to build containers for packaging")
  endif()
endif()
