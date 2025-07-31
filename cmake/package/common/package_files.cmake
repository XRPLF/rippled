install(FILES
    ${CMAKE_CURRENT_LIST_DIR}/rippled-logrotate
    bin/getRippledInfo
    ${CMAKE_CURRENT_LIST_DIR}/update-rippled.sh
    TYPE BIN
)

install(FILES
    ${CMAKE_CURRENT_LIST_DIR}/update-rippled-cron
    TYPE SYSCONF
)

install(FILES
    ${CMAKE_CURRENT_LIST_DIR}/rippled.service
    DESTINATION /lib/systemd/system
)
