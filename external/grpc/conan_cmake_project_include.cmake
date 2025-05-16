# TODO: move to a patch? It avoids link errors while resolving abseil symbols with gcc
if (TARGET check_epollexclusive)
    set_target_properties(check_epollexclusive PROPERTIES LINKER_LANGUAGE CXX)
endif()
