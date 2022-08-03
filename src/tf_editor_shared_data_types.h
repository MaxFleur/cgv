/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/generic_renderer.h>
#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/draggable.h>
#include <cgv_glutil/2d/draggables_collection.h>

typedef cgv::glutil::overlay::vec2 vec2;
typedef cgv::glutil::overlay::vec4 vec4;
typedef cgv::render::render_types::rgba rgba;

/* Contains different data types used by the editors */
namespace tf_editor_shared_data_types
{
	// Geometry used to draw various lines
	DEFINE_GENERIC_RENDER_DATA_CLASS(line_geometry, 2, vec2, position, rgba, color);

	// Geometry used for quadstrips
	DEFINE_GENERIC_RENDER_DATA_CLASS(polygon_geometry, 2, vec2, position, rgba, color);
	DEFINE_GENERIC_RENDER_DATA_CLASS(polygon_geometry2, 3, vec2, position, rgba, color, vec2, texcoord);

	// Geometry for draggable points
	DEFINE_GENERIC_RENDER_DATA_CLASS(point_geometry_draggable, 1, vec2, position);
	// Geometry for non draggable points containing data
	DEFINE_GENERIC_RENDER_DATA_CLASS(point_geometry_data, 2, vec2, position, rgba, color);

	struct line
	{
		vec2 a;
		vec2 b;

		vec2 interpolate(float value, bool clamp_enabled = true) const {
			// Clamp so the value stays inside the boundaries
			if (clamp_enabled) {
				value = cgv::math::clamp(value, 0.0f, 1.0f);
				value = cgv::math::lerp(0.1f, 0.9f, value);
			}
			return cgv::math::lerp(a, b, value);
		}

		// Used if a draggable point is moved on the line to keep it on this line
		vec2 get_intersection(vec2 point) const {
			const auto direction = b - a;
			const auto cd = a - point; 
			auto length = -dot(cd, direction) / dot(direction, direction);

			// Keep point on boundaries
			length = cgv::math::clamp(length, 0.1f, 0.9f);
			
			return a + length * direction;
		}

		float get_length() {
			return cgv::math::length(b - a);
		}
	};

	struct rectangle
	{
		vec2 start, end;

		rectangle(vec2 s, vec2 e) {
			start = s;
			end = e;
		}
		// Check if a point is inside the rectangle
		bool is_inside(int x, int y) {
			return (x > start.x() && x < end.x()) && (y > start.y() && y < end.y()) ? true : false;
		}
		// Calculate the relative x or y position of a point inside the rectangle
		float relative_position(float value, bool is_value_x) const {
			auto rel = is_value_x ? (value - start.x()) / (end.x() - start.x()) : (value - start.y()) / (end.y() - start.y());
			rel = cgv::math::clamp(rel, 0.0f, 1.0f);
			return rel;
		}

		float size_x() {
			return end.x() - start.x();
		}

		float size_y() {
			return end.y() - start.y();
		}
		// Get a point inside the rectangle based on an input value
		// A vec2 of (0, 0) would return the loest left coordinate, while (1, 1) would return the uppermost right
		vec2 point_in_rect(vec2 value) {
			return vec2(start.x() + size_x() * value.x(), start.y() + size_y() * value.y());
		}
	};

	struct ellipse
	{
		vec2 pos;
		vec2 size;

		ellipse(vec2 p, vec2 s) {
			pos = p;
			size = s;
		}
	};

	struct point : public cgv::glutil::draggable
	{
		point(const ivec2& pos)
		{
			this->pos = pos;
			size = vec2(6.0f);
			position_is_center = true;
			constraint_reference = CR_FULL_SIZE;
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

			const auto dist = length(mp - center());
			return dist <= size.x();
		}
	};

	// Point with a parent line. The point can be dragged along this line.
	struct point_line : point
	{
		line* m_parent_line;

		point_line(const ivec2& pos, line* line) : point(pos)
		{
			m_parent_line = line;
		}

		void update_val() {
			pos = m_parent_line->get_intersection(pos);
		}

		// Move the point according to a relative position along the line
		void move_along_line(float value) {
			pos = m_parent_line->interpolate(value);
		}

		// Gets the relative position of the point along the line
		// Leftmost value would be 0.0f, while rightmost would be 1.0f
		float get_relative_line_position() {
			return ((cgv::math::length(pos - m_parent_line->a) / m_parent_line->get_length()));
		}
	};

	// Point with a parent rectangle. The point stays inside this rectangle at all times.
	struct point_scatterplot : point
	{
		int m_stain_first;
		int m_stain_second;

		rectangle* parent_rectangle;

		point_scatterplot(const ivec2& pos, int stain_first, int stain_second, rectangle* rectangle) : point(pos)
		{
			m_stain_first = stain_first;
			m_stain_second = stain_second;

			parent_rectangle = rectangle;
		}

		// Make sure the point stays in its rectangle
		void update_val() {
			pos.x() = cgv::math::clamp(pos.x(), parent_rectangle->start.x(), parent_rectangle->end.x());
			pos.y() = cgv::math::clamp(pos.y(), parent_rectangle->start.y(), parent_rectangle->end.y());
		}

		float get_relative_position(float val, bool is_relative_x) {
			return parent_rectangle->relative_position(val, is_relative_x);
		}
	};

	struct polygon {
		std::vector<vec2> points;

		// Find apoint inside a polygon based on the Jordan method
		// https://www.maths.ed.ac.uk/~v1ranick/jordan/cr.pdf
		bool is_point_in_polygon(int x, int y) {
			auto is_inside = false;

			for (int i = 0, j = points.size() - 1; i < points.size(); j = i++) {

				const auto i_x = points.at(i).x();
				const auto i_y = points.at(i).y();
				const auto j_x = points.at(j).x();
				const auto j_y = points.at(j).y();

				if (((i_y > y) != (j_y > y)) && (x < (j_x - i_x) * (y - i_y) / (j_y - i_y) + i_x)) {
					is_inside = !is_inside;
				}
			}
			return is_inside;
		}

		vec2 get_center() const {
			vec2 c(0.0f);
			for(const vec2& p : points)
				c += p;
			return c /= static_cast<float>(points.size());
		}

		// default constructor
		polygon() {}

		// construct from given vector of points
		polygon(std::vector<vec2>& pnts) {
			points = pnts;
		}
	};
}

/** END - MFLEURY **/