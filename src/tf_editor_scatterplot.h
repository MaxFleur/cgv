/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_gl/generic_renderer.h>

#include "tf_editor_basic.h"

/*
This class provides a 2D-scatterplot matrix (SPLOM). Added primitives can be modified here. 
If primitive values were modified, they are synchronized with the GUI and line based editor.
*/
class tf_editor_scatterplot : public tf_editor_basic {

public:
	tf_editor_scatterplot();
	std::string get_type_name() const { return "tf_editor_scatterplot_overlay"; }

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

	void create_grid();

	void create_primitive_shapes();

	void add_draggables(int index);

	void draw_content(cgv::render::context& ctx) override;

	bool draw_scatterplot(cgv::render::context& ctx);

	void draw_primitive_shapes(cgv::render::context& ctx);

	void set_point_positions();

	void point_clicked(const vec2& mouse_pos, bool double_clicked, bool ctrl_pressed = false);

	void scroll_primitive_width(int x, int y, bool negative_change, bool ctrl_pressed, bool shift_pressed);

private:

	// renderer and geometry for the plot points
	cgv::render::generic_renderer m_renderer_plot_points;
	tf_editor_shared_data_types::point_geometry m_geometry_plot_points;
	// geometry for lines replacing shapes
	cgv::render::generic_renderer m_renderer_lines;
	std::vector<tf_editor_shared_data_types::line_geometry> m_geometry_lines;

	// Styles for the recangle grid, drawn focus point position shapes and the data plot points
	cgv::g2d::shape2d_style m_style_grid;
	cgv::g2d::shape2d_style m_style_shapes;
	cgv::g2d::shape2d_style m_style_plot_points;

	cgv::g2d::line2d_style m_style_lines;

	cgv::g2d::shape2d_style m_rectangle_style;
	// Rectangles used for drawing
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_draw;
	// Rectangles used for calculations
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_calc;

	// Store ellipses and boxes for the focus point positioning shapes
	std::vector<std::vector<tf_editor_shared_data_types::ellipse>> m_ellipses;
	std::vector<std::vector<tf_editor_shared_data_types::rectangle>> m_boxes;

	std::vector<std::vector<tf_editor_shared_data_types::point_scatterplot>> m_points;
	cgv::g2d::draggable_collection<tf_editor_shared_data_types::point_scatterplot*> m_point_handles;

	// Store the indices of to be updated focus point if a draggable has been interacted with
	int m_interacted_point_id = INT_MAX;

	// plot point radius and blur
	float radius = 2.0f;
	float blur = 0.5f;

	const int label_space = 20;

	rgba m_color_gray{ 0.4f, 0.4f, 0.4f, 1.0f };
};

typedef cgv::data::ref_ptr<tf_editor_scatterplot> tf_editor_scatterplot_ptr;

/** END - MFLEURY **/