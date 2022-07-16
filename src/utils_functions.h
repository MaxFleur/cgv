/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/overlay.h>

#include "tf_editor_shared_data_types.h"

namespace utils_functions
{
	// set the centroid id array
	void set_interacted_centroid_ids(int *interacted_centroid_ids, int id, int input_index) {
		// Because we have four centroids, but 12 points, multiply with 3
		const auto index = input_index * 3;
		interacted_centroid_ids[0] = id;
		interacted_centroid_ids[1] = index;
		// Apply to the following two centroids
		interacted_centroid_ids[2] = index + 1;
		interacted_centroid_ids[3] = index + 2;
	}

	float gaussian_transfer_function(const vec4& data_values, const vec4& gaussian_centroid, const vec4& gaussian_width) {

		// construct a matrix with diagonal components set to the given value
		cgv::glutil::overlay::mat4 mat;

		mat.set_row(0, vec4(2.0 / gaussian_width[0], 0.0, 0.0, 0.0));
		mat.set_row(1, vec4(0.0, 2.0 / gaussian_width[1], 0.0, 0.0));
		mat.set_row(2, vec4(0.0, 0.0, 2.0 / gaussian_width[2], 0.0));
		mat.set_row(3, vec4(0.0, 0.0, 0.0, 2.0 / gaussian_width[3]));

		vec4 diff = data_values - gaussian_centroid;
		cgv::glutil::overlay::mat4 mat_squared = mat * mat;

		// calculate exponent; use dot product to multiply two vectors with the intention of receiving a single floating point result
		float exponent = dot(diff, mat_squared * diff);

		return exp(-exponent);
	}

	float box_transfer_function(const vec4& data_values, const vec4& centroid_positions, const vec4& width) {
		float ret = 1.0f;
		for (int i = 0; i < 4; i++) {
			ret *= data_values[i] >= centroid_positions[i] - (width[i] / 2) && data_values[i] <= centroid_positions[i] + (width[i] / 2) ? 1.0f : 0.0f;
		}

		return ret;
	}
}

/** END - MFLEURY **/