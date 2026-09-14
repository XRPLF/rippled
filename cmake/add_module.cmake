include(isolate_headers)
include(verify_headers)

# Create an OBJECT library target named
#
#   ${PROJECT_NAME}.lib${parent}.${name}
#
# with sources in src/lib${parent}/${name}
# and headers in include/${parent}/${name}
# that cannot include headers from other directories in include/
# unless they come through linked libraries.
#
# add_module(parent a)
# add_module(parent b)
# target_link_libraries(project.libparent.b PUBLIC project.libparent.a)
function(add_module parent name)
    set(target ${PROJECT_NAME}.lib${parent}.${name})
    add_library(${target} OBJECT)
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib${parent}/${name}/*.cpp"
    )
    target_sources(${target} PRIVATE ${sources})
    target_include_directories(
        ${target}
        PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    isolate_headers(
        ${target}
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/${parent}/${name}"
        PUBLIC
    )
    isolate_headers(
        ${target}
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib${parent}/${name}"
        PRIVATE
    )
    # protocol_autogen contains generated headers that are deliberately exempt
    # from clang-tidy (see ExcludeHeaderFilterRegex in .clang-tidy), so we do not
    # verify them either.
    if(
        verify_headers
        AND NOT "${parent}/${name}" STREQUAL "xrpl/protocol_autogen"
    )
        verify_target_headers(
            ${target}
            "${CMAKE_CURRENT_SOURCE_DIR}/include/${parent}/${name}"
        )
        verify_target_headers(
            ${target}
            "${CMAKE_CURRENT_SOURCE_DIR}/src/lib${parent}/${name}"
        )
    endif()
endfunction()
