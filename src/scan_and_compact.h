#pragma once

#include "gpu_algorithm.h"

#include "lib_begin.h"

namespace cgv {
namespace gpgpu {

/** GPU filter routine implemented using a prefix-sum based radix sort. */
class CGV_API scan_and_compact : public gpu_algorithm {
protected:
	unsigned n = 0;
	unsigned n_pad = 0;
	unsigned group_size = 0;
	unsigned num_groups = 0;
	unsigned num_scan_groups = 0;
	unsigned num_block_sums = 0;

	GLuint votes_ssbo = 0;
	GLuint prefix_sums_ssbo = 0;
	GLuint block_sums_ssbo = 0;
	GLuint last_sum_ssbo = 0;

	std::string data_type_def = "";
	std::string vote_definition = "";

	shader_program vote_prog;
	shader_program scan_local_prog;
	shader_program scan_global_prog;
	shader_program compact_prog;

	bool load_shader_programs(context& ctx);

	void delete_buffers();

public:
	scan_and_compact() : gpu_algorithm() {}

	void destruct(context& ctx);

	bool init(context& ctx, size_t count);

	void execute(context& ctx, GLuint in_buffer, GLuint out_buffer);

	/** GLSL code to define the data type and structure of one element of the input data buffer.
		This effectively defines the contents of a struct used to represent one array element.
		The default value is "float x, y, z;" to map the buffer contents to an array of vec3, e.g.
		positions.
		Careful: vec3 may not be used directly with a shader storage buffer (due to a bug on some older drivers), hence the three
		separate coordinates! However, vec4 works as expected. */
	void set_data_type_override(const std::string& def) { data_type_def = def; }

	/** Resets the data type definition to an empty string, which will not override the default
		definition in the shader. */
	void reset_data_type_override() { data_type_def = ""; }

	/** GLSL code to define the calculation of the vote value used to filter the data elements.
		This defines the body of a method that takes a data element as input and returns a boolean vote.
		Return <false> to exclude this element and <true> to include this element in the compacted array.
		The default definition just returns <false>.
		Variables:
		* reserved (expert use only)
			n, n_padded
		* input (read only)
			data_type d - the input data element of this shader invocation
		* output
			bool
		*/
	void set_vote_definition_override(const std::string& def) { vote_definition = def; }

	/** Resets the vote definition to an empty string, which will not override the default
		definition in the shader. */
	void reset_key_definition_override() { vote_definition = ""; }
};

}
}

#include <cgv/config/lib_end.h>
