#pragma once

#include <cgv_glutil/overlay.h>

#include "utils_data_types.h"

namespace utils_functions
{
	// get the nearest data position to a certain boundary value of a centroid point
	float search_nearest_boundary_value(float relative_position,
										float boundary_value,
										int protein_id,
										bool is_left,
										const std::vector<cgv::glutil::overlay::vec4>& data) {
		// start with the centroid's relative position
		auto nearest_position = relative_position;
		// do we want to know the left or right nearest position?
		for (const auto data_val : data) {
			// If the value is closer to the boundary without exceeding it, apply
			if (is_left ? data_val[protein_id] < nearest_position && data_val[protein_id] >= boundary_value
				: data_val[protein_id] > nearest_position && data_val[protein_id] <= boundary_value) {
				nearest_position = data_val[protein_id];
				// abort if the boundary is reached
				if (nearest_position == boundary_value) {
					break;
				}
			}
		}
		return nearest_position;
	}

	rgba get_complementary_color(const rgba& color) {
		return {1.0f - color.R(), 1.0f - color.G(), 1.0f - color.B(), 1.0f};
	}

	utils_data_types::line create_nearest_boundary_line(utils_data_types::point point) {
		const auto direction = normalize(point.pos - point.m_parent_line->a);
		const auto ortho_direction = cgv::math::ortho(direction);

		return utils_data_types::line({ point.pos - 4.0f * ortho_direction, point.pos + 2.5f * ortho_direction });
	}
}