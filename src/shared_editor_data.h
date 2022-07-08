/** BEGIN - MFLEURY **/

#pragma once

#include "tf_editor_shared_data_types.h"

/* This class contains data used by all myofibril editors. */
class shared_data {

public:
	std::vector<tf_editor_shared_data_types::centroid> centroids;
};

typedef std::shared_ptr<shared_data> shared_data_ptr;

/** END - MFLEURY **/
