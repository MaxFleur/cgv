#pragma once

#include "utils_data_types.h"

/* This class contains data used by all myofibril editors. */
class shared_data {

public:
	std::vector<utils_data_types::centroid> centroids;
};

typedef std::shared_ptr<shared_data> shared_data_ptr;
