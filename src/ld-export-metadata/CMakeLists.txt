add_executable(ld-export-metadata
    audacity.cpp
    closedcaptions.cpp
    csv.cpp
    ffmetadata.cpp
    metadataexportdialog.cpp
    metadataexportdialog.ui
    main.cpp
)
target_link_libraries(ld-export-metadata PRIVATE Qt::Core Qt::Gui Qt::Widgets lddecode-library)
target_link_libraries(ld-export-metadata PRIVATE Qt::Core lddecode-library)

install(TARGETS ld-export-metadata)
