cmake_minimum_required(VERSION 3.15)
project(video-scale VERSION 1.0)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# ${PROJECT_NAME}
set(LIB_SOURCES
        src/vscale.c
)

add_library(${PROJECT_NAME} ${LIB_SOURCES})

# checks if set up rpath exists for install
if(COMMAND set_up_rpath)
    set_up_rpath()
else()
    message("Set up rpath not defined!")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                            $<INSTALL_INTERFACE:include>
)

target_compile_definitions(${PROJECT_NAME} PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(${PROJECT_NAME} PRIVATE "-fvisibility=hidden")
target_compile_options(${PROJECT_NAME} PRIVATE "-std=gnu99")

target_link_libraries(${PROJECT_NAME} 
                            PRIVATE
                                pomp 
                                ulog 
                                futils
                                media-buffers 
                                video-defs 
                                ${PROJECT_NAME}-core 
                                ${PROJECT_NAME}-libyuv
)

# ${PROJECT_NAME}-core
set(LIB_SOURCES
        core/src/vscale_core.c
        core/src/vscale_enums.c
)

add_library(${PROJECT_NAME}-core ${LIB_SOURCES})

target_include_directories(${PROJECT_NAME}-core PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
                            $<INSTALL_INTERFACE:include>
)

target_compile_definitions(${PROJECT_NAME}-core PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(${PROJECT_NAME}-core PRIVATE "-fvisibility=hidden")
target_compile_options(${PROJECT_NAME}-core PRIVATE "-std=gnu99")

target_link_libraries(${PROJECT_NAME}-core
                        PRIVATE
                        pomp
                        futils
                        media-buffers
                        ulog
                        video-defs
                        video-metadata
)

# ${PROJECT_NAME}-libyuv
set(LIB_SOURCES
        libyuv/src/vscale_libyuv.c
)

add_library(${PROJECT_NAME}-libyuv ${LIB_SOURCES})

target_include_directories(${PROJECT_NAME}-libyuv PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libyuv/include>
                            $<INSTALL_INTERFACE:include>
)

# state that libpomp need PIC when the default is shared libraries
set_target_properties(${PROJECT_NAME}-libyuv PROPERTIES
                    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

target_compile_definitions(${PROJECT_NAME}-libyuv PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(${PROJECT_NAME}-libyuv PRIVATE "-fvisibility=hidden")
target_compile_options(${PROJECT_NAME}-libyuv PRIVATE "-std=gnu11")


target_link_libraries(${PROJECT_NAME}-libyuv
                        PRIVATE
                        futils
                        media-buffers
                        media-buffers-memory
                        media-buffers-memory-generic
                        pomp
                        ulog
                        video-defs
                        video-metadata
                        ${PROJECT_NAME}-core
                        yuv
)

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    PUBLIC_HEADER DESTINATION include
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS ${PROJECT_NAME}-core
    EXPORT ${PROJECT_NAME}-core-targets
    PUBLIC_HEADER DESTINATION include
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(TARGETS ${PROJECT_NAME}-libyuv
    EXPORT ${PROJECT_NAME}-libyuv-targets
    PUBLIC_HEADER DESTINATION include
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
