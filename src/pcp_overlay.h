#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>
#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/draggable.h>
#include <cgv_glutil/2d/draggables_collection.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

/*
This class provides an example to render a parallel coordinates plot (PCP) for 4 dimensional data.
Parallel coordinates are represented by pair-wise connections of the dimension axes using lines.
Due to the large amount of input data, lines are drawn in batches to prevent program stalls during rendering.
Press the "Update" button in the GUi to force the plot to redraw its contents.

Lines are drawn transparent, using the standard over blending operator, to achieve an accumulative effect
in the final image, emphasizing regions of many overlapping lines.
To filter the data before drawing, lines whose average sample values are lower than a given threshold are removed.
*/
class pcp_overlay : public cgv::glutil::overlay {
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
	/// keeps track of the amount of lines that have been rendred so-far
	int total_count = 0;
	/// threshold that is applied to protein desnity samples before plotting
	float threshold = 0.3f;
	/// alpha value of individual lines in plot
	float line_alpha = 0.0001f;

	/// renderer for the 2d plot lines
	cgv::glutil::generic_renderer m_line_renderer;
	/// define a geometry class holding only 2d position values
	DEFINE_GENERIC_RENDER_DATA_CLASS(line_geometry, 1, vec2, position);
	// The lines for the relations between the widgets
	line_geometry m_line_geometry_relations;
	// widget boundaries
	line_geometry m_line_geometry_widgets;

	DEFINE_GENERIC_RENDER_DATA_CLASS(point_geometry, 1, vec2, position);
	point_geometry m_draggable_points;

	cgv::glutil::generic_renderer m_point_renderer;
	cgv::glutil::shape2d_style m_draggable_style;

	cgv::glutil::msdf_font m_font;
	cgv::glutil::msdf_gl_font_renderer m_font_renderer;
	cgv::glutil::msdf_text_geometry m_labels;
	const float m_font_size = 18.0f;

	/// initialize styles
	void init_styles(cgv::render::context& ctx);
	/// update the overlay content (called by a button in the gui)
	void update_content();

public:
	pcp_overlay();
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
	
	void set_data(std::vector<vec4>& data) { this->data = data; }
	void set_names(std::vector<std::string>& names) { m_protein_names = names; }

private:

	void init_widgets();

	void add_widgets();

	void add_centroids();

	void add_centroid_draggables();

	void draw_draggables(cgv::render::context& ctx);

	void set_draggable_styles();

	void set_point_positions();

private:

	cgv::glutil::line2d_style m_line_style_relations;
	cgv::glutil::line2d_style m_line_style_widgets;

	struct line
	{
		vec2 a;
		vec2 b;

		vec2 interpolate(float value) const {
			return cgv::math::lerp(a, b, value);
		}

		vec2 getIntersection(vec2 point) const {
			const auto direction = b - a;
			const auto cd = a - point; 
			auto boundary = -dot(cd, direction) / dot(direction, direction);

			// Keep point on boundaries
			boundary = cgv::math::clamp(boundary, 0.1f, 0.9f);
			
			return a + boundary * direction;
		}
	};

	struct centroid
	{
		rgba color{0.0f, 0.0f, 0.0f, 0.0f};

		float centr_myosin = 0.0f;
		float centr_actin = 0.0f;
		float centr_obscurin = 0.0f;
		float centr_sallimus = 0.0f;

		float gaussian_width = 0.0f;
	};

	struct point : public cgv::glutil::draggable
	{
		line* m_parent_line;

		point(const ivec2& pos, line* line)
		{
			m_parent_line = line;
			this->pos = pos;
			size = vec2(8.0f);
			position_is_center = true;
			constraint_reference = CR_FULL_SIZE;
		}

		void update_val() {
			pos = m_parent_line->getIntersection(pos);
		}

		bool is_inside(const vec2& mp) const
		{
			float dist = length(mp - center());
			return dist <= size.x();
		}

		ivec2 get_render_position() const
		{
			return ivec2(pos + 0.5f);
		}

		ivec2 get_render_size() const
		{
			return 2 * ivec2(size);
		}
	};

	std::vector<line> m_widget_lines;

	std::vector<std::string> m_protein_names;

	std::vector<centroid> m_centroids;

	std::vector<std::vector<point>> m_points;
	cgv::glutil::draggables_collection<point*> m_point_handles;

	// ids used for the texts inside the widgets
	int m_id_left = 0;
	int m_id_right = 1;
	int m_id_bottom = 2;
	int m_id_center = 3;
};

typedef cgv::data::ref_ptr<pcp_overlay> pcp_overlay_ptr;
