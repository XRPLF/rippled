include(isolate_headers)

function (xrpl_add_test name)
    set(target ${PROJECT_NAME}.test.${name})

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
    add_executable(${target} ${ARGN} ${sources})

    isolate_headers(${target} "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/tests/${name}" PRIVATE)

    add_test(NAME ${target} COMMAND ${target})
endfunction ()
