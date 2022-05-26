#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

/*
This class provides an example to render a parallel coordinates plot (PCP) for 4 dimensional data.
Parallel coordinates are represented by pair-wise connections of the dimension axes using lines.
Due to the large amount of input data, lines are drawn in batches to prevent program stalls during rendering.
Press the "Update" button in the GUi to force the plot to redraw its contents.

Lines are drawn using additive blending with an alpha of 1. This effectively produces per-pixel counts stating
the number of lines that intersect a certain pixel. This allows for better control over the aggregated output image.
A custom mapping shader is used to read the pixel counts and map them to a color via clamping and normalizing to [0,1]
using the "normalize_count" and then performing a kind of contrast shift with the given "exponent". Thsi serves to
preserve information of features with lower line counts.
To filter the data before drawing, lines whose average sample values are lower than a given threshold are removed.
*/
class pcp2_overlay : public cgv::glutil::overlay {
protected:
	/// whether we need to redraw the contents of this overlay
	bool has_damage = true;

	/// a frame buffer container, storing the offline frame buffer of this overlay content
	cgv::glutil::frame_buffer_container fbc;
	/// a second frame buffer container, storing the offline frame buffer of only the plot domain
	cgv::glutil::frame_buffer_container pcp_fbc;

	/// canvas for the plot domain
	cgv::glutil::canvas pcp_canvas;
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
	/// keeps track of the amount of lines that have been rendred so-far
	int total_count = 0;
	/// threshold that is applied to protein desnity samples before plotting
	float threshold = 0.3f;
	/// alpha value of individual lines in plot
	float line_alpha = 1.0f;
	/// used in mapping shader to normalize the accumulated alpha values
	float normalize_count = 50000.0f;
	/// applied to the alpha values after normalization and used to shift the contrast
	float exponent = 0.3f;

	/// renderer for the 2d plot lines
	cgv::glutil::generic_renderer line_renderer;
	/// define a geometry class holding only 2d position values
	DEFINE_GENERIC_RENDER_DATA_CLASS(line_geometry, 1, vec2, position);
	line_geometry lines;

	/// initialize styles
	void init_styles(cgv::render::context& ctx);
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	pcp2_overlay();
	std::string get_type_name() const { return "pcp2_overlay"; }

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
};

typedef cgv::data::ref_ptr<pcp2_overlay> pcp2_overlay_ptr;
