file(GLOB_RECURSE SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cxx")
file(GLOB_RECURSE HEADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.h")
file(GLOB_RECURSE SHADERS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.gl*")

set(ALL_SOURCES ${SOURCES} ${HEADERS} ${IMG_SOURCES} ${ST_FILES} ${SHADERS} ${IMAGES})

add_library(mdtf_myofibrils SHARED ${ALL_SOURCES})

target_link_libraries(mdtf_myofibrils
	glew
	cgv_gl
	cgv_type
	cgv_base
    cgv_signal
	cgv_reflect
	cgv_os
	cgv_gui
	cgv_data
	cgv_utils
	cgv_glutil
	cgv_render
	cgv_media
	plot)

add_dependencies(mdtf_myofibrils
	cgv_viewer
	cg_ext
	cg_fltk
	cg_meta
	cmi_io
	cgv_reflect_types
	crg_light
	cg_icons
	crg_antialias
	crg_depth_of_field
	crg_grid
	crg_stereo_view)

set_plugin_execution_params(mdtf_myofibrils
"plugin:cg_fltk \
\"type(shader_config):shader_path='${CMAKE_SOURCE_DIR}/glsl;${CGV_DIR}/libs/plot/glsl;${CGV_DIR}/libs/cgv_gl/glsl;${CGV_DIR}/libs/cgv_glutil/glsl'\" \
plugin:cg_ext \
plugin:crg_stereo_view \
plugin:cg_meta \
plugin:cmi_io \
plugin:crg_light \
plugin:cg_icons \
plugin:crg_antialias \
plugin:crg_depth_of_field \
plugin:crg_grid \
plugin:mdtf_myofibrils \
config:\"${CMAKE_SOURCE_DIR}/config.def\"\
")
configure_file(run_plugin.sh.in ${CMAKE_BINARY_DIR}/run_plugin.sh 
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        @ONLY)

install(TARGETS mdtf_myofibrils EXPORT cgv_plugins DESTINATION ${CGV_BIN_DEST})
