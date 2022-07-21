#pragma once

#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>

#include "tf_editor_basic.h"

/*
This class provides a 2D-scatterplot matrix (SPLOM). Added primitives can be modified here. 
If primitive values were modified, they are synchronized with the GUI and line based editor.
*/
class tf_editor_scatterplot : public tf_editor_basic {
protected:

	/// point radius
	float radius = 2.0f;
	/// point blur amount
	float blur = 0.5f;

	const int label_space = 20;

	/// renderer for the 2d plot points
	cgv::glutil::generic_renderer m_point_renderer;
	// renderer for the draggables
	cgv::glutil::generic_renderer m_draggables_renderer;

	/// define a geometry class holding 2d positions and rgba colors for each point
	tf_editor_shared_data_types::point_geometry_data m_point_geometry_data;

	// If a centroid is dragged, the size of the other centroids will decrease
	// so we need two different geometries and styles as well
	tf_editor_shared_data_types::point_geometry_draggable m_point_geometry_interacted;
	tf_editor_shared_data_types::point_geometry_draggable m_point_geometry;

	tf_editor_shared_data_types::line_geometry m_line_geometry_grid;

	/// stores the actually used font (atlas)
	cgv::glutil::msdf_font font;
	/// a font renderer that supplies the shader program and renders given text geometry
	cgv::glutil::msdf_gl_font_renderer font_renderer;
	/// text geometry storing the individual labels
	cgv::glutil::msdf_text_geometry labels;
	/// size to use when rendering the labels
	float font_size = 18.0f;

	/// initialize styles
	void init_styles(cgv::render::context& ctx);
	/// create the label texts
	void create_labels() override;
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	tf_editor_scatterplot();
	std::string get_type_name() const { return "tf_editor_scatterplot_overlay"; }

	void clear(cgv::render::context& ctx);

	bool self_reflect(cgv::reflect::reflection_handler& _rh);

	bool handle_event(cgv::gui::event& e);
	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);

	void draw_content(cgv::render::context& ctx) override;
	
	void create_gui();

	void resynchronize();

	void primitive_added();

private:

	void create_grid();

	void create_primitive_shapes();

	void add_centroid_draggables(bool new_point = true, int centroid_index = 0);

	bool draw_scatterplot(cgv::render::context& ctx);

	void draw_draggables(cgv::render::context& ctx);

	void draw_primitive_shapes(cgv::render::context& ctx);

	void set_point_positions();

	void find_clicked_centroid(int x, int y);

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

	rgba m_color_gray{ 0.4f, 0.4f, 0.4f, 1.0f };

	cgv::glutil::shape2d_style m_rectangle_style;
	// Rectangles used for drawing
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_draw;
	// Rectangles used for calculations
	std::vector<tf_editor_shared_data_types::rectangle> m_rectangles_calc;

	// Style of the draggables, interacted ones are drawn differently
	cgv::glutil::shape2d_style m_draggable_style;
	cgv::glutil::shape2d_style m_draggable_style_interacted;

	cgv::glutil::shape2d_style m_ellipse_style;
	std::vector<std::vector<tf_editor_shared_data_types::ellipse>> m_ellipses;

	cgv::glutil::shape2d_style m_rect_box_style;
	std::vector<std::vector<tf_editor_shared_data_types::rectangle>> m_boxes;

	cgv::glutil::shape2d_style m_point_style;
	std::vector<std::vector<tf_editor_shared_data_types::point_scatterplot>> m_points;
	cgv::glutil::draggables_collection<tf_editor_shared_data_types::point_scatterplot*> m_point_handles;

	std::vector<tf_editor_shared_data_types::point_scatterplot*> m_interacted_points;

	cgv::glutil::shape2d_style m_rect_grid_style;

	// Store the indices of to be updated centroids if a point has been interacted with
	std::pair<int, int> m_interacted_point_id;
};

typedef cgv::data::ref_ptr<tf_editor_scatterplot> tf_editor_scatterplot_ptr;

