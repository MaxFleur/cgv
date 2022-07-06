#pragma once

#include <chrono>
#include <omp.h>

#include <cgv_glutil/overlay.h>

#include "utils_data_types.h"

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
}