add_executable(testexportarguments
    testexportarguments.cpp
    ../exportarguments.cpp
)

target_include_directories(testexportarguments PRIVATE
    ..
)

target_link_libraries(testexportarguments PRIVATE
    Qt::Core
)

add_test(NAME testexportarguments COMMAND testexportarguments)
