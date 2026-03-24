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

# Generate the RPM spec from template (substitutes @xrpld_version@, @pkg_release@).
if(NOT DEFINED pkg_release)
    set(pkg_release 1)
endif()
configure_file(
    ${CMAKE_SOURCE_DIR}/package/rpm/xrpld.spec.in
    ${CMAKE_BINARY_DIR}/package/rpm/xrpld.spec
    @ONLY
)

find_program(RPMBUILD_EXECUTABLE rpmbuild)
if(RPMBUILD_EXECUTABLE)
    add_custom_target(
        package-rpm
        COMMAND
            ${CMAKE_SOURCE_DIR}/package/build_pkg.sh rpm ${CMAKE_SOURCE_DIR}
            ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building RPM package"
        VERBATIM
    )
else()
    message(STATUS "rpmbuild not found; 'package-rpm' target not available")
endif()

find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)
if(DPKG_BUILDPACKAGE_EXECUTABLE)
    add_custom_target(
        package-deb
        COMMAND
            ${CMAKE_SOURCE_DIR}/package/build_pkg.sh deb ${CMAKE_SOURCE_DIR}
            ${CMAKE_BINARY_DIR} ${xrpld_version}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Debian package"
        VERBATIM
    )
else()
    message(
        STATUS
        "dpkg-buildpackage not found; 'package-deb' target not available"
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

set(DEB_TEST_IMAGE "geerlingguy/docker-ubuntu2204-ansible:latest")
set(RPM_TEST_IMAGE "geerlingguy/docker-rockylinux9-ansible:latest")

foreach(PKG deb rpm)
    if(PKG STREQUAL "deb")
        set(IMAGE ${DEB_TEST_IMAGE})
    else()
        set(IMAGE ${RPM_TEST_IMAGE})
    endif()

    # Fixture: start container
    add_test(
        NAME ${PKG}_container_start
        COMMAND
            sh -c
            "docker rm -f xrpld_${PKG}_install_test 2>/dev/null || true && \
            docker run --rm -d \
            --name xrpld_${PKG}_install_test \
            --memory=45g --memory-swap=45g \
            --privileged \
            --cgroupns host \
            --volume '${CMAKE_SOURCE_DIR}:/root:ro' \
            --volume /sys/fs/cgroup:/sys/fs/cgroup:rw \
            --tmpfs /tmp --tmpfs /run --tmpfs /run/lock \
            ${IMAGE} \
            /usr/sbin/init"
    )
    set_tests_properties(
        ${PKG}_container_start
        PROPERTIES FIXTURES_SETUP ${PKG}_container LABELS packaging
    )

    # Fixture: stop container
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

    # Install package and run smoke test
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

    # Validate install paths and compat symlinks
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
