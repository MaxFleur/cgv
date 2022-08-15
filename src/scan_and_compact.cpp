#include "scan_and_compact.h"

namespace cgv {
namespace gpgpu {

void scan_and_compact::destruct(context& ctx) {

	vote_prog.destruct(ctx);
	scan_local_prog.destruct(ctx);
	scan_global_prog.destruct(ctx);
	compact_prog.destruct(ctx);

	delete_buffers();
}

bool scan_and_compact::load_shader_programs(context& ctx) {

	bool res = true;
	std::string where = "scan_and_compact::load_shader_programs()";

	shader_define_map vote_defines, compact_defines;

	if(data_type_def != "") {
		vote_defines["DATA_TYPE_DEFINITION"] = data_type_def;
		compact_defines = vote_defines;
	}
	if(vote_definition != "")
		vote_defines["VOTE_DEFINITION"] = vote_definition;

	res = res && cgv::glutil::shader_library::load(ctx, vote_prog, "vote", vote_defines, true, where);
	res = res && cgv::glutil::shader_library::load(ctx, scan_local_prog, "scan_local_x1", true, where);
	res = res && cgv::glutil::shader_library::load(ctx, scan_global_prog, "scan_global", true, where);
	res = res && cgv::glutil::shader_library::load(ctx, compact_prog, "compact", compact_defines, true, where);

	return res;
}

void scan_and_compact::delete_buffers() {

	delete_buffer(votes_ssbo);
	delete_buffer(prefix_sum_ssbo);
	delete_buffer(blocksums_ssbo);
	delete_buffer(last_sums_ssbo);
}

bool scan_and_compact::init(context& ctx, size_t count) {

	if(!load_shader_programs(ctx))
		return false;

	delete_buffers();

	n = unsigned(count);
	group_size = 64;

	unsigned int block_size = 4 * group_size;

	// Calculate padding for n to next multiple of blocksize.
	n_pad = block_size - (n % (block_size));
	if(n % block_size == 0)
		n_pad = 0;

	num_groups = (n + n_pad + group_size - 1) / group_size;
	num_scan_groups = (n + n_pad + block_size - 1) / block_size;
	unsigned int blocksum_offset_shift = static_cast<unsigned int>(log2f(float(block_size)));

	num_blocksums = num_scan_groups;

	unsigned int num = 1;
	while(num_blocksums > num)
		num <<= 1;
	num_blocksums = num;

	size_t data_size = (n + n_pad) * sizeof(unsigned int);
	size_t blocksums_size = 4 * num_blocksums * sizeof(unsigned int);

	create_buffer(keys_in_ssbo, data_size);
	create_buffer(values_out_ssbo, data_size);
	create_buffer(values_out_ssbo, value_component_count*data_size);
	create_buffer(prefix_sum_ssbo, data_size / 4);
	create_buffer(blocksums_ssbo, blocksums_size);
	create_buffer(scratch_ssbo, 8 * sizeof(unsigned int));

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "n", n);
	distance_prog.set_uniform(ctx, "n_padded", n + n_pad);
	distance_prog.disable(ctx);

	scan_local_prog.enable(ctx);
	scan_local_prog.set_uniform(ctx, "n", n + n_pad);
	scan_local_prog.set_uniform(ctx, "n_scan_groups", num_scan_groups);
	scan_local_prog.set_uniform(ctx, "n_blocksums", num_blocksums);
	scan_local_prog.disable(ctx);

	scan_global_prog.enable(ctx);
	scan_global_prog.set_uniform(ctx, "n", num_blocksums);
	scan_global_prog.disable(ctx);

	scatter_prog.enable(ctx);
	scatter_prog.set_uniform(ctx, "n", n + n_pad);
	scatter_prog.set_uniform(ctx, "n_blocksums", num_blocksums);
	scatter_prog.set_uniform(ctx, "last_blocksum_idx", ((n + n_pad) >> blocksum_offset_shift) - 1);
	scatter_prog.disable(ctx);

	return true;
}

void scan_and_compact::execute(context& ctx, GLuint data_buffer, GLuint value_buffer, const vec3& eye_pos, const vec3& view_dir, GLuint auxiliary_buffer) {

	GLuint values_in_buffer = value_buffer;

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, data_buffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, keys_in_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, values_in_buffer);
	if(auxiliary_type_def != "" && auxiliary_buffer > 0)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, auxiliary_buffer);

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "eye_pos", eye_pos);
	distance_prog.set_uniform(ctx, "view_dir", view_dir);
	glDispatchCompute(num_scan_groups, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	distance_prog.disable(ctx);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, prefix_sum_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, blocksums_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, scratch_ssbo);

	for(unsigned int b = 0; b < 32; b += 2) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, keys_in_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, keys_out_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, values_in_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, values_out_ssbo);

		scan_local_prog.enable(ctx);
		glDispatchCompute(num_scan_groups, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_local_prog.disable(ctx);

		scan_global_prog.enable(ctx);
		glDispatchCompute(4, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_global_prog.disable(ctx);

		scatter_prog.enable(ctx);
		glDispatchCompute(num_scan_groups, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scatter_prog.disable(ctx);

		std::swap(keys_in_ssbo, keys_out_ssbo);
		std::swap(values_in_buffer, values_out_ssbo);
	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, 0);
}

}
}
