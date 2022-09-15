/** BEGIN - MFLEURY **/

#pragma once

#include "shared_editor_data.h"

/* Contains functions used by both editors */
namespace tf_editor_shared_functions
{
	// set the centroid id array
	static void set_interacted_centroid_ids(int* interacted_centroid_ids, int input_index) {
		// Because we have four centroids, but 12 points, multiply with 3
		const auto index = input_index * 3;
		interacted_centroid_ids[1] = index;
		// Apply to the following two centroids
		interacted_centroid_ids[2] = index + 1;
		interacted_centroid_ids[3] = index + 2;
	}

	static float gaussian_transfer_function(const vec4& data_values, const vec4& gaussian_centroid, const vec4& gaussian_width) {

		// construct a matrix with diagonal components set to the given value
		cgv::glutil::overlay::mat4 mat;

		mat.set_row(0, vec4(2.0 / gaussian_width[0], 0.0, 0.0, 0.0));
		mat.set_row(1, vec4(0.0, 2.0 / gaussian_width[1], 0.0, 0.0));
		mat.set_row(2, vec4(0.0, 0.0, 2.0 / gaussian_width[2], 0.0));
		mat.set_row(3, vec4(0.0, 0.0, 0.0, 2.0 / gaussian_width[3]));

		const auto diff = data_values - gaussian_centroid;
		const auto mat_squared = mat * mat;

		// calculate exponent; use dot product to multiply two vectors with the intention of receiving a single floating point result
		const auto exponent = dot(diff, mat_squared * diff);

		return exp(-exponent);
	}

	// Apply an alpha value of 1 or 0 based on if the input values are inside the hyperbox
	static float box_transfer_function(const vec4& data_values, const vec4& centroid_positions, const vec4& width) {
		for (int i = 0; i < 4; i++) {
			if (data_values[i] < centroid_positions[i] - (width[i] / 2) || data_values[i] > centroid_positions[i] + (width[i] / 2)) {
				return 0.0f;
			}
		}
		return 1.0f;
	}

	// Apply an alpha value of 1 or 0 based on if the input values are inside the hyperellipsoid
	static float sphere_transfer_function(const vec4& data_values, const vec4& centroid_positions, const vec4& width) {
		const auto delta = data_values - centroid_positions;

		const auto max_width = cgv::math::max_value(width);
		const auto lambda = 2.0f * vec4(max_width) / width;

		auto scaled_diff = delta * lambda;
		scaled_diff *= scaled_diff;

		const auto dist = sqrt(scaled_diff[0] + scaled_diff[1] + scaled_diff[2] + scaled_diff[3]) - max_width;

		if(dist > 0.0f)
			return 0.0f;

		return 1.0f;
	}

	// Calculate a color for a relations type, based on the current primitive type
	static rgb get_color(const vec4& v, const std::vector<shared_data::primitive>& primitives) {
		rgb color(0.0f);

		for (int i = 0; i < primitives.size(); i++) {
			// Get the primitive
			const auto& primitive = primitives.at(i);
			auto alpha = 0.0f;

			switch (primitive.type) {
			// Calculate the alpha value based on the primitive tyoe
			case shared_data::TYPE_GAUSS:
				alpha = tf_editor_shared_functions::gaussian_transfer_function(v, primitive.centroid, primitive.widths);
				break;
			case shared_data::TYPE_BOX:
				alpha = tf_editor_shared_functions::box_transfer_function(v, primitive.centroid, primitive.widths);
				break;
			case shared_data::TYPE_SPHERE:
				alpha = tf_editor_shared_functions::sphere_transfer_function(v, primitive.centroid, primitive.widths);
				break;
			}
			alpha *= primitive.color.alpha();
			// Color is added for each primitive
			color += alpha * rgb{ primitive.color.R(), primitive.color.G(), primitive.color.B() };
		}
		// Clamp between range
		color.R() = cgv::math::clamp(color.R(), 0.0f, 1.0f);
		color.G() = cgv::math::clamp(color.G(), 0.0f, 1.0f);
		color.B() = cgv::math::clamp(color.B(), 0.0f, 1.0f);

		return color;
	}

	static int maximum_width_count(const vec4& widths) {
		auto maximum_width_count = 0;
		for (int i = 0; i < 4; i++) {
			maximum_width_count += widths[i] == 10.0f;
		}

		return maximum_width_count;
	}
}

/** END - MFLEURY **/