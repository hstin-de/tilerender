add_executable(
    tilerender
    main.cpp
)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

target_include_directories(
    tilerender
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/maplibre-gl-native/include
)

target_link_libraries(
    tilerender
    PRIVATE
        Mapbox::Base
        Mapbox::Base::Extras::args
        mbgl-compiler-options
        mbgl-core
)

set_target_properties(tilerender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS tilerender
    RUNTIME DESTINATION bin
)
