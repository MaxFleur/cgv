#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>
#include <plot/plot2d.h>

#include "sliced_volume_data_set.h"
#include "shared_editor_data.h"
#include "tf_editor_shared_data_types.h"

/*
This class provides an example to render a scatter plot (SP), depicting 2 dimensions of the given 4 dimensional data.
Points are drawn on a 2 dimensional domain, where each orthogonal axis represents a different data dimension.
Due to the large amount of input data, points are drawn in batches to prevent program stalls during rendering.
Press the "Update" button in the GUi to force the plot to redraw its contents.

A x and y index determine the index of the data dimension to be displayed.

Points are drawn transparent, using the standard over blending operator, to achieve an accumulative effect
in the final image, emphasizing regions of many overlapping lines.
Point size and blur can be adjusted in the GUI. The effect will be visible after pressing "Update".
To filter the data before drawing, points whose average sample values are lower than a given threshold are removed.
Even though only 2 dimensions are displayed, the filtering considers the all dimensions.

This example further demonstrates how to render 2d text. The text is used to display labels on the coordinate axes.
*/
class tf_editor_scatterplot : public cgv::glutil::overlay {
protected:
	/// whether we need to redraw the contents of this overlay
	bool has_damage = true;

	/// a frame buffer container, storing the offline frame buffer of this overlay content
	cgv::glutil::frame_buffer_container fbc;

	/// a frame buffer container, storing the offline frame buffer of the plot points
	cgv::glutil::frame_buffer_container fbc_plot;

	/// canvas to draw content into (same size as overlay)
	cgv::glutil::canvas content_canvas;
	/// canvas to draw overlay into (same size as viewport/main framebuffer)
	cgv::glutil::canvas viewport_canvas;
	/// final style for the overlay when rendering into main framebuffer
	cgv::glutil::shape2d_style overlay_style;

	/// rectangle defining the draw area of the actual plot
	cgv::glutil::rect domain;

	/// keeps track of the amount of points that have been rendred so-far
	int total_count = 0;
	/// threshold that is applied to protein desnity samples before plotting
	float threshold = 0.25f;
	/// alpha value of individual points in plot
	float alpha = 0.1f;
	/// point radius
	float radius = 2.0f;
	/// point blur amount
	float blur = 0.5f;

	const int label_space = 20;

	/// the protein stain indices for the corresponding scatter plot axis
	int x_idx = 0;
	int y_idx = 1;

	/// renderer for the 2d plot points
	cgv::glutil::generic_renderer m_point_renderer;
	// renderer for grid lines
	cgv::glutil::generic_renderer m_line_renderer;
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
	void create_labels();
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	tf_editor_scatterplot();
	std::string get_type_name() const { return "sp_overlay"; }

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

	void primitive_added();
	
	void set_data_set(sliced_volume_data_set* data_set_ptr) {
		m_data_set_ptr = data_set_ptr;
		create_labels();
	}

	void set_shared_data(shared_data_ptr data_ptr) { m_shared_data_ptr = data_ptr; }

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

	// redraw the plot contents excluding the data vis, optionally recreate the gui
	void redraw() {
		has_damage = true;
		post_redraw();
	}

private:

	enum VisualizationMode {
		VM_SHAPES = 0,
		VM_GTF = 1
	} vis_mode;

	/// store a pointer to the data set
	sliced_volume_data_set* m_data_set_ptr = nullptr;

	shared_data_ptr m_shared_data_ptr;

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

	bool use_tone_mapping = false;
	unsigned tm_normalization_count = 1000;
	float tm_alpha = 1.0f;
	float tm_gamma = 1.0f;

	bool reset_plot = true;

	// Has a point been clicked?
	bool m_is_point_clicked = false;

	// Store the indices of to be updated centroids if a point has been interacted with
	std::pair<int, int> m_interacted_point_id;
	// Has a point been dragged?
	bool m_interacted_id_set = false;

	// Id of the centroid layer whose point was clicked
	int m_clicked_centroid_id;
};

typedef cgv::data::ref_ptr<tf_editor_scatterplot> tf_editor_scatterplot_ptr;

