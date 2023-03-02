cmake_minimum_required(VERSION 3.15)
project(libvideo-scale VERSION 1.0)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# video-scale
set(LIB_SOURCES
        src/vscale.c
)

add_library(video-scale ${LIB_SOURCES})

set_target_properties(video-scale PROPERTIES
                    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

target_include_directories(video-scale PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                            $<INSTALL_INTERFACE:include>
)

target_compile_definitions(video-scale PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(video-scale PRIVATE "-fvisibility=hidden")
target_compile_options(video-scale PRIVATE "-std=gnu99")

target_link_libraries(video-scale 
                                pomp 
                                ulog 
                                media-buffers 
                                video-defs 
                                video-scale-core 
                                video-scale-libyuv
)

# video-scale-core
set(LIB_SOURCES
        core/src/vscale_core.c
        core/src/vscale_enums.c
)

add_library(video-scale-core ${LIB_SOURCES})
set_target_properties(video-scale-core PROPERTIES
                    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

target_include_directories(video-scale-core PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
                            $<INSTALL_INTERFACE:include>
)

target_compile_definitions(video-scale-core PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(video-scale-core PRIVATE "-fvisibility=hidden")
target_compile_options(video-scale-core PRIVATE "-std=gnu99")

target_link_libraries(video-scale-core
                        pomp
                        futils
                        media-buffers
                        ulog
                        video-defs
                        video-metadata
)

# video-scale-libyuv
set(LIB_SOURCES
        libyuv/src/vscale_libyuv.c
)

add_library(video-scale-libyuv STATIC ${LIB_SOURCES})
set_target_properties(video-scale-libyuv PROPERTIES
                    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

target_include_directories(video-scale-libyuv PUBLIC
                            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libyuv/include>
                            $<INSTALL_INTERFACE:include>
)

target_compile_definitions(video-scale-libyuv PRIVATE "VSCALE_API_EXPORTS")
target_compile_options(video-scale-libyuv PRIVATE "-fvisibility=hidden")
target_compile_options(video-scale-libyuv PRIVATE "-std=gnu11")


target_link_libraries(video-scale-libyuv
                        futils
                        media-buffers
                        media-buffers-memory
                        media-buffers-memory-generic
                        pomp
                        ulog
                        video-defs
                        video-metadata
                        video-scale-core
                        yuv
)