#pragma once

#include <cgv_glutil/overlay.h>

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
}