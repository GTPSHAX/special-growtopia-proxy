add_subdirectory(enet_growtopia)
add_subdirectory(glm)
add_library(mongoose STATIC mongoose/mongoose.c)
target_include_directories(mongoose PUBLIC mongoose)
