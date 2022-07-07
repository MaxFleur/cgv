#pragma once

#include <string>
#include <vector>

#include <cgv/data/data_view.h>
#include <cgv/render/render_types.h>
#include <cgv/render/texture.h>

struct sarcomere_segment : public cgv::render::render_types {
	vec3 a;
	vec3 b;
	float length;
	box3 box;

	void generate_box(const vec3 na, const vec3 nb, float size) {

		vec3 r(size, size, 4.0f*size);

		box.add_point(na - r);
		box.add_point(na + r);
		box.add_point(nb - r);
		box.add_point(nb + r);

		vec3& pmin = box.ref_min_pnt();
		vec3& pmax = box.ref_max_pnt();
	}
};

struct sliced_volume_data_set : public cgv::render::render_types {
	bool loaded = false;
	bool prepared = false;

	std::string meta_fn = "";
	std::string slices_fn = "";
	std::string sallimus_dots_fn = "";
	std::string sarcomeres_fn = "";
	std::string transfer_function_fn = "";

	std::vector<std::string> stain_names;
	cgv::data::data_format raw_image_format;
	std::vector<cgv::data::data_view> raw_image_slices;
	cgv::data::data_view raw_data;
	cgv::render::texture volume_tex;
	cgv::render::texture gradient_tex;
		
	ivec3 resolution = ivec3(0);
	size_t num_slices = 0;
	float slice_width = 1.0f;
	float slice_height = 1.0f;
	float stack_height = 1.0f;
	float volume_scaling = 1.0f;
	vec3 scaled_size = vec3(1.0f);
		
	std::vector<sarcomere_segment> sarcomere_segments;
	size_t sarcomere_count = 0;

	// transforms a point given in micrometers to the local size of the volume data given by scale
	vec3 to_local_frame(const vec3& p) {
		// flip y-z to account for OpenGL coordinate system
		vec3 s = p;

		// account for height offset
		s.z() -= 0.5f*slice_height + 0.5f*stack_height;

		// scale to fit volume
		s *= volume_scaling;

		// offset to center at origin
		s.x() -= 0.5f;
		s.y() -= 0.5f;

		return s;
	}

	std::vector<vec4> voxel_data;

	template<typename T>
	void extract_voxel_values_impl(size_t voxel_count, float max_value) {

		const T* raw_data_ptr = raw_data.get_ptr<T>();

		for(size_t i = 0; i < 4 * voxel_count; i += 4) {
			voxel_data.push_back(vec4(
				static_cast<float>(raw_data_ptr[i + 0]) / max_value,
				static_cast<float>(raw_data_ptr[i + 1]) / max_value,
				static_cast<float>(raw_data_ptr[i + 2]) / max_value,
				static_cast<float>(raw_data_ptr[i + 3]) / max_value
			));
		}
	}

	void extract_voxel_values() {
		
		size_t voxel_count = resolution.x() * resolution.y() * resolution.z();
		
		voxel_data.clear();
		voxel_data.reserve(voxel_count);

		const auto& type_id = raw_data.get_format()->get_component_type();

		switch(type_id) {
		case cgv::type::info::TI_UINT8: extract_voxel_values_impl<cgv::type::uint8_type>(voxel_count, 255.0f); break;
		case cgv::type::info::TI_UINT16: extract_voxel_values_impl<cgv::type::uint16_type>(voxel_count, 65535.0f); break;
		default: std::cout << "Warning: Could not find implementation of extract_voxel_values that matches the given component type." << std::endl; break;
		}
	}
};
