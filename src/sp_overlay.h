#pragma once

#ifndef    SP_OVERLAY_H
#define    SP_OVERLAY_H

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>
#include <plot/plot2d.h>

#include "utils_data_types.h"

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
class sp_overlay : public cgv::glutil::overlay {
protected:
	/// whether we need to redraw the contents of this overlay
	bool has_damage = true;

	/// a frame buffer container, storing the offline frame buffer of this overlay content
	cgv::glutil::frame_buffer_container fbc;

	/// canvas to draw content into (same size as overlay)
	cgv::glutil::canvas content_canvas;
	/// canvas to draw overlay into (same size as viewport/main framebuffer)
	cgv::glutil::canvas viewport_canvas;
	/// final style for the overlay when rendering into main framebuffer
	cgv::glutil::shape2d_style overlay_style;

	/// rectangle defining the draw area of the actual plot
	cgv::glutil::rect domain;

	/// stores the data of the 4 protein stains
	std::vector<vec4> data;
	/// stores the names of the 4 protein stains
	std::vector<std::string> names;
	/// keeps track of the amount of points that have been rendred so-far
	int total_count = 0;
	/// threshold that is applied to protein desnity samples before plotting
	float threshold = 0.25f;
	/// alpha value of individual points in plot
	float alpha = 0.1f;
	/// point radius
	float radius = 4.0f;
	/// point blur amount
	float blur = 2.0f;

	/// the protein stain indices for the corresponding scatter plot axis
	int x_idx = 0;
	int y_idx = 1;

	/// renderer for the 2d plot points
	cgv::glutil::generic_renderer point_renderer;
	// renderer for grid lines
	cgv::glutil::generic_renderer m_line_renderer;

	/// define a geometry class holding 2d positions and rgba colors for each point
	DEFINE_GENERIC_RENDER_DATA_CLASS(point_geometry, 2, vec2, position, rgba, color);
	point_geometry points;

	DEFINE_GENERIC_RENDER_DATA_CLASS(line_geometry, 1, vec2, position);
	line_geometry m_line_geometry_grid;

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
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	sp_overlay();
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
	
	void set_data(std::vector<vec4>& data) { this->data = data; }
	void set_names(std::vector<std::string>& names) { this->names = names; }

private:

	void create_grid_lines();

	void add_grid_lines();

private:

	rgba m_color_gray{ 0.4f, 0.4f, 0.4f, 1.0f };

	std::vector<utils_data_types::line> m_lines_grid;

	cgv::glutil::line2d_style m_line_style_grid;
};

typedef cgv::data::ref_ptr<sp_overlay> sp_overlay_ptr;

#endif SP_OVERLAY_H
