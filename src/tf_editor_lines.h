/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/generic_renderer.h>

#include "tf_editor_basic.h"

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

	void update_point_positions();

	void point_clicked(const vec2& mouse_pos);

	void scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed);

	void handle_interacted_primitive_ids(int i, int j, bool set_corresponding_points = false);

private:

	// renderer for the 2d plot lines and quadstrips
	cgv::glutil::generic_renderer m_renderer_lines;
	cgv::glutil::generic_renderer m_renderer_strips;
	cgv::glutil::generic_renderer m_renderer_strips_gauss;

	// Geometry for the quadstrips and line relations
	std::vector<tf_editor_shared_data_types::polygon_geometry> m_geometry_strips;
	tf_editor_shared_data_types::line_geometry m_geometry_relations;

	// widget and strip border lines
	tf_editor_shared_data_types::line_geometry m_geometry_widgets;
	tf_editor_shared_data_types::line_geometry m_geometry_strip_borders;

	cgv::glutil::line2d_style m_style_relations;
	cgv::glutil::line2d_style m_style_widgets;
	cgv::glutil::line2d_style m_style_strip_borders;
	cgv::glutil::shape2d_style m_style_polygons;

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
};

typedef cgv::data::ref_ptr<tf_editor_lines> tf_editor_lines_ptr;

/** END - MFLEURY **/