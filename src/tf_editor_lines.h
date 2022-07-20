/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>

#include "tf_editor_basic.h"

/* 
This class provides the line based editor of the transfer function. Added primitives can be modified here. 
If primitive values were modified, they are synchronized with the GUI and scatterplot.
*/
class tf_editor_lines : public tf_editor_basic {
protected:

	bool other_threshold = false;

	/// renderer for the 2d plot lines
	cgv::glutil::generic_renderer m_line_renderer;
	// strips renderer
	cgv::glutil::generic_renderer m_polygon_renderer;

	tf_editor_shared_data_types::polygon_geometry m_strips;

	tf_editor_shared_data_types::relation_line_geometry m_line_geometry_relations;

	// widget boundaries and strip border lines
	tf_editor_shared_data_types::line_geometry m_line_geometry_widgets;
	tf_editor_shared_data_types::line_geometry m_line_geometry_strip_borders;

	// If a centroid is dragged, the size of the other centroids will decrease
	// so we need two different geometries and styles as well
	tf_editor_shared_data_types::point_geometry_draggable m_point_geometry_interacted;
	tf_editor_shared_data_types::point_geometry_draggable m_point_geometry;

	cgv::glutil::generic_renderer m_point_renderer;

	cgv::glutil::msdf_font m_font;
	cgv::glutil::msdf_gl_font_renderer m_font_renderer;
	cgv::glutil::msdf_text_geometry m_labels;
	const float m_font_size = 18.0f;

	/// initialize styles
	void init_styles(cgv::render::context& ctx);
	/// create the label texts
	void create_labels() override;
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	tf_editor_lines();
	std::string get_type_name() const { return "tf_editor_lines_overlay"; }

	void clear(cgv::render::context& ctx);

	bool handle_event(cgv::gui::event& e);
	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);

	void draw_content(cgv::render::context& ctx) override;
	
	void create_gui();
	
	void resynchronize();

	void primitive_added();

private:

	void init_widgets();

	void add_widget_lines();

	void add_centroid_draggables(bool new_point = true, int centroid_index = 0);

	bool draw_plot(cgv::render::context& ctx);

	void draw_draggables(cgv::render::context& ctx);

	void draw_arrows(cgv::render::context& ctx);

	bool create_centroid_boundaries();

	void create_centroid_strips();

	void create_strip_borders(int index);

	void set_point_positions();

	void update_point_positions();

	void find_clicked_centroid(int x, int y);

	void scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed);

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

	enum VisualizationMode {
		VM_QUADSTRIP = 0,
		VM_GTF = 1
	} vis_mode;

	cgv::glutil::line2d_style m_line_style_relations;
	cgv::glutil::line2d_style m_line_style_widgets;
	cgv::glutil::line2d_style m_line_style_polygons;
	cgv::glutil::line2d_style m_line_style_strip_borders;

	cgv::glutil::shape2d_style m_draggable_style;
	cgv::glutil::shape2d_style m_draggable_style_interacted;

	cgv::glutil::arrow2d_style m_arrow_style;

	std::vector<tf_editor_shared_data_types::line> m_widget_lines;
	std::vector<tf_editor_shared_data_types::polygon> m_widget_polygons;

	// Boundaries of the centroid points
	std::vector<std::vector<vec2>> m_strip_border_points;

	std::vector<std::vector<tf_editor_shared_data_types::point_line>> m_points;
	cgv::glutil::draggables_collection<tf_editor_shared_data_types::point_line*> m_point_handles;

	std::vector<tf_editor_shared_data_types::point_line*> m_interacted_points;

	cgv::glutil::shape2d_style m_plot_line_style;

	rgba m_gray_widgets{ 0.4f, 0.4f, 0.4f, 1.0f };
	rgba m_gray_arrows{ 0.45f, 0.45f, 0.45f, 1.0f };

	// ids used for the texts inside the widgets
	int m_text_ids[4] = { 0, 1, 2, 3 };

	// Store the indices of to be updated centroids if a point has been interacted with
	int m_interacted_centroid_ids[4];
	// Were strips created?
	bool m_strips_created = true;
	// Do we need to update all values?
	bool m_create_all_values = true;
};

typedef cgv::data::ref_ptr<tf_editor_lines> tf_editor_lines_ptr;

/** END - MFLEURY **/