#[===================================================================[
   Linux packaging support: RPM and Debian targets + install tests
#]===================================================================]

if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/opt/xrpld")
    message(
        STATUS
        "Packaging targets require -DCMAKE_INSTALL_PREFIX=/opt/xrpld "
        "(current: '${CMAKE_INSTALL_PREFIX}'); skipping."
    )
    return()
endif()

if(NOT DEFINED pkg_release)
    set(pkg_release 1)
endif()

find_program(RPMBUILD_EXECUTABLE rpmbuild)
find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)
if(RPMBUILD_EXECUTABLE OR DPKG_BUILDPACKAGE_EXECUTABLE)
    add_custom_target(
        package
        COMMAND
            ${CMAKE_SOURCE_DIR}/package/build_pkg.sh ${CMAKE_SOURCE_DIR}
            ${CMAKE_BINARY_DIR} "${xrpld_version}" ${pkg_release}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS xrpld
        COMMENT "Building Linux package (deb/rpm inferred from host tooling)"
        VERBATIM
    )
else()
    message(
        STATUS
        "Neither rpmbuild nor dpkg-buildpackage found; 'package' target not available"
    )
endif()

#[===================================================================[
   CTest fixtures for package install verification (requires docker)
#]===================================================================]

find_program(DOCKER_EXECUTABLE docker)
if(NOT DOCKER_EXECUTABLE)
    message(STATUS "docker not found; package install tests not available")
    return()
endif()

set(DEB_TEST_IMAGE
    "geerlingguy/docker-ubuntu2204-ansible@sha256:bbe4c56c16c57c902554b9a47833590926b7a7d4440aef3d9851473b9f7be9d4"
)
set(RPM_TEST_IMAGE
    "geerlingguy/docker-rockylinux9-ansible@sha256:790c2db9add93c0daa903ace816f352c9c04abb046ecfa12c581e8d4c59f41d6"
)

# Only register install-test fixtures for package formats the host can build,
# since the smoketest needs a corresponding .deb/.rpm artifact in build/.
set(PKG_TYPES "")
if(DPKG_BUILDPACKAGE_EXECUTABLE)
    list(APPEND PKG_TYPES deb)
endif()
if(RPMBUILD_EXECUTABLE)
    list(APPEND PKG_TYPES rpm)
endif()

foreach(PKG IN LISTS PKG_TYPES)
    if(PKG STREQUAL "deb")
        set(IMAGE ${DEB_TEST_IMAGE})
    else()
        set(IMAGE ${RPM_TEST_IMAGE})
    endif()

    # This image runs systemd for full testing xrpld.service
    add_test(
        NAME ${PKG}_container_start
        COMMAND
            sh -c
            "docker rm -f xrpld_${PKG}_install_test 2>/dev/null || true && \
            docker run -d \
            --name xrpld_${PKG}_install_test \
            --cgroupns host \
            --volume '${CMAKE_SOURCE_DIR}:/root:ro' \
            --volume /sys/fs/cgroup:/sys/fs/cgroup:rw \
            --tmpfs /run/lock \
            ${IMAGE}"
    )
    set_tests_properties(
        ${PKG}_container_start
        PROPERTIES FIXTURES_SETUP ${PKG}_container LABELS packaging
    )

    # On CI: always stop. Locally: leave running on failure for diagnosis.
    add_test(
        NAME ${PKG}_container_stop
        COMMAND
            sh -c
            "if [ -n \"$CI\" ] || ! docker exec xrpld_${PKG}_install_test test -f /tmp/test_failed 2>/dev/null; then \
            docker rm -f xrpld_${PKG}_install_test; \
        else \
            echo 'Tests failed — leaving xrpld_${PKG}_install_test running for diagnosis'; \
            echo 'Clean up with: docker rm -f xrpld_${PKG}_install_test'; \
        fi"
    )
    set_tests_properties(
        ${PKG}_container_stop
        PROPERTIES FIXTURES_CLEANUP ${PKG}_container LABELS packaging
    )

    add_test(
        NAME ${PKG}_install
        COMMAND
            docker exec -w /root xrpld_${PKG}_install_test bash
            /root/package/test/smoketest.sh local
    )
    set_tests_properties(
        ${PKG}_install
        PROPERTIES
            FIXTURES_REQUIRED ${PKG}_container
            FIXTURES_SETUP ${PKG}_installed
            LABELS packaging
            TIMEOUT 600
    )

    add_test(
        NAME ${PKG}_install_paths
        COMMAND
            docker exec -w /root xrpld_${PKG}_install_test sh
            /root/package/test/check_install_paths.sh
    )
    set_tests_properties(
        ${PKG}_install_paths
        PROPERTIES
            FIXTURES_REQUIRED "${PKG}_container;${PKG}_installed"
            LABELS packaging
            TIMEOUT 60
    )
endforeach()
