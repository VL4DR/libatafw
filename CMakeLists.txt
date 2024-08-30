cmake_minimum_required(VERSION 3.15)

project(libatafw
	C
	ASM
)

set(LIBATAFW_TARGET_NAME	libatafw)

set(LIBATAFW_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/src/ata_fw/ata_fw.c
)

add_library(${LIBATAFW_TARGET_NAME}
	${LIBATAFW_SOURCES}
)

target_include_directories(${LIBATAFW_TARGET_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(${LIBATAFW_TARGET_NAME} PUBLIC SYSTEM
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
