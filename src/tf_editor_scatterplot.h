/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/generic_renderer.h>

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

	void update_content();

	void create_labels() override;

	void create_grid();

	void create_primitive_shapes();

	void add_centroid_draggables(bool new_point = true, int centroid_index = 0);

	void draw_content(cgv::render::context& ctx) override;

	bool draw_scatterplot(cgv::render::context& ctx);

	void draw_draggables(cgv::render::context& ctx);

	void draw_primitive_shapes(cgv::render::context& ctx);

	void set_point_positions();

	void find_clicked_draggable(int x, int y);

	void scroll_centroid_width(int x, int y, bool negative_change, bool ctrl_pressed);

	void end_drag() {
		m_interacted_points.clear();

		if (vis_mode == VM_GTF) {
			update_content();
			return;
		}
		has_damage = true;
		post_redraw();
	}

private:

	// renderer and geometry for the plot points
	cgv::glutil::generic_renderer m_renderer_plot_points;
	tf_editor_shared_data_types::point_geometry_data m_geometry_plot_points;

	// Stles for the recangle grid, drawn centroid position shapes and the data plot points
	cgv::glutil::shape2d_style m_style_grid;
	cgv::glutil::shape2d_style m_style_shapes;
	cgv::glutil::shape2d_style m_style_plot_points;

	cgv::glutil::shape2d_style m_rectangle_style;
	// Rectangles used for drawing
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_draw;
	// Rectangles used for calculations
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_calc;

	// Store ellipses and boxes for the centroid positioning shapes
	std::vector<std::vector<tf_editor_shared_data_types::ellipse>> m_ellipses;
	std::vector<std::vector<tf_editor_shared_data_types::rectangle>> m_boxes;

	std::vector<std::vector<tf_editor_shared_data_types::point_scatterplot>> m_points;
	cgv::glutil::draggables_collection<tf_editor_shared_data_types::point_scatterplot*> m_point_handles;

	std::vector<tf_editor_shared_data_types::point_scatterplot*> m_interacted_points;

	// Store the indices of to be updated centroid poition if a draggable has been interacted with
	std::pair<int, int> m_interacted_point_id;

	// plot point radius and blir
	float radius = 2.0f;
	float blur = 0.5f;

	const int label_space = 20;

	rgba m_color_gray{ 0.4f, 0.4f, 0.4f, 1.0f };
};

typedef cgv::data::ref_ptr<tf_editor_scatterplot> tf_editor_scatterplot_ptr;

/** END - MFLEURY **/