cmake_minimum_required(VERSION 3.12)

file(GLOB_RECURSE SRC_FILES
        CONFIGURE_DEPENDS
        Src/*.cpp
        Src/*.c
        Src/*.hpp
        Src/*.h
)

add_library(BulletTracer SHARED
        ${SRC_FILES}
)

target_include_directories(BulletTracer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_link_libraries(BulletTracer PRIVATE
        ZHMModSDK
)


# Only the file name of the .pdb goes into the DLL, not the path of the machine it was built on.
target_link_options(BulletTracer PRIVATE "/PDBALTPATH:%_PDB%")
