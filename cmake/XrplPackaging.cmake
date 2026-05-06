#[===================================================================[
   Linux packaging support: 'package' target + install-test fixtures.

   The packaging script (package/build_pkg.sh) installs to FHS-standard
   paths (/usr/bin, /etc/xrpld, etc.) regardless of CMAKE_INSTALL_PREFIX,
   so no prefix guard is needed here.
#]===================================================================]
if(NOT is_linux)
    message(STATUS "Packaging not supported on non-Linux hosts")
    return()
endif()

if(NOT DEFINED pkg_release)
    set(pkg_release 1)
endif()

find_program(RPMBUILD_EXECUTABLE rpmbuild)
find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)

if(NOT (RPMBUILD_EXECUTABLE OR DPKG_BUILDPACKAGE_EXECUTABLE))
    message(
        STATUS
        "Neither rpmbuild nor dpkg-buildpackage found; 'package' target not available"
    )
    return()
endif()

set(package_env
    SRC_DIR=${CMAKE_SOURCE_DIR}
    BUILD_DIR=${CMAKE_BINARY_DIR}
    PKG_VERSION=${xrpld_version}
    PKG_RELEASE=${pkg_release}
)

add_custom_target(
    package
    COMMAND
        ${CMAKE_COMMAND} -E env ${package_env}
        ${CMAKE_SOURCE_DIR}/package/build_pkg.sh
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS xrpld
    COMMENT "Building Linux package (deb/rpm inferred from host tooling)"
    VERBATIM
)

#[===================================================================[
   CTest fixtures for package install verification (requires docker)
#]===================================================================]

find_program(DOCKER_EXECUTABLE docker)
if(NOT DOCKER_EXECUTABLE)
    message(STATUS "docker not found; package install tests not available")
    return()
endif()

# The host has at most one packaging toolchain available (we returned earlier
# if neither was found), so the smoketest runs in the matching distro image.
# The tool's presence implies the OS family.
if(DPKG_BUILDPACKAGE_EXECUTABLE)
    set(IMAGE
        "geerlingguy/docker-ubuntu2204-ansible@sha256:bbe4c56c16c57c902554b9a47833590926b7a7d4440aef3d9851473b9f7be9d4"
    )
else()
    set(IMAGE
        "geerlingguy/docker-rockylinux9-ansible@sha256:790c2db9add93c0daa903ace816f352c9c04abb046ecfa12c581e8d4c59f41d6"
    )
endif()

set(CONTAINER xrpld_install_test)

# Run systemd as PID 1 so the package installation can register and start
# xrpld.service exactly as it does on a real host. That requires:
#   --cgroupns host + /sys/fs/cgroup mount: systemd needs to manage cgroups
#   --tmpfs /run:                           systemd's runtime dir
#   --tmpfs /run/lock:                      Debian mounts this as a separate
#                                           tmpfs (with nosuid/nodev/noexec)
#                                           and systemd expects it as a mount
#                                           point at boot; RHEL doesn't, but
#                                           the extra mount is harmless there.
add_test(
    NAME package_container_setup
    COMMAND
        sh -c
        "docker rm -f ${CONTAINER} 2>/dev/null; \
        docker run -d \
            --name ${CONTAINER} \
            --cgroupns host \
            --volume '${CMAKE_SOURCE_DIR}:/root:ro' \
            --volume /sys/fs/cgroup:/sys/fs/cgroup:rw \
            --tmpfs /run \
            --tmpfs /run/lock \
            ${IMAGE}"
)
set_tests_properties(
    package_container_setup
    PROPERTIES FIXTURES_SETUP package_container LABELS packaging
)

# On CI: always stop. Locally: leave running on failure for diagnosis.
if(is_ci)
    add_test(NAME package_container_teardown COMMAND docker rm -f ${CONTAINER})
else()
    add_test(
        NAME package_container_teardown
        COMMAND
            sh -c
            "if docker exec ${CONTAINER} test -f /tmp/test_failed 2>/dev/null; then \
                echo 'Tests failed — leaving ${CONTAINER} running for diagnosis'; \
                echo 'Clean up with: docker rm -f ${CONTAINER}'; \
            else \
                docker rm -f ${CONTAINER}; \
            fi"
    )
endif()
set_tests_properties(
    package_container_teardown
    PROPERTIES FIXTURES_CLEANUP package_container LABELS packaging
)

add_test(
    NAME package_install
    COMMAND
        docker exec -w /root ${CONTAINER} bash /root/package/test/smoketest.sh
        local
)
set_tests_properties(
    package_install
    PROPERTIES
        FIXTURES_REQUIRED package_container
        FIXTURES_SETUP package_installed
        LABELS packaging
        TIMEOUT 600
)

add_test(
    NAME package_install_paths
    COMMAND
        docker exec -w /root ${CONTAINER} sh
        /root/package/test/check_install_paths.sh
)
set_tests_properties(
    package_install_paths
    PROPERTIES
        FIXTURES_REQUIRED "package_container;package_installed"
        LABELS packaging
        TIMEOUT 60
)
