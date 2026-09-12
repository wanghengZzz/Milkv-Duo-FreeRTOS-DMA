file(GLOB _SOURCES "src/*.c")
file(GLOB _HEADERS "include/*.h")

include_directories(include)
include_directories(../config)
include_directories(../uart/include)
include_directories(${TOP_DIR}/common/include/riscv64)

add_library(haldma OBJECT ${_SOURCES})

install(FILES ${_HEADERS} DESTINATION include/hal/dma)
