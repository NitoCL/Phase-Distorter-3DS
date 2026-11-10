# Validation-only toolchain: compiles the portable core for the 3DS CPU
# (ARM11 MPCore, ARMv6K + VFPv2, hard float) with a generic 32-bit ARM GCC.
# It checks 32-bit portability and code size; the real build uses devkitARM.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(EB_3DS_ARCH "-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -marm")
set(CMAKE_C_FLAGS_INIT "${EB_3DS_ARCH}")
set(CMAKE_CXX_FLAGS_INIT "${EB_3DS_ARCH}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
