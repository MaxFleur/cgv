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

/* This class provides the editor of the transfer function. The values are synchronized with the GUI */
class tf_editor_widget : public cgv::glutil::overlay {
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
	line_geometry m_line_geometry_centroid_lines;

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
	tf_editor_widget();
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

	void add_widget_lines();

	void add_centroids();

	void add_centroid_draggables();

	bool draw_plot(cgv::render::context& ctx);

	void draw_draggables(cgv::render::context& ctx);

	void draw_centroid_lines(cgv::render::context& ctx, cgv::render::shader_program& prog);

	void draw_arrows(cgv::render::context& ctx);

	void create_centroid_lines();

	void set_point_positions();

	float search_nearest_boundary_value(float relative_position, 
										float boundary_value,
										int protein_id,
										bool is_left);

private:

	struct line
	{
		vec2 a;
		vec2 b;

		vec2 interpolate(float value) const {
			return cgv::math::lerp(a, b, value);
		}

		vec2 get_intersection(vec2 point) const {
			const auto direction = b - a;
			const auto cd = a - point; 
			auto boundary = -dot(cd, direction) / dot(direction, direction);

			// Keep point on boundaries
			boundary = cgv::math::clamp(boundary, 0.1f, 0.9f);
			
			return a + boundary * direction;
		}

		float get_length() {
			return cgv::math::length(b - a);
		}
	};

	struct centroid
	{
		rgba color{0.0f, 0.0f, 0.0f, 0.0f};

		vec4 centroids{ 0.0f, 0.0f, 0.0f, 0.0f };

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
			pos = m_parent_line->get_intersection(pos);
		}

		// Move the point according to a relative position along the line
		void move_along_line(float value) {
			pos = m_parent_line->interpolate(value);
		}

		// Gets the relative position of the point along the line
		// Upmost left value would be 0.0f, while upmost right would be 1.0f
		float get_relative_line_position() {
			return ( ( cgv::math::length(pos - m_parent_line->a) / m_parent_line->get_length()));
		}

		ivec2 get_render_position() const
		{
			return ivec2(pos + 0.5f);
		}

		ivec2 get_render_size() const
		{
			return 2 * ivec2(size);
		}

		// we need to override this method to test if the position is inside the circle (and not a rectangle as is supplied by draggable)
		bool is_inside(const vec2& mp) const {

			float dist = length(mp - center());
			return dist <= size.x();
		}
	};

private:

	cgv::glutil::line2d_style m_line_style_relations;
	cgv::glutil::line2d_style m_line_style_widgets;
	cgv::glutil::line2d_style m_line_style_centroid_lines;

	cgv::glutil::arrow2d_style m_arrow_style;

	std::vector<line> m_widget_lines;
	std::vector<std::vector<line>> m_centroid_lines;

	std::vector<std::string> m_protein_names;

	std::vector<centroid> m_centroids;

	std::vector<std::vector<point>> m_points;
	cgv::glutil::draggables_collection<point*> m_point_handles;

	rgba color_red{ 1.0f, 0.0f, 0.0f, 1.0f };

	// ids used for the texts inside the widgets
	int m_id_left = 0;
	int m_id_right = 1;
	int m_id_bottom = 2;
	int m_id_center = 3;

	bool m_are_centroid_lines_created = true;
};

typedef cgv::data::ref_ptr<tf_editor_widget> tf_editor_widget_ptr;
