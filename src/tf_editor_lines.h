/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/generic_renderer.h>

#include "tf_editor_basic.h"
#include "scan_and_compact.h"

/* 
This class provides the line based editor of the transfer function. Added primitives can be modified here. 
If primitive values were modified, they are synchronized with the GUI and scatterplot.
*/
class tf_editor_lines : public tf_editor_basic {

	void reload_shaders();

public:
	tf_editor_lines();
	std::string get_type_name() const { return "tf_editor_lines_overlay"; }

	void clear(cgv::render::context& ctx);

	bool handle_event(cgv::gui::event& e);

	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);

	void init_frame(cgv::render::context& ctx);
	
	void create_gui();
	
	void resynchronize();

	void primitive_added();

	void set_data_set(sliced_volume_data_set* data_set_ptr) {
		m_data_set_ptr = data_set_ptr;
		create_labels();

		auto ctx_ptr = get_context();
		if(ctx_ptr && m_data_set_ptr) {
		
			//unsigned width = m_data_set_ptr->volume_tex.get_resolution(0);
			//unsigned height = m_data_set_ptr->volume_tex.get_resolution(1);
			//unsigned depth = m_data_set_ptr->volume_tex.get_resolution(2);
			//
			//unsigned n = width * height * depth; // number of data points
			unsigned n = m_data_set_ptr->voxel_data.size();
			std::cout << "VOXEL DATA SIZE " << n << std::endl;
			
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, index_buffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int) * n, (void*)0, GL_DYNAMIC_COPY);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

			sac.set_vote_prog_name("volume_vote");
			//sac.set_data_type_override("int x;");// int y; ");
			//sac.set_vote_definition_override("return (value.x & 1) == 0; ");
			//sac.set_vote_definition_override("return value.x == 5;");

			sac.set_mode(cgv::gpgpu::scan_and_compact::M_CREATE_INDICES);

			sac.init(*ctx_ptr, n);
		}
	}

private:

	void init_styles(cgv::render::context& ctx);

	void update_content() override;

	void create_labels() override;

	void create_widget_lines();

	void create_centroid_boundaries();

	void create_strips();

	void create_strip_borders(int index);

	void add_widget_lines();

	void add_draggables(int centroid_index);

	void draw_content(cgv::render::context& ctx) override;

	bool draw_plot(cgv::render::context& ctx);

	void set_point_positions();

	void set_point_handles();

	void update_point_positions();

	void point_clicked(const vec2& mouse_pos);

	void scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed);

private:

	// renderer for the 2d plot lines and quadstrips
	cgv::glutil::generic_renderer m_renderer_lines;
	cgv::glutil::generic_renderer m_renderer_strips;

	// Geometry for the quadstrips and line relations
	tf_editor_shared_data_types::polygon_geometry2 m_geometry_strips;
	tf_editor_shared_data_types::line_geometry m_geometry_relations;

	// widget and strip border lines
	tf_editor_shared_data_types::line_geometry m_geometry_widgets;
	tf_editor_shared_data_types::line_geometry m_geometry_strip_borders;

	cgv::glutil::line2d_style m_style_relations;
	cgv::glutil::line2d_style m_style_widgets;
	cgv::glutil::line2d_style m_style_polygons;
	cgv::glutil::line2d_style m_style_strip_borders;

	cgv::glutil::shape2d_style m_style_plot;

	cgv::glutil::arrow2d_style m_style_arrows;

	std::vector<tf_editor_shared_data_types::line> m_widget_lines;
	std::vector<tf_editor_shared_data_types::polygon> m_widget_polygons;

	// Boundaries of the centroid points
	std::vector<std::vector<vec2>> m_strip_boundary_points;

	std::vector<std::vector<tf_editor_shared_data_types::point_line>> m_points;
	cgv::glutil::draggables_collection<tf_editor_shared_data_types::point_line*> m_point_handles;

	rgba m_gray_widgets{ 0.4f, 0.4f, 0.4f, 1.0f };
	rgba m_gray_arrows{ 0.4f, 0.4f, 0.4f, 1.0f };

	// Store the indices of to be updated centroids if a point has been interacted with
	int m_interacted_primitive_ids[4] = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };
	// Were strips created?
	bool m_strips_created = true;
	// Do we need to update all values?
	bool m_create_all_values = true;










	enum Mode {
		M_COMPUTE = 0,
		M_VERTEX = 1,
		M_GEOM = 2,
		M_COMPUTE_2 = 3
	} m_mode = M_COMPUTE_2;
	GLuint plot_buffer;
	texture plot_texture;

	ivec2 plot_resolution;
	GLuint plot_buffers2[6];
	texture plot_textures[6];
	cgv::glutil::shader_library shaders;

	GLuint index_buffer = 0;
	unsigned filtered_count = 0;
	cgv::gpgpu::scan_and_compact sac;




};

typedef cgv::data::ref_ptr<tf_editor_lines> tf_editor_lines_ptr;

/** END - MFLEURY **/