add_executable(ld-process-vbi
    biphasecode.cpp
    closedcaption.cpp
    decoderpool.cpp
    main.cpp
    fmcode.cpp
    teletextintegration.cpp
    vbilinedecoder.cpp
    videoid.cpp
    vitccode.cpp
    whiteflag.cpp
)

target_link_libraries(ld-process-vbi PRIVATE Qt::Core lddecode-library)
set(_teletext_vendor_dir "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vhs-teletext")
target_compile_definitions(ld-process-vbi PRIVATE
    TELETEXT_VENDOR_DIR="${_teletext_vendor_dir}"
)

add_custom_command(TARGET ld-process-vbi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ld-process-vbi>/vendor/vhs-teletext"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${_teletext_vendor_dir}"
        "$<TARGET_FILE_DIR:ld-process-vbi>/vendor/vhs-teletext"
)

install(TARGETS ld-process-vbi)
install(DIRECTORY "${_teletext_vendor_dir}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/vendor/vhs-teletext)

if(BUILD_TESTING)
    add_test(
        NAME ld-process-vbi-teletext-help
        COMMAND $<TARGET_FILE:ld-process-vbi> --help
    )
    set_tests_properties(ld-process-vbi-teletext-help PROPERTIES
        PASS_REGULAR_EXPRESSION "--teletext-html-dir"
    )
endif()
