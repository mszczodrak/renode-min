cmake_minimum_required(VERSION 2.8)

project (tcg)

option (BIG_ENDIAN "Big endian" OFF)
set (HOST_LONG_BITS 0 CACHE INT "Host word size (0 - autodetection)")
set (HOST_ARCHITECTURE "i386" CACHE STRING "Host architecture")
set_property (CACHE HOST_ARCHITECTURE PROPERTY STRINGS i386 arm)
set (TARGET_LONG_BITS 32 CACHE INT "Target word size")
set (TARGET_INSN_START_EXTRA_WORDS 0 CACHE INT "Number of extra words in insn_start needed by the target")

if(HOST_LONG_BITS EQUAL 0)
    # Determine bitness
    # 32 or 64 bits
    set (HOST_LONG_BITS 32 CACHE INT "Host word size (0 - autodetection)" FORCE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set (HOST_LONG_BITS 64 CACHE INT "Host word size (0 - autodetection)" FORCE)
    endif()
endif()

# Make sure host architecture is lower case,
# as later we are including folder with this name
string (TOLOWER ${HOST_ARCHITECTURE} HOST_LOWER)

if(BIG_ENDIAN)
    set (BIG_ENDIAN_DEF -DTARGET_WORDS_BIGENDIAN)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-fomit-frame-pointer)
endif()

add_definitions (
    -fPIC

    -DTARGET_LONG_BITS=${TARGET_LONG_BITS}
    -DHOST_LONG_BITS=${HOST_LONG_BITS}
    -DTARGET_INSN_START_EXTRA_WORDS=${TARGET_INSN_START_EXTRA_WORDS}

    ${BIG_ENDIAN_DEF}
    )

include_directories (
    ${HOST_LOWER}
    ${CMAKE_SOURCE_DIR}
    )

add_library (tcg STATIC
    additional.c
    host-utils.c
    optimize.c
    tcg.c
    tcg-runtime.c
    )

