/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

#include "sliced_volume_data_set.h"
#include "shared_editor_data.h"
#include "tf_editor_shared_data_types.h"

/* This class provides the editor of the transfer function. The values are synchronized with the GUI */
class tf_editor_lines : public cgv::glutil::overlay {
protected:
	/// whether we need to redraw the contents of this overlay
	bool has_damage = true;

	/// a frame buffer container, storing the offline frame buffer of this overlay content
	cgv::glutil::frame_buffer_container fbc;

	/// a frame buffer container, storing the offline frame buffer of the plot lines
	cgv::glutil::frame_buffer_container fbc_plot;

	/// canvas to draw content into (same size as overlay)
	cgv::glutil::canvas content_canvas;
	/// canvas to draw overlay into (same size as viewport/main framebuffer)
	cgv::glutil::canvas viewport_canvas;
	/// final style for the overlay when rendering into main framebuffer
	cgv::glutil::shape2d_style overlay_style;

	/// rectangle defining the draw area of the actual plot
	cgv::glutil::rect domain;

	/// whether the plot shall be reset and its framebuffer cleared
	bool reset_plot = true;
	/// keeps track of the amount of lines that have been rendred so-far
	int total_count = 0;
	/// threshold that is applied to protein desnity samples before plotting
	float threshold = 0.3f;
	/// alpha value of individual lines in plot
	float line_alpha = 0.0001f;

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
	void create_labels();
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	tf_editor_lines();
	std::string get_type_name() const { return "pcp_overlay"; }

	void clear(cgv::render::context& ctx);

	bool self_reflect(cgv::reflect::reflection_handler& _rh);
	void stream_help(std::ostream& os) {}

	bool handle_event(cgv::gui::event& e);
	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);
	void draw(cgv::render::context& ctx);
	void draw_content(cgv::render::context& ctx);
	
	void create_gui();
	
	void resynchronize();

	void set_data_set(sliced_volume_data_set* data_set_ptr) {
		m_data_set_ptr = data_set_ptr;
		create_labels();
	}

	void set_shared_data(shared_data_ptr data_ptr) { m_shared_data_ptr = data_ptr; }

private:

	void init_widgets();

	void add_widget_lines();

	void add_centroids();

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

	// redraw the plot contents excluding the data vis, optionally recreate the gui
	void redraw(bool recreate_gui) {
		has_damage = true;
		if (recreate_gui) {
			post_recreate_gui();
		}
		post_redraw();
	}

private:

	enum VisualizationMode {
		VM_QUADSTRIP = 0,
		VM_GTF = 1
	} vis_mode;

	/// store a pointer to the data set
	sliced_volume_data_set* m_data_set_ptr = nullptr;

	shared_data_ptr m_shared_data_ptr;

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
	bool use_tone_mapping = false;
	unsigned tm_normalization_count = 1000;
	float tm_alpha = 1.0f;
	float tm_gamma = 1.0f;

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
	// Has a point been dragged?
	bool m_interacted_id_set = false;

	// Has a point been clicked?
	bool m_is_point_clicked = false;
	// Id of the centroid layer whose point was clicked
	int m_clicked_centroid_id;
};

typedef cgv::data::ref_ptr<tf_editor_lines> tf_editor_lines_ptr;

/** END - MFLEURY **/