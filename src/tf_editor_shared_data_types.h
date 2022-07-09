/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/draggable.h>
#include <cgv_glutil/2d/draggables_collection.h>

typedef cgv::glutil::overlay::vec2 vec2;
typedef cgv::glutil::overlay::vec4 vec4;
typedef cgv::render::render_types::rgba rgba;

namespace tf_editor_shared_data_types
{
	// Simple line geometry
	DEFINE_GENERIC_RENDER_DATA_CLASS(line_geometry, 1, vec2, position);
	// Relation lines between widgets which might be drawn with a varying color
	DEFINE_GENERIC_RENDER_DATA_CLASS(relation_line_geometry, 2, vec2, position, rgba, color);

	// Geometry of strips
	DEFINE_GENERIC_RENDER_DATA_CLASS(polygon_geometry, 2, vec2, position, rgba, color);

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
		// Default color: blue to see the new centroid better
		rgba color{0.0f, 0.0f, 1.0f, 0.5f};

		vec4 centroids{ 0.0f, 0.0f, 0.0f, 0.0f };

		vec4 widths{ 0.5f, 0.5f, 0.5f, 0.5f };
	};

	struct point : public cgv::glutil::draggable
	{
		point(const ivec2& pos)
		{
			this->pos = pos;
			// Huger size so the point is easily clickable
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

			float dist = length(mp - center());
			return dist <= size.x();
		}
	};

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
		// Upmost left value would be 0.0f, while upmost right would be 1.0f
		float get_relative_line_position() {
			return ((cgv::math::length(pos - m_parent_line->a) / m_parent_line->get_length()));
		}
	};

	struct point_scatterplot : point
	{
		int m_stain_first;
		int m_stain_second;

		float x_min;
		float x_max;
		float y_min;
		float y_max;

		point_scatterplot(const ivec2& pos, int stain_first, int stain_second, float x_min, float x_max, float y_min, float y_max) : point(pos)
		{
			m_stain_first = stain_first;
			m_stain_second = stain_second;

			this->x_min = x_min;
			this->x_max = x_max;
			this->y_min = y_min;
			this->y_max = y_max;
		}

		void update_val() {
			pos.x() = cgv::math::clamp(pos.x(), x_min, x_max);
			pos.y() = cgv::math::clamp(pos.y(), y_min, y_max);
		}
	};

	struct polygon {
		std::vector<vec2> points;

		// Find apoint inside a polygon based on the Jordan method
		// https://www.maths.ed.ac.uk/~v1ranick/jordan/cr.pdf
		bool is_point_in_polygon(int x, int y) {
			bool is_inside = false;

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