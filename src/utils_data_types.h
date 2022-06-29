#pragma once

#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/draggable.h>
#include <cgv_glutil/2d/draggables_collection.h>

typedef cgv::glutil::overlay::vec2 vec2;
typedef cgv::glutil::overlay::vec4 vec4;
typedef cgv::render::render_types::rgba rgba;

namespace utils_data_types
{

struct line
	{
		vec2 a;
		vec2 b;

		vec2 interpolate(float value, bool clamp_enabled = true) const {
			if (clamp_enabled) {
				value = cgv::math::clamp(value, 0.1f, 0.9f);
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
		rgba color{0.0f, 0.0f, 0.0f, 0.5f};

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
}