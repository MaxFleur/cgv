#pragma once

#include <cgv/base/node.h>
#include <cgv/gui/event_handler.h>
#include <cgv/gui/provider.h>
#include <cgv/render/drawable.h>
#include <cgv/render/texture.h>
#include <cgv_gl/sphere_renderer.h>
#include <cgv_glutil/application_plugin.h>
#include <cgv_glutil/color_map.h>
#include <cgv_glutil/color_map_editor.h>
#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/shader_library.h>
#include <cgv_glutil/box_wire_render_data.h>
#include <cgv_glutil/cone_render_data.h>
#include <cgv_glutil/sphere_render_data.h>
#include <plot/plot2d.h>

#include "sliced_volume_data_set.h"
#include "special_volume_renderer.h"
#include "gridtree.h"

#include "plot_overlay.h"

#include "tf_editor_widget.h"
#include "pcp2_overlay.h"
#include "sp_overlay.h"
#include "bpcp_overlay.h"

using namespace cgv::render;

class viewer : public cgv::glutil::application_plugin {
protected:
	enum ViewMode {
		VM_VOLUME = 0,
		VM_SMALL_MULTIPLES = 1,
	};

	enum VisibilityOption {
		VO_NONE,
		VO_SELECTED,
		VO_ALL,
	};

	/// store a pointer to the view
	view* view_ptr = nullptr;
	/// store a pointer to the transfer function editor
	cgv::glutil::color_map_editor_ptr tf_editor_ptr = nullptr;
	/// store a pointer to the plot overlay
	cgv::data::ref_ptr<plot_overlay> length_histogram_po_ptr = nullptr;



	std::string input_path;

	ViewMode view_mode = VM_VOLUME;
	bool use_aligned_segment_box = true;
	VisibilityOption volume_visibility = VO_ALL;
	VisibilityOption segment_visibility = VO_ALL;
	bool show_sallimus_dots;
	bool show_sarcomeres;
	bool show_sample_points = false;
	bool show_sample_voxels = false;

	bool clip_sallimus_dots;
	bool clip_sarcomeres;
	int seg_idx = -1;

	sliced_volume_data_set dataset;
	special_volume_render_style vstyle;

	struct {
		std::vector<unsigned> hist0;
		std::vector<unsigned> hist1;
		std::vector<unsigned> hist2;
		std::vector<unsigned> hist3;
		bool changed = false;
	} histograms;

	unsigned blur_radius;
	cgv::gui::button_ptr prepare_btn = nullptr;
	
	cgv::glutil::frame_buffer_container fbc;
	cgv::glutil::shader_library shaders;

	cgv::glutil::box_wire_render_data<> bounding_box_rd, voxel_boxes_rd;
	box_wire_render_style bwstyle;

	cgv::glutil::sphere_render_data<> sample_points_rd;
	sphere_render_style sstyle;

	// plot length histogram data
	std::vector<vec2> length_histogram;
	
	std::vector<vec3> sallimus_dots;
	sphere_render_style sallimus_dots_style;
	cgv::glutil::sphere_render_data<> sallimus_dots_rd;

	std::vector<vec3> sarcomeres;
	cone_render_style sarcomere_style;
	cgv::glutil::cone_render_data<> sarcomeres_rd;

	tf_editor_widget_ptr tf_editor_w_ptr = nullptr;
	pcp2_overlay_ptr pcp2_ptr = nullptr;
	sp_overlay_ptr sp_ptr = nullptr;
	bpcp_overlay_ptr bpcp_ptr = nullptr;

	gridtree gtree;
	float gridtree_error_threshold = 0.08f;

	void generate_tree_boxes() {

		//tree_boxes_rd.clear();

		if(gtree.levels.size() > 0) {
			//const auto& nodes = oct.levels.back();

			std::cout << "Extracting octree leaf nodes... ";
			cgv::utils::stopwatch s(true);

			const auto& nodes = gtree.extract_leafs(gridtree_error_threshold);

			std::cout << "done (" << s.get_elapsed_time() << "s)" << std::endl;

			std::cout << "Extracted " << nodes.size() << " leaf nodes." << std::endl;
			//for(const auto& n : nodes) {
			//	create_box(n.a, n.b);
			//}

			float total_volume = static_cast<float>(dataset.resolution.x() * dataset.resolution.y() * dataset.resolution.z());

			std::vector<float> mm_data;
			std::vector<float> volume_data;
			for(const auto& n : nodes) {
				vec4 avg = n.stats.get_average();
				vec4 var = n.stats.get_variance();
				vec4 dev = n.stats.get_standard_deviation();

				//vec4 mi = n.stats.get_minimum();
				//vec4 ma = n.stats.get_maximum();
				vec4 mi = avg - dev;
				vec4 ma = avg + dev;

				mm_data.push_back(ma[0]);
				mm_data.push_back(mi[0]);
				mm_data.push_back(ma[1]);
				mm_data.push_back(mi[1]);
				mm_data.push_back(ma[2]);
				mm_data.push_back(mi[2]);
				mm_data.push_back(ma[3]);
				mm_data.push_back(mi[3]);

				ivec3 ext = n.b - n.a;
				float volume = static_cast<float>(ext.x() * ext.y() * ext.z()) / total_volume;

				volume_data.push_back(volume);
				volume_data.push_back(0.25f * (avg[0] + avg[1] + avg[2] + avg[3]));
			}

			if(bpcp_ptr)
				bpcp_ptr->set_data(mm_data, volume_data);
		}

		post_redraw();
	}

	void create_pcp();

	bool read_data_set(context& ctx, const std::string& filename);
	bool read_image_slices(context& ctx, const std::string& filename);
	bool read_sallimus_dots(const std::string& filename);
	bool read_sarcomeres(const std::string& filename);
	bool read_transfer_functions(context& ctx, const std::string& filename);
	bool save_transfer_functions(const std::string& filename);
	bool save_data_set_meta_file(const std::string& filename);

	bool prepare_dataset();

	void extract_sarcomere_segments();

	void calculate_volume_gradients(context& ctx);

	void percentage_threshold(cgv::data::data_view& dv, float percentage);
	void gaussian_blur(cgv::data::data_view& dv, unsigned size);
	void fast_gaussian_blur(cgv::data::data_view& dv, unsigned size);
	void fast_gaussian_bluru(cgv::data::data_view& dv, unsigned size);
	void multiply(cgv::data::data_view& dv, float x);
	std::vector<unsigned> histogram(cgv::data::data_view& dv);

	void create_length_histogram();
	void create_sallimus_dots_geometry();
	void create_sarcomeres_geometry();
	void create_segment_render_data();
	//void create_selected_segment_render_data();

	struct {
		/// file name of loaded transfer functions
		std::string file_name;
		/// file name of to be saved transfer functions (used to trigger save action)
		std::string save_file_name;
		/// track whether the current transfer functions have unsaved changes
		bool has_unsaved_changes = false;
	} fh;

	std::vector<cgv::glutil::color_map> tfs;
	texture tf_tex;

	bool init_tf_texture(cgv::render::context& ctx) {
		std::vector<uint8_t> data(4 * 4 * 256, 0u);

		cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(256, 4, TI_UINT8, cgv::data::CF_RGBA), data.data());

		tf_tex.destruct(ctx);
		tf_tex = cgv::render::texture("uint8[R,G,B,A]", cgv::render::TF_LINEAR, cgv::render::TF_LINEAR);
		return tf_tex.create(ctx, dv, 0);
	}

	bool update_tf_texture(cgv::render::context& ctx) {
		if(tfs.size() == 0)
			return false;

		size_t res = 256;
		std::vector<uint8_t> data(4 * tfs.size() * res);

		// TODO: use float values

		size_t base_idx = 0;
		for(size_t i = 0; i < tfs.size(); ++i) {
			std::vector<rgba> cm_data = tfs[i].interpolate(res);

			for(size_t j = 0; j < res; ++j) {
				data[base_idx + 0] = static_cast<uint8_t>(255.0f * cm_data[j].R());
				data[base_idx + 1] = static_cast<uint8_t>(255.0f * cm_data[j].G());
				data[base_idx + 2] = static_cast<uint8_t>(255.0f * cm_data[j].B());
				data[base_idx + 3] = static_cast<uint8_t>(255.0f * cm_data[j].alpha());
				base_idx += 4;
			}
		}

		cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(res, tfs.size(), TI_UINT8, cgv::data::CF_RGBA), data.data());

		unsigned width = tf_tex.get_width();
		unsigned height = tf_tex.get_height();

		if(tf_tex.is_created() && width == res && height == tfs.size()) {
			return tf_tex.replace(ctx, 0, 0, dv);
		} else {
			tf_tex.destruct(ctx);
			tf_tex = cgv::render::texture("uint8[R,G,B,A]", cgv::render::TF_LINEAR, cgv::render::TF_LINEAR);
			return tf_tex.create(ctx, dv, 0);
		}
	}

	void edit_transfer_function(const size_t index) {
		if(index >= 0 && index < tfs.size()) {
			tf_editor_ptr->set_color_map(&tfs[index]);
			tf_editor_ptr->set_visibility(true);
			post_redraw();
		}
	}











public:
	class viewer();
	std::string get_type_name() const { return "viewer"; }

	void clear(context& ctx);

	bool self_reflect(cgv::reflect::reflection_handler& rh);
	void stream_help(std::ostream& os) {}
	void stream_stats(std::ostream& os) {}

	bool handle_event(cgv::gui::event& e);
	void on_set(void* member_ptr);
	bool on_exit_request();

	bool init(context& ctx);
	void init_frame(context& ctx);
	void draw(context& ctx);

	void create_gui();

private:

		float m_centr_myosin = 0.5f;
		float m_centr_actin = 0.5f;
		float m_centr_obscurin = 0.5f;
		float m_centr_sallimus = 0.5f;

		float m_gaussian_width = 0.5f;
};
