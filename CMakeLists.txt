cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32_project)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
    Core/Src/main.c
    Core/Src/ds18b20.c
    Core/Src/hcsr04.c
    Core/Inc/hx711.h
    Core/Src/hx711.c
    Core/Inc/dht11.h
    Core/Src/dht11.c
    Core/Src/sgp40.c
    Core/Inc/sgp40.h
    Core/Inc/sensirion_gas_index_algorithm.h
    Core/Src/sensirion_gas_index_algorithm.c
    Core/Src/ens160_aht21.c
    Core/Inc/ens160_aht21.h
    Core/Inc/bme68x_defs.h
    Core/Inc/bme68x.h
    Core/Src/bme68x.c
    Core/Inc/bme688_port.h
    Core/Src/bme688_port.c
    Core/Inc/w25q64.h
    Core/Src/w25q64.c
    Core/Inc/system_data.h
    Core/Src/system_data.c
    Core/Inc/control.h
    Core/Src/control.c
    Core/Inc/usb_reporter.h
    Core/Src/usb_reporter.c
    Core/Inc/enose.h
    Core/Src/enose.c
    Core/Inc/bsec_datatypes.h
    Core/Inc/bsec_interface.h
    Core/Src/bsec_config.c
    Core/Src/bme688_bsec_app.c
    Core/Inc/bme688_bsec_app.h

)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
# 1. 声明一个外部导入的静态库
add_library(bsec_algo STATIC IMPORTED)

# 2. 绝对绑定静态库的物理路径 (告诉 CMake 去这里拿算法实体)
set_target_properties(bsec_algo PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/Core/Lib/libalgobsec.a)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    bsec_algo   # <--- 把 AI 核心库链接进来！
    m           # <--- 链接标准的 C 数学库 (算法必须用到 math.h)

    # Add user defined libraries
)
#-------------------------------------------------------------------------------
# 开启 printf 的浮点数(float)打印功能
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
# 强制链接 C 语言标准数学库 (lm)
target_link_libraries(${CMAKE_PROJECT_NAME}  m)
