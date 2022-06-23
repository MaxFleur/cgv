#pragma once

#include <chrono>
#include <omp.h>

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

		auto t1 = std::chrono::high_resolution_clock::now();

		#pragma omp parallel for
		for (int i = 0; i < data.size(); i++) {
			// If the value is closer to the boundary without exceeding it, apply
			if (is_left ? data.at(i)[protein_id] < nearest_position && data.at(i)[protein_id] >= boundary_value
				: data.at(i)[protein_id] > nearest_position && data.at(i)[protein_id] <= boundary_value) {
					#pragma omp critical
					{
						nearest_position = data.at(i)[protein_id];
					}
					// abort if the boundary is reached
					if (nearest_position == boundary_value) {
						break;
					}
			}
		}
		/* measure execution time and times the search was performed */
		auto t2 = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> ms_double = t2 - t1;
		std::cout << "Calculation time: " << ms_double.count() << "ms" << std::endl;

		return nearest_position;
	}

	rgba get_complementary_color(const rgba& color) {
		return { 1.0f - color.R(), 1.0f - color.G(), 1.0f - color.B(), 1.0f };
	}

	utils_data_types::line create_nearest_boundary_line(utils_data_types::point point) {
		const auto direction = normalize(point.pos - point.m_parent_line->a);
		const auto ortho_direction = cgv::math::ortho(direction);

		return utils_data_types::line({ point.pos - 4.0f * ortho_direction, point.pos + 2.5f * ortho_direction });
	}
}