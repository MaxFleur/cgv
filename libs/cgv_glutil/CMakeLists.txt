
file(GLOB_RECURSE SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
file(GLOB_RECURSE PPP_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.ph")
file(GLOB_RECURSE SHADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "glsl/*.gl*")

cgv_create_lib(cgv_glutil SOURCES ${SOURCES} PPP_SOURCES ${PPP_SOURCES} SHADER_SOURCES ${SHADERS}
	DEPENDENCIES cgv_utils cgv_type cgv_data cgv_base cgv_signal cgv_math cgv_media cgv_render cgv_gui cgv_reflect_types cgv_gl glew)

target_compile_definitions(cgv_glutil PRIVATE CGV_GLUTIL_EXPORTS)
target_compile_definitions(cgv_glutil_static PRIVATE CGV_GLUTIL_FORCE_STATIC)
if (WIN32)
	target_compile_definitions(cgv_glutil PRIVATE REGISTER_SHADER_FILES)
	target_compile_definitions(cgv_glutil_static PRIVATE REGISTER_SHADER_FILES)
endif ()
if (APPLE)
	target_compile_definitions(cgv_glutil PRIVATE __APPLGLSU__ GL_SILENCE_DEPRECATION)
	target_compile_definitions(cgv_glutil_static PRIVATE __APPLGLSU__ GL_SILENCE_DEPRECATION)
endif ()

target_link_libraries(cgv_glutil PUBLIC ${OPENGL_LIBRARIES})
target_link_libraries(cgv_glutil_static PUBLIC ${OPENGL_LIBRARIES})

install(DIRECTORY glsl/. DESTINATION ${CGV_LIBS_INCLUDE_DEST}/cgv_glutil/glsl)
