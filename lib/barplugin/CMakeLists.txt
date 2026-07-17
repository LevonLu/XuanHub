# lib/barplugin — 插件接口与横切服务（STATIC 库）
# 用标准 add_library + 显式 AUTOMOC，确保 EventBus(Q_OBJECT) 的 moc 符号编入。
add_library(barplugin STATIC
    src/EventBus.cpp
    include/barplugin/EventBus.h
)

set_target_properties(barplugin PROPERTIES
    AUTOMOC ON
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(barplugin PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(barplugin PUBLIC Qt6::Core)
