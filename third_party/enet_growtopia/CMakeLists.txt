file(GLOB ENET_GROWTOPIa_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

add_library(enet_growtopia STATIC
    ${ENET_GROWTOPIa_SOURCES}
)

target_include_directories(enet_growtopia PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
