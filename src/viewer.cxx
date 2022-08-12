#include "viewer.h"

#include <filesystem>
#include <random>

#include <omp.h>

#include <cgv/base/find_action.h>
#include <cgv/defines/quote.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/gui/dialog.h>
#include <cgv/math/ftransform.h>
#include <cgv/media/image/image.h>
#include <cgv/media/image/image_reader.h>
#include <cgv/utils/advanced_scan.h>
#include <cgv/utils/file.h>
#include <cgv/utils/stopwatch.h>
#include <cgv_glutil/color_map_reader.h>
#include <cgv_glutil/color_map_writer.h>
#include <cgv/gui/theme_info.h>

#include "tf_editor_shared_data_types.h"



viewer::viewer() : application_plugin("Viewer") {

	blur_radius = 0;

	vstyle.enable_depth_test = true;
	vstyle.size_scale = 200.0f;
	
	mdtf_vstyle.enable_depth_test = true;
	mdtf_vstyle.size_scale = 200.0f;

	sarcomere_style.surface_color = rgb(0.55f, 0.50f, 0.4f);
	sarcomere_style.radius = 0.001;

	sallimus_dots_style.surface_color = rgb(0.75f, 0.25f, 0.15f);
	sallimus_dots_style.radius = 0.002;

	show_sallimus_dots = true;
	show_sarcomeres = true;
	
	fbc.add_attachment("depth", "[D]");
	fbc.add_attachment("color", "flt32[R,G,B,A]");

	shaders.add("screen", "screen_texture.glpr");
	shaders.add("gradient", "gradient_3d");

	// default filter and clamp parameters are ok
	dataset.gradient_tex = texture("flt32[R,G,B,A]");

	tf_editor_ptr = register_overlay<cgv::glutil::color_map_editor>("TF Editor");
	tf_editor_ptr->set_opacity_support(true);
	tf_editor_ptr->set_visibility(false);

	cs_ptr = register_overlay<cgv::glutil::color_selector>("Color Selector");
	cs_ptr->set_visibility(false);

	/** BEGIN - MFLEURY **/
	m_shared_data_ptr = std::make_shared<shared_data>();

	m_editor_lines_ptr = register_overlay<tf_editor_lines>("TF Lines Overlay");
	m_editor_lines_ptr->set_shared_data(m_shared_data_ptr);
	m_editor_lines_ptr->set_visibility(false);

	m_editor_scatterplot_ptr = register_overlay<tf_editor_scatterplot>("TF Scatterplot Overlay");
	m_editor_scatterplot_ptr->set_shared_data(m_shared_data_ptr);
	m_editor_scatterplot_ptr->set_overlay_alignment(cgv::glutil::overlay::AO_START, cgv::glutil::overlay::AO_START);
	m_editor_scatterplot_ptr->set_visibility(false);

	/** END - MFLEURY **/
}

void viewer::clear(cgv::render::context& ctx) {

	ref_special_volume_renderer(ctx, -1);
	ref_mdtf_volume_renderer(ctx, -1);
	ref_sphere_renderer(ctx, -1);
	ref_cone_renderer(ctx, -1);
	ref_box_wire_renderer(ctx, -1);

	fbc.clear(ctx);
	shaders.clear(ctx);

	sallimus_dots_rd.destruct(ctx);
	sarcomeres_rd.destruct(ctx);

	bounding_box_rd.destruct(ctx);
}

bool viewer::self_reflect(cgv::reflect::reflection_handler& rh) {

	return
		rh.reflect_member("input_path", input_path);
}

bool viewer::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if (et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;
		cgv::gui::KeyAction ka = ke.get_action();

		/* ka is one of:
			cgv::gui::[
				KA_PRESS,
				KA_RELEASE,
				KA_REPEAT
			]
		*/
		if (ka == cgv::gui::KA_PRESS) {
			unsigned short key = ke.get_key();

			bool handled = false;

			switch (ke.get_key()) {
			case 'T':
				if(volume_mode == VM_4_CHANNEL) {
					if(tf_editor_ptr) {
						tf_editor_ptr->toggle_visibility();
						handled = true;
					}
				} else {
					if(m_editor_lines_ptr) {
						m_editor_lines_ptr->toggle_visibility();

						set_tree_node_visibility(m_editor_lines_ptr, m_editor_lines_ptr->is_visible());
						handled = true;
					}
				}
				break;
			case 'S':
				if (m_editor_scatterplot_ptr) {
					m_editor_scatterplot_ptr->toggle_visibility();

					set_tree_node_visibility(m_editor_scatterplot_ptr, m_editor_scatterplot_ptr->is_visible());
					handled = true;
				}
				break;
			case 'P':
				add_primitive();
				if (!is_tree_node_visible(m_shared_data_ptr)) {
					set_tree_node_visibility(m_shared_data_ptr, true);
				}
				break;
			case 'R':
				if (m_shared_data_ptr->is_primitive_selected) {
					remove_primitive(m_shared_data_ptr->selected_primitive_id);
					handled = true;
				}
				break;
			default: break;
			}

			if(handled) {
				post_redraw();
				return true;
			}
		}

		return false;
	}
	else if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;
		cgv::gui::MouseAction ma = me.get_action();

		/* ma is one of:
			cgv::gui::[
				MA_DRAG,
				MA_ENTER,
				MA_LEAVE,
				MA_MOVE,
				MA_PRESS,
				MA_RELEASE,
				MA_WHEEL
			]
		*/

		return false;
	}
	else {
		return false;
	}
}

#define CMPM(X) (member_ptr == &X)
#define IFSET(X) if(CMPM(X))

void viewer::on_set(void* member_ptr) {

	if (member_ptr == &input_path) {

#ifdef CGV_FORCE_STATIC
		std::string base_path = cgv::base::ref_prog_path_prefix() + input_path;
#else
		std::string base_path = QUOTE_SYMBOL_VALUE(INPUT_DIR);
		base_path += "/" + input_path;// +"/";
#endif
		std::cout << "Setting base_path to: " << base_path << std::endl;

		context* ctx_ptr = get_context();
		if (ctx_ptr) {
			std::string filename = base_path;
			dataset.meta_fn = filename;
			if (!read_data_set(*ctx_ptr, filename))
				std::cout << "Error: Could not read data set from " << filename << std::endl;
		}
	}

	IFSET(blur_radius) {
		blur_radius = cgv::math::clamp(blur_radius, 0u, 64u);
		if (prepare_btn)
			prepare_btn->set("color", "0xff6666");
	}

	IFSET(fh.file_name) {
		std::string extension = cgv::utils::file::get_extension(fh.file_name);
		// only try to read the filename if it ends with an xml extension
		if (cgv::utils::to_upper(extension) == "XML") {
			context* ctx_ptr = get_context();
			if (ctx_ptr) {
				if (read_transfer_functions(*ctx_ptr, fh.file_name)) {

					dataset.transfer_function_fn = fh.file_name;

					fh.has_unsaved_changes = false;
					on_set(&fh.has_unsaved_changes);
				}
				else {
					std::cout << "Error: could not read transfer functions from " << fh.file_name << std::endl;
				}
			}
		}
	}

	IFSET(fh.save_file_name) {
		std::string extension = cgv::utils::file::get_extension(fh.save_file_name);

		if (extension == "") {
			extension = "xml";
			fh.save_file_name += "." + extension;
		}

		if (cgv::utils::to_upper(extension) == "XML") {
			if (save_transfer_functions(fh.save_file_name)) {

				dataset.transfer_function_fn = fh.save_file_name;

				fh.file_name = fh.save_file_name;
				update_member(&fh.file_name);
				fh.has_unsaved_changes = false;
				on_set(&fh.has_unsaved_changes);
			}
			else {
				std::cout << "Error: Could not write transfer functions to file: " << fh.save_file_name << std::endl;
			}
		}
		else {
			std::cout << "Please specify a xml file name." << std::endl;
		}
	}

	IFSET(fh.has_unsaved_changes) {
		auto ctrl = find_control(fh.file_name);
		if (ctrl)
			ctrl->set("text_color", fh.has_unsaved_changes ? cgv::gui::theme_info::instance().warning_hex() : "");
	}

	IFSET(volume_mode) {
		if(volume_mode == VM_4_CHANNEL) {
			if(m_editor_lines_ptr && m_editor_lines_ptr->is_visible()) {
				m_editor_lines_ptr->set_visibility(false);
				tf_editor_ptr->set_visibility(true);
			}
		} else {
			if(tf_editor_ptr && tf_editor_ptr->is_visible()) {
				tf_editor_ptr->set_visibility(false);
				// m_editor_lines_ptr->set_visibility(true);
			}
		}
		post_recreate_gui();
	}

	// look for updated centroid data
	for (int i = 0; i < m_shared_data_ptr->primitives.size(); ++i) {
		auto value = 0.0f;

		for (int c_protein_i = 0; c_protein_i < 4; c_protein_i++) {
			if (member_ptr == &m_shared_data_ptr->primitives.at(i).type ||
				member_ptr == &m_shared_data_ptr->primitives.at(i).centr_pos[c_protein_i] ||
				member_ptr == &m_shared_data_ptr->primitives.at(i).color ||
				member_ptr == &m_shared_data_ptr->primitives.at(i).centr_widths[c_protein_i]) {

				if (m_editor_lines_ptr && m_editor_lines_ptr->is_visible()) {
					m_editor_lines_ptr->resynchronize();
				}
				if (m_editor_scatterplot_ptr && m_editor_scatterplot_ptr->is_visible()) {
					m_editor_scatterplot_ptr->resynchronize();
				}
			}
		}
	}

	update_member(member_ptr);
	post_redraw();
}

bool viewer::on_exit_request() {
	// TODO: does not seem to fire when window is maximized?
#ifndef _DEBUG
	if (fh.has_unsaved_changes) {
		return cgv::gui::question("The transfer function has unsaved changes. Are you sure you want to quit?");
	}
#endif

	save_data_set_meta_file(dataset.meta_fn);

	return true;
}

bool viewer::init(cgv::render::context& ctx) {

	ref_special_volume_renderer(ctx, 1);
	ref_mdtf_volume_renderer(ctx, 1);
	ref_sphere_renderer(ctx, 1);
	ref_cone_renderer(ctx, 1);
	ref_box_wire_renderer(ctx, 1);

	bool success = true;
	success &= shaders.load_shaders(ctx);

	success &= sallimus_dots_rd.init(ctx);
	success &= sarcomeres_rd.init(ctx);

	success &= bounding_box_rd.init(ctx);
	
	success &= init_tf_texture(ctx);

	bounding_box_rd.add(vec3(0.0f), vec3(1.0f), rgb(0.5f, 0.5f, 0.5f));
	bounding_box_rd.add(vec3(0.0f), vec3(1.0f), rgb(1.0f, 0.0f, 0.0f));

	ctx.set_bg_clr_idx(0);
	return success;
}

void viewer::init_frame(cgv::render::context& ctx) {

	if (!view_ptr) {
		if (view_ptr = find_view_as_node()) {
			ensure_selected_in_tab_group_parent();
		}
	}

	fbc.ensure(ctx);

	if(tf_editor_ptr && histograms.changed) {
		histograms.changed = false;

		auto cm_ptr = tf_editor_ptr->get_color_map();

		int index = -1;
		for(size_t i = 0; i < tfs.size(); ++i) {
			if(cm_ptr == &tfs[i])
				index = i;
		}

		if(index > -1 && index < tfs.size()) {
			switch(index) {
			case 0: tf_editor_ptr->set_histogram_data(histograms.hist0); break;
			case 1: tf_editor_ptr->set_histogram_data(histograms.hist1); break;
			case 2: tf_editor_ptr->set_histogram_data(histograms.hist2); break;
			case 3: tf_editor_ptr->set_histogram_data(histograms.hist3); break;
			default: break;
			}
		}
	}

	/** BEGIN - MFLEURY **/
	if (tf_editor_ptr && tf_editor_ptr->was_updated()) {
		update_tf_texture(ctx);
		fh.has_unsaved_changes = true;
		on_set(&fh.has_unsaved_changes);
	}

	// resynchronize if any primitive updates occured
	if (m_editor_lines_ptr && m_editor_scatterplot_ptr && !m_shared_data_ptr->is_synchronized) {
		if(!m_shared_data_ptr->scatterplot_updated) {
			m_shared_data_ptr->scatterplot_updated = true;
			m_editor_scatterplot_ptr->resynchronize();
		}
		else {
			m_editor_lines_ptr->resynchronize();
		}

		m_shared_data_ptr->is_synchronized = true;
		post_recreate_gui();
	}

	// If draggables were selected, open the color selector (if not visible)
	if (m_shared_data_ptr->is_primitive_selected) {
		if (cs_ptr) {
			if (!cs_ptr->is_visible()) {
				cs_ptr->set_visibility(true);
				m_selected_primitve_id = m_shared_data_ptr->selected_primitive_id;
				// Also set its color to the selected primitive of the draggable
				cs_ptr->set_rgba_color(m_shared_data_ptr->primitives[m_shared_data_ptr->selected_primitive_id].color);
			}
			else {
				// If another primitive was selected, reupdate the selector colors
				if (m_selected_primitve_id != m_shared_data_ptr->selected_primitive_id) {
					m_selected_primitve_id = m_shared_data_ptr->selected_primitive_id;
					cs_ptr->set_rgba_color(m_shared_data_ptr->primitives[m_shared_data_ptr->selected_primitive_id].color);
				}
				if (cs_ptr->was_updated()) {
					// If the selector was updated, set the colors to the selected primitive
					m_shared_data_ptr->primitives[m_shared_data_ptr->selected_primitive_id].color = cs_ptr->get_rgba_color();
					if (m_editor_lines_ptr && m_editor_lines_ptr->is_visible()) {
						m_editor_lines_ptr->resynchronize();
					}
					if (m_editor_scatterplot_ptr && m_editor_scatterplot_ptr->is_visible()) {
						m_editor_scatterplot_ptr->resynchronize();
					}
					// Also update the gui to show the new color
					post_recreate_gui();
				}
			}
		}
	}
	else if (cs_ptr && cs_ptr->is_visible()) {
		cs_ptr->set_visibility(false);
	}
	/** END - MFLEURY **/
}

void viewer::draw(cgv::render::context& ctx) {

	if(!view_ptr || !dataset.loaded || !tf_editor_ptr)
		return;

	vec3 eye_pos = view_ptr->get_eye();
	vec3 view_dir = view_ptr->get_view_dir();

	unsigned offset = 0;
	unsigned count = sarcomeres_rd.ref_pos().size();

	// this matrix transforms the unit cube to the volume size and orientation
	mat4 volume_transform = cgv::math::scale4(dataset.scaled_size);
	// this matrix transforms the unit cube to the clip box
	mat4 clip_box_transform;
	// this matrix transforms the unit cube to the segment bounding box
	mat4 segment_box_transform;

	bool draw_plot = false;
	bool draw_segment_bbox = false;

	clip_box_transform = volume_transform;

	if (seg_idx > -1 && seg_idx < dataset.sarcomere_segments.size()) {
		draw_plot = true;
		draw_segment_bbox = true;

		const auto& seg = dataset.sarcomere_segments[seg_idx];
		if (segment_visibility == VO_SELECTED) {
			offset = 2 * seg_idx;
			count = 2;
		}

		if (use_aligned_segment_box) {
			vec3 box_translate = seg.box.get_center();
			box_translate.z() *= dataset.scaled_size.z();
			box_translate -= vec3(0.5f, 0.5f, 0.5f * dataset.scaled_size.z());

			vec3 box_extent = seg.box.get_extent() * dataset.scaled_size;
			segment_box_transform = cgv::math::translate4(box_translate) * cgv::math::scale4(box_extent);
		}
		else {
			mat4 T = cgv::math::translate4(0.5f * (seg.a + seg.b));

			vec3 dir = normalize(seg.b - seg.a);
			vec3 ref_dir = vec3(0.0f, 0.0f, 1.0f);

			if (abs(dot(dir, ref_dir)) > 0.999f) {
				ref_dir = vec3(1.0f, 0.0f, 0.0f);
				std::swap(dir, ref_dir);
			}

			vec3 ortho = normalize(cross(dir, ref_dir));
			vec3 third = cross(dir, ortho);

			mat4 R;
			R.identity();
			R.set_col(0, vec4(dir, 0.0f));
			R.set_col(1, vec4(ortho, 0.0f));
			R.set_col(2, vec4(third, 0.0f));

			vec3 extent(
				length(seg.a - seg.b) + 0.02f,
				0.04f,
				0.2f
			);
			extent *= dataset.scaled_size;

			mat4 S = cgv::math::scale4(extent);

			segment_box_transform = T * R * S;
		}

		if (volume_visibility == VO_SELECTED)
			clip_box_transform = segment_box_transform;
	}

	fbc.enable(ctx);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	ctx.push_modelview_matrix();
	//ctx.mul_modelview_matrix(rotation);
	//ctx.mul_modelview_matrix(translation);

	if (segment_visibility != VO_NONE) {
		if (show_sallimus_dots)
			sallimus_dots_rd.render(ctx, ref_sphere_renderer(ctx), sallimus_dots_style, offset, count);

		if (show_sarcomeres)
			sarcomeres_rd.render(ctx, ref_cone_renderer(ctx), sarcomere_style, offset, count);
	}

	ctx.pop_modelview_matrix();

	// render the bounding box of the whole volume
	ctx.push_modelview_matrix();
	ctx.mul_modelview_matrix(volume_transform);
	bounding_box_rd.render(ctx, ref_box_wire_renderer(ctx), bwstyle, 0, 1);
	ctx.pop_modelview_matrix();

	fbc.disable(ctx);

	shader_program& prog = shaders.get("screen");
	prog.enable(ctx);

	fbc.enable_attachment(ctx, "color", 0);
	fbc.enable_attachment(ctx, "depth", 1);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	fbc.disable_attachment(ctx, "color");
	fbc.disable_attachment(ctx, "depth");

	prog.disable(ctx);

	if (volume_visibility != VO_NONE) {
		ctx.push_modelview_matrix();
		ctx.mul_modelview_matrix(clip_box_transform);

		if(volume_mode == VM_4_CHANNEL) {
			vstyle.clip_box_transform = clip_box_transform;
			vstyle.volume_transform = volume_transform;

			auto& vr = ref_special_volume_renderer(ctx);
			vr.set_render_style(vstyle);
			vr.set_volume_texture(&dataset.volume_tex);
			vr.set_transfer_function_texture(&tf_tex);
			vr.set_gradient_texture(&dataset.gradient_tex);
			vr.set_depth_texture(fbc.attachment_texture_ptr("depth"));

			vr.render(ctx, 0, 0);
		} else {
			mdtf_vstyle.clip_box_transform = clip_box_transform;
			mdtf_vstyle.volume_transform = volume_transform;

			auto& vr = ref_mdtf_volume_renderer(ctx);
			vr.set_render_style(mdtf_vstyle);
			vr.set_volume_texture(&dataset.volume_tex);
			vr.set_gradient_texture(&dataset.gradient_tex);
			vr.set_depth_texture(fbc.attachment_texture_ptr("depth"));

			/** BEGIN - MFLEURY **/

			if(vr.enable(ctx)) {
				auto& vol_prog = vr.ref_prog();

				const int size = m_shared_data_ptr->primitives.size();
				vol_prog.set_uniform(ctx, "centroid_values_size", size);
				// send all primitive data to the shader
				for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
					const auto color = m_shared_data_ptr->primitives.at(i).color;
					vec4 color_vec{ color.R(), color.G(), color.B(), color.alpha() };

					const auto idx = std::to_string(i);
					vol_prog.set_uniform(ctx, "gtfs[" + idx + "].type", static_cast<int>(m_shared_data_ptr->primitives.at(i).type));
					vol_prog.set_uniform(ctx, "gtfs[" + idx + "].c", m_shared_data_ptr->primitives.at(i).centr_pos);
					vol_prog.set_uniform(ctx, "gtfs[" + idx + "].width", m_shared_data_ptr->primitives.at(i).centr_widths);
					vol_prog.set_uniform(ctx, "gtfs[" + idx + "].color", color_vec);
				}

				vr.draw(ctx, 0, 0);
				vr.disable(ctx);
			}

			/** END - MFLEURY **/
		}

		ctx.pop_modelview_matrix();
	}

	//// render the bounding box of the whole volume
	//ctx.push_modelview_matrix();
	//ctx.mul_modelview_matrix(volume_transform);
	//bounding_box_rd.render(ctx, ref_box_wire_renderer(ctx), bwstyle, 0, 1);
	//ctx.pop_modelview_matrix();

	// render the bounding box of the selected sarcomere segment
	if (draw_segment_bbox) {
		ctx.push_modelview_matrix();
		ctx.mul_modelview_matrix(segment_box_transform);
		bounding_box_rd.render(ctx, ref_box_wire_renderer(ctx), bwstyle, 1, 1);
		ctx.pop_modelview_matrix();
	}
}

void viewer::create_gui() {
	add_decorator("Myofibril Viewer", "heading", "level=2");
	
	if (begin_tree_node("Data Set", blur_radius, false)) {
		align("\a");
		add_member_control(this, "Blur Radius", blur_radius, "value_slider", "min=0;max=8;step=1;ticks=true");
		prepare_btn = add_button("Filter");
		connect_copy(prepare_btn->click, rebind(this, &viewer::prepare_dataset));
		align("\b");
		end_tree_node(blur_radius);
	}

	add_member_control(this, "Volume Mode", volume_mode, "dropdown", "enums='4-Channel, Multi-Dimensional'");

	if(volume_mode == VM_4_CHANNEL) {
		if(begin_tree_node("Volume Rendering", vstyle, false)) {
			align("\a");
			add_gui("vstyle", vstyle);
			align("\b");
			end_tree_node(vstyle);
		}

		add_decorator("", "separator");

		if(begin_tree_node("Transfer Functions", tf_editor_ptr, true, "active=false")) {
			align("\a");

			std::string filter = "XML Files (xml):*.xml|All Files:*.*";
			add_gui("File", fh.file_name, "file_name", "title='Open Transfer Function';filter='" + filter + "';save=false;w=136;small_icon=true;align_gui=' '" + (fh.has_unsaved_changes ? ";text_color=" + cgv::gui::theme_info::instance().warning_hex() : ""));
			add_gui("save_file_name", fh.save_file_name, "file_name", "title='Save Transfer Function';filter='" + filter + "';save=true;control=false;small_icon=true");

			for(size_t i = 0; i < dataset.stain_names.size(); ++i) {
				add_member_control(this, "", dataset.stain_names[i], "string", "w=168", " ");
				connect_copy(add_button("@1edit", "w=20;")->click, cgv::signal::rebind(this, &viewer::edit_transfer_function, cgv::signal::_c<size_t>(i)));
			}

			inline_object_gui(tf_editor_ptr);
			align("\b");
			end_tree_node(tf_editor_ptr);
		}
	} else {
		/** BEGIN - MFLEURY **/
		if(begin_tree_node("Volume Rendering", mdtf_vstyle, false)) {
			align("\a");
			add_gui("vstyle", mdtf_vstyle);
			align("\b");
			end_tree_node(mdtf_vstyle);
		}

		add_decorator("", "separator");

		if(begin_tree_node("TF Editor - Lines", m_editor_lines_ptr, false)) {
			align("\a");
			inline_object_gui(m_editor_lines_ptr);
			align("\b");
			end_tree_node(m_editor_lines_ptr);
		}

		if (begin_tree_node("TF Editor - SPLOM", m_editor_scatterplot_ptr, false)) {
			align("\a");
			inline_object_gui(m_editor_scatterplot_ptr);
			align("\b");
			end_tree_node(m_editor_scatterplot_ptr);
		}

		if (begin_tree_node("Primitives", m_shared_data_ptr, false)) {
			align("\a");

			auto const add_primitive_button = add_button("Add Primitive");
			connect_copy(add_primitive_button->click, rebind(this, &viewer::add_primitive));

			for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {

				bool node_is_open = begin_tree_node_void("Primitive " + std::to_string(i + 1), &m_shared_data_ptr->primitives.at(i), -1, true, "level=2;options='w=180';align=''");

				connect_copy(add_button("@9+", "w=20;")->click, cgv::signal::rebind(this, &viewer::remove_primitive, cgv::signal::_c<size_t>(i)));

				if(node_is_open) {
					align("\a");
					add_member_control(this, "Type", m_shared_data_ptr->primitives.at(i).type, "dropdown", "enums=Gaussian, Hyperbox, Hyperellipsoid");

					// Color widget
					add_member_control(this, "Color", m_shared_data_ptr->primitives.at(i).color, "", "");
					// Centroid parameters themselves
					add_member_control(this, "Pos Myosin", m_shared_data_ptr->primitives.at(i).centr_pos[0], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Actin", m_shared_data_ptr->primitives.at(i).centr_pos[1], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Obscurin", m_shared_data_ptr->primitives.at(i).centr_pos[2], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Sallimus", m_shared_data_ptr->primitives.at(i).centr_pos[3], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");

					// Gaussian width
					add_member_control(this, "Width Myosin", m_shared_data_ptr->primitives.at(i).centr_widths[0], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Actin", m_shared_data_ptr->primitives.at(i).centr_widths[1], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Obscurin", m_shared_data_ptr->primitives.at(i).centr_widths[2], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Salimus", m_shared_data_ptr->primitives.at(i).centr_widths[3], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");

					align("\b");
					end_tree_node(m_shared_data_ptr->primitives.at(i));
				}

				/*if (begin_tree_node("Primitive " + std::to_string(i + 1), m_shared_data_ptr->primitives.at(i), true)) {
					align("\a");

					connect_copy(add_button("@9+", "w=20;")->click, cgv::signal::rebind(this, &viewer::remove_primitive, cgv::signal::_c<size_t>(i)));

					add_member_control(this, "Type", m_shared_data_ptr->primitives.at(i).type, "dropdown", "enums=Gaussian, Hyperbox, Hyperellipsoid");

					// Color widget
					add_member_control(this, "Color", m_shared_data_ptr->primitives.at(i).color, "", "");
					// Centroid parameters themselves
					add_member_control(this, "Pos Myosin", m_shared_data_ptr->primitives.at(i).centr_pos[0], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Actin", m_shared_data_ptr->primitives.at(i).centr_pos[1], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Obscurin", m_shared_data_ptr->primitives.at(i).centr_pos[2], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Pos Sallimus", m_shared_data_ptr->primitives.at(i).centr_pos[3], "value_slider",
						"min=0.0;max=1.0;step=0.0001;ticks=true");

					// Gaussian width
					add_member_control(this, "Width Myosin", m_shared_data_ptr->primitives.at(i).centr_widths[0], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Actin", m_shared_data_ptr->primitives.at(i).centr_widths[1], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Obscurin", m_shared_data_ptr->primitives.at(i).centr_widths[2], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
					add_member_control(this, "Width Salimus", m_shared_data_ptr->primitives.at(i).centr_widths[3], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");

					align("\b");
					end_tree_node(m_shared_data_ptr->primitives.at(i));
				}*/
			}

			align("\b");
			end_tree_node(m_shared_data_ptr);
		}

		if(begin_tree_node("Color Selector", cs_ptr, false)) {
			align("\a");
			inline_object_gui(cs_ptr);
			align("\b");
			end_tree_node(cs_ptr);
		}
		/** END - MFLEURY **/
	}
	add_decorator("", "separator");

	if(begin_tree_node("Sarcomere Visualization", dataset.sarcomere_count, false)) {
		align("\a");

		if(begin_tree_node("Sallimus dots", sallimus_dots_style, false)) {
			align("\a");
			add_gui("", sallimus_dots_style);
			align("\b");
			end_tree_node(sallimus_dots_style);
		}
		add_member_control(this, "Show", show_sallimus_dots, "check");

		if(begin_tree_node("Sarcomeres", sarcomere_style, false)) {
			align("\a");
			add_gui("", sarcomere_style);
			align("\b");
			end_tree_node(sarcomere_style);
		}
		add_member_control(this, "Show", show_sarcomeres, "check");

		add_member_control(this, "Align Box", use_aligned_segment_box, "check");

		add_member_control(this, "Show Volume", volume_visibility, "dropdown", "enums='None, Selected, All'");
		add_member_control(this, "Show Segments", segment_visibility, "dropdown", "enums='None, Selected, All'");

		add_decorator("Segment Selection", "heading", "level=3");
		add_view("Sarcomere Count", dataset.sarcomere_count);
		add_member_control(this, "Segment", seg_idx, "value_slider", "min=-1;step=1;max=" + (dataset.loaded ? std::to_string(dataset.sarcomere_count - 1) : "100"));
		add_member_control(this, "", seg_idx, "wheel", "min=-1;step=0.25;max=100");

		align("\b");
	}
}

void viewer::add_primitive() {
	// Hardcoded boundary, this might change later
	if (m_shared_data_ptr->primitives.size() == 8) {
		return;
	}

	// Create a new centroid and store it
	shared_data::primitive centr;
	m_shared_data_ptr->primitives.push_back(centr);

	if (m_editor_lines_ptr) {
		m_editor_lines_ptr->primitive_added();
	}
	if (m_editor_scatterplot_ptr) {
		m_editor_scatterplot_ptr->primitive_added();
	}

	post_recreate_gui();
}

void viewer::remove_primitive(int index) {
	std::cout << "Primitve hit!" << std::endl;
	
	m_shared_data_ptr->primitives.erase(m_shared_data_ptr->primitives.begin() + index);
	m_shared_data_ptr->is_primitive_selected = false;
	m_shared_data_ptr->selected_primitive_id = INT_MAX;

	post_recreate_gui();

	if (m_editor_lines_ptr && m_editor_lines_ptr->is_visible()) {
		m_editor_lines_ptr->resynchronize();
	}
	if (m_editor_scatterplot_ptr && m_editor_scatterplot_ptr->is_visible()) {
		m_editor_scatterplot_ptr->resynchronize();
	}
}

bool viewer::read_data_set(context& ctx, const std::string& filename) {

	// read the whole data set from a given meta file
	// meta files contain information about and paths to the individual files used in a data set
	// paths in a meta file are specified relative to the meta file location
	if (!cgv::utils::file::exists(filename)) {
		std::cout << "Warning: Could not open file " << filename << std::endl;
		return false;
	}

	std::string content;
	std::vector<cgv::utils::line> lines;
	cgv::utils::file::read(filename, content, true);
	cgv::utils::split_to_lines(content, lines);

	//std::vector<std::string> stain_names;
	auto& stain_names = dataset.stain_names;
	std::string slices_fn = "";
	std::string sallimus_dots_fn = "";
	std::string sarcomeres_fn = "";
	std::string transfer_function_fn = "";

	for (size_t i = 0; i < lines.size(); ++i) {
		// convert token to string
		std::string line = to_string(lines[i]);

		if (line[0] == '#') {
			if (line != "#SLICED_MICROSCOPY_META") {
				// first line must be magic sequence
				break;
			}
		}
		else {
			// other lines must be of format <identifier> = <value>
			std::vector<cgv::utils::token> parts;
			// split line at equals character and remove whitspaces
			cgv::utils::split_to_tokens(line, parts, "", true, "", "", "= \t\n");
			if (parts.size() == 2) {
				std::string identifier = to_string(parts[0]);
				cgv::utils::token& value_tok = parts[1];
				if (identifier == "stains") {
					// read stains
					std::vector<cgv::utils::token> values;
					// split value token at commas
					cgv::utils::split_to_tokens(value_tok, values, "", true, "", "", ", \t\n");
					// we only expect 4 stain names
					if (values.size() == 4) {
						for (size_t j = 0; j < values.size(); ++j)
							stain_names.push_back(to_string(values[j]));
					}
				}
				else if (identifier == "slices") {
					// read image slices file name
					slices_fn = to_string(value_tok);
				}
				else if (identifier == "sallimus_dots") {
					// read sallimus dots file name
					sallimus_dots_fn = to_string(value_tok);
				}
				else if (identifier == "sarcomeres") {
					// read sarcomeres file name
					sarcomeres_fn = to_string(value_tok);
				}
				else if (identifier == "transfer_function") {
					// read transfer function file name
					transfer_function_fn = to_string(value_tok);
				}
			}
		}
	}

	// check if all necessary information was read
	if (stain_names.size() != 4 ||
		slices_fn == "" ||
		sallimus_dots_fn == "" ||
		sarcomeres_fn == "") {
		std::cout << "Error: Not enough information in meta file " << filename << std::endl;
		return false;
	}

	dataset.slices_fn = slices_fn;
	dataset.sallimus_dots_fn = sallimus_dots_fn;
	dataset.sarcomeres_fn = sarcomeres_fn;
	dataset.transfer_function_fn = transfer_function_fn;

	// get parent path of input meta file
	std::filesystem::path meta_path(filename);
	std::string parent_path = meta_path.parent_path().string() + "/";

	slices_fn = parent_path + slices_fn;
	sallimus_dots_fn = parent_path + sallimus_dots_fn;
	sarcomeres_fn = parent_path + sarcomeres_fn;

	dataset.loaded = read_image_slices(ctx, slices_fn);
	if (dataset.loaded)
		dataset.prepared = prepare_dataset();

	if (read_sarcomeres(sarcomeres_fn))
		extract_sarcomere_segments();
	else
		std::cout << "Error: Could not read sarcomeres from " << sarcomeres_fn << std::endl;

	create_segment_render_data();

#ifndef _DEBUG
	std::cout << "Building gridtree... ";
	cgv::utils::stopwatch s0(true);

	gtree.build(dataset);

	std::cout << "done (" << s0.get_elapsed_time() << "s)" << std::endl;

	gtree.print_info();

	generate_tree_boxes();
#endif
	std::cout << "Extracting voxel values ...";
	cgv::utils::stopwatch s(true);

	dataset.extract_voxel_values();

	std::cout << "done (" << s.get_elapsed_time() << "s)" << std::endl;

	/** BEGIN - MFLEURY **/
	if(m_editor_lines_ptr)
		m_editor_lines_ptr->set_data_set(&dataset);

	if(m_editor_scatterplot_ptr)
		m_editor_scatterplot_ptr->set_data_set(&dataset);
	/** END - MFLEURY **/

	// transfer function is optional
	if (transfer_function_fn == "") {
		transfer_function_fn = QUOTE_SYMBOL_VALUE(INPUT_DIR);
		transfer_function_fn += "/../data/default.xml";
	}
	else {
		std::filesystem::path transfer_function_path(transfer_function_fn);
		if (transfer_function_path.is_relative())
			transfer_function_fn = parent_path + transfer_function_fn;
	}

	fh.file_name = transfer_function_fn;
	update_member(&fh.file_name);
	fh.has_unsaved_changes = false;
	on_set(&fh.has_unsaved_changes);

	read_transfer_functions(ctx, transfer_function_fn);

	post_recreate_gui();
}

bool viewer::read_image_slices(context& ctx, const std::string& filename) {

	std::cout << "Loading image slices...";

	cgv::media::image::image_reader image(dataset.raw_image_format);

	cgv::utils::stopwatch s(true);

	unsigned num_images = 0;

	if (image.open(filename)) {
		num_images = image.get_nr_images();
		dataset.raw_image_slices.resize(num_images);

		for (unsigned i = 0; i < num_images; ++i) {
			if (image.seek_image(i)) {
				image.read_image(dataset.raw_image_slices[i]);
			}
		}
		image.close();
	}
	else {
		std::cout << "Error: Could not read image file \"" << filename << "\"" << std::endl;
		return false;
	}

	if (num_images == 0) {
		return false;
		std::cout << "Error: No images were read from file \"" << filename << "\"" << std::endl;
	}

	// assume 4 protein stains per data set to calculate numer of slices
	dataset.num_slices = static_cast<size_t>(num_images / 4);

	dataset.resolution = ivec3(1024, 1024, dataset.num_slices);

	// set the real-world dimensions and scaling for visualization
	// this is hard-coded for now but in the future needs to be read from the file
	float pixel_size = 0.0329471f; // size of one pixel in micrometers

	dataset.slice_width = 1024.0f * pixel_size; // width of one slice in micrometers;
	dataset.slice_height = 0.2f; // height of one slice in micrometers;
	dataset.stack_height = static_cast<float>(dataset.num_slices) * dataset.slice_height; // total height of the slice stack in micrometers;
	// scale to make size 1 in x and y direction
	dataset.volume_scaling = 1.0f / dataset.slice_width;

	dataset.scaled_size = dataset.volume_scaling * vec3(dataset.slice_width, dataset.slice_width, dataset.stack_height);

	std::cout << "done (" << s.get_elapsed_time() << "s)" << std::endl;

	if(!dataset.raw_image_slices.empty()) {
		const auto& fmt = *dataset.raw_image_slices[0].get_format();
		const auto& type_id = fmt.get_component_type();
		std::cout << "Image Component Type: " << cgv::type::info::get_type_name(type_id) << std::endl << std::endl;
	}

	return true;
}

bool viewer::read_sallimus_dots(const std::string& filename) {

	if (!std::filesystem::exists(filename))
		return false;

	// clear previous data
	sallimus_dots.clear();

	std::string content;

	// Get the filesize in a x64 compatible way
	uint64_t file_size = std::filesystem::file_size(filename);

	// Resize the content to fit all of the files content
	content.resize(file_size);
	FILE* fp = std::fopen(filename.c_str(), "r");
	size_t n = std::fread(&content[0], 1, file_size, fp);
	// Resize content to the actual size of the data that was read
	content.resize(n);
	std::fclose(fp);

	bool read = true;
	size_t nl_pos = content.find_first_of("\n");
	size_t line_offset = 0;
	bool first_line = true;

	while (read) {
		std::string line = "";

		if (nl_pos == std::string::npos) {
			read = false;
			line = content.substr(line_offset, std::string::npos);
		}
		else {
			size_t next_line_offset = nl_pos;
			line = content.substr(line_offset, next_line_offset - line_offset);
			line_offset = next_line_offset + 1;
			nl_pos = content.find_first_of('\n', line_offset);
		}

		// skip first line
		if (first_line) {
			first_line = false;
			continue;
		}

		if (line != "") {
			// read position values
			size_t space_pos = line.find_first_of(" ");
			size_t last_space_pos = 0;

			vec3 pos(0.0f);
			std::string value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pos[0] = std::strtof(value_str.c_str(), nullptr);

			last_space_pos = space_pos + 1;
			space_pos = line.find_first_of(" ", last_space_pos);
			value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pos[1] = std::strtof(value_str.c_str(), nullptr);

			value_str = line.substr(space_pos + 1);
			pos[2] = std::strtof(value_str.c_str(), nullptr);

			// transform coordinates and add to data set
			sallimus_dots.push_back(dataset.to_local_frame(pos));
		}
	}

	return true;
}

bool viewer::read_sarcomeres(const std::string& filename) {

	if (!std::filesystem::exists(filename))
		return false;

	// clear previous data
	sarcomeres.clear();

	std::string content;

	// Get the filesize in a x64 compatible way
	uint64_t file_size = std::filesystem::file_size(filename);

	// Resize the content to fit all of the files content
	content.resize(file_size);
	FILE* fp = std::fopen(filename.c_str(), "r");
	size_t n = std::fread(&content[0], 1, file_size, fp);
	// Resize content to the actual size of the data that was read
	content.resize(n);
	std::fclose(fp);

	bool read = true;
	size_t nl_pos = content.find_first_of("\n");
	size_t line_offset = 0;
	bool first_line = true;

	while (read) {
		std::string line = "";

		if (nl_pos == std::string::npos) {
			read = false;
			line = content.substr(line_offset, std::string::npos);
		}
		else {
			size_t next_line_offset = nl_pos;
			line = content.substr(line_offset, next_line_offset - line_offset);
			line_offset = next_line_offset + 1;
			nl_pos = content.find_first_of('\n', line_offset);
		}

		// skip first line
		if (first_line) {
			first_line = false;
			continue;
		}

		if (line != "") {
			// read position values
			vec3 pa(0.0f);
			vec3 pb(0.0f);

			size_t last_space_pos = 0;
			size_t space_pos = line.find_first_of(" ");
			std::string value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pa[0] = std::strtof(value_str.c_str(), nullptr);

			last_space_pos = space_pos + 1;
			space_pos = line.find_first_of(" ", last_space_pos);
			value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pa[1] = std::strtof(value_str.c_str(), nullptr);

			last_space_pos = space_pos + 1;
			space_pos = line.find_first_of(" ", last_space_pos);
			value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pa[2] = std::strtof(value_str.c_str(), nullptr);


			// skip double space
			last_space_pos = space_pos + 2;
			space_pos = line.find_first_of(" ", last_space_pos);
			value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pb[0] = std::strtof(value_str.c_str(), nullptr);

			last_space_pos = space_pos + 1;
			space_pos = line.find_first_of(" ", last_space_pos);
			value_str = line.substr(last_space_pos, space_pos - last_space_pos);
			pb[1] = std::strtof(value_str.c_str(), nullptr);

			value_str = line.substr(space_pos + 1);
			pb[2] = std::strtof(value_str.c_str(), nullptr);

			// transform coordinates and add to data set
			sarcomeres.push_back(dataset.to_local_frame(pa));
			sarcomeres.push_back(dataset.to_local_frame(pb));
		}
	}

	return true;
}

bool viewer::read_transfer_functions(context& ctx, const std::string& filename) {
	cgv::glutil::color_map_reader::result color_maps;
	tfs.clear();

	for (size_t i = 0; i < 4; ++i) {
		cgv::glutil::color_map tf;
		tf.add_color_point(0.0f, rgb(1.0f));
		tf.add_opacity_point(0.0f, 0.0f);
		tf.add_opacity_point(1.0f, 1.0f);
		tfs.push_back(tf);
	}

	if (cgv::glutil::color_map_reader::read_from_xml(filename, color_maps)) {
		if (color_maps.size() > 0) {
			for (const auto& stain : dataset.stain_names) {
				int idx = -1;
				for (size_t i = 0; i < color_maps.size(); ++i) {
					if (stain == color_maps[i].first) {
						idx = i;
						break;
					}
				}

				if (idx > -1) {
					tfs[idx] = color_maps[idx].second;
				}
			}
		}
		else {
			std::cout << "Error: transfer functions given in file " << filename << " incomplete" << std::endl;
		}
	}

	update_tf_texture(ctx);
	return true;
}

bool viewer::save_transfer_functions(const std::string& filename) {

	if (dataset.stain_names.size() == 4 && tfs.size() == 4)
		return cgv::glutil::color_map_writer::write_to_xml(filename, dataset.stain_names, tfs);
	return false;
}

bool viewer::save_data_set_meta_file(const std::string& filename) {
	std::string content = "";

	content += "#SLICED_MICROSCOPY_META\n";

	content += "stains = ";
	size_t count = dataset.stain_names.size();
	for (size_t i = 0; i < count; ++i) {
		content += dataset.stain_names[i];
		if (i + 1 < count)
			content += ",";
	}

	content += "\n";
	content += "slices = " + dataset.slices_fn + "\n";
	content += "sallimus_dots = " + dataset.sallimus_dots_fn + "\n";
	content += "sarcomeres = " + dataset.sarcomeres_fn + "\n";

	if (dataset.transfer_function_fn != "")
		content += "transfer_function = " + dataset.transfer_function_fn;

	return cgv::utils::file::write(filename, content, true);
}

bool viewer::prepare_dataset() {

	context* ctx_ptr = get_context();
	if (!ctx_ptr) {
		std::cout << "Error: Could not prepare data set. Context is missing." << std::endl;
		return false;
	}

	std::cout << "Preparing data set..." << std::endl;
	cgv::utils::stopwatch s0(true);

	std::vector<data_view>* data_views = &dataset.raw_image_slices;

	if (blur_radius > 0) {
		std::cout << "Blurring slices... ";
		cgv::utils::stopwatch s1(true);
		data_views = new std::vector<data_view>(dataset.raw_image_slices.size());

#pragma omp parallel for
		for (int i = 0; i < data_views->size(); ++i) {
			data_view copy(dataset.raw_image_slices[i]);
			fast_gaussian_blur(copy, blur_radius);
			(*data_views)[i] = copy;
		}
		std::cout << "done (" << s1.get_elapsed_time() << "s)" << std::endl;
	}

	std::cout << "Combining components and slices... ";
	cgv::utils::stopwatch s1(true);

	std::vector<data_view> rgba_views(data_views->size() / 4);
	cgv::data::data_view tex_data;

	bool combine_success = true;
	for (int i = 0; i < rgba_views.size(); ++i) {
		if (!cgv::data::data_view::combine_components(rgba_views[i], data_views->begin() + 4 * i, data_views->begin() + 4 * i + 4)) {
			combine_success = false;
		}
	}

	if (!combine_success) {
		std::cout << "Error: Could not combine data view components." << std::endl;
		return false;
	}

	if (!cgv::data::data_view::compose(tex_data, rgba_views)) {
		std::cout << "Error: Could not compose data views." << std::endl;
		return false;
	}
	std::cout << "done (" << s1.get_elapsed_time() << "s)" << std::endl;

	// TODO: maybe convert to float values before putting in to the texture
	dataset.raw_data = tex_data;

	std::cout << "Creating volume texture... ";
	s1.restart();
	auto& volume_tex = dataset.volume_tex;
	volume_tex.create(*ctx_ptr, tex_data, 0);
	volume_tex.set_min_filter(cgv::render::TF_LINEAR);
	volume_tex.set_mag_filter(cgv::render::TF_LINEAR);
	volume_tex.set_wrap_s(cgv::render::TW_CLAMP_TO_EDGE);
	volume_tex.set_wrap_t(cgv::render::TW_CLAMP_TO_EDGE);
	volume_tex.set_wrap_r(cgv::render::TW_CLAMP_TO_EDGE);
	std::cout << "done (" << s1.get_elapsed_time() << "s)" << std::endl;

	std::cout << "Computing gradients... ";
	s1.restart();
	calculate_volume_gradients(*ctx_ptr);
	std::cout << "done (" << s1.get_elapsed_time() << "s)" << std::endl;

//#ifndef _DEBUG
	std::cout << "Computing histograms... ";
	s1.restart();

	std::vector<unsigned>* hists[4] = {
		&histograms.hist0,
		&histograms.hist1,
		&histograms.hist2,
		&histograms.hist3
	};

	histograms.num_bins = 512;
	histograms.hist0.resize(histograms.num_bins, 0);
	histograms.hist1.resize(histograms.num_bins, 0);
	histograms.hist2.resize(histograms.num_bins, 0);
	histograms.hist3.resize(histograms.num_bins, 0);

//#pragma omp parallel for
	for (int i = 0; i < data_views->size(); ++i) {
		std::vector<unsigned>& hist = (*hists[i % 4]);
		std::vector<unsigned> h = calculate_histogram((*data_views)[i]);

		for (unsigned j = 0; j < histograms.num_bins; ++j) {
			hist[j] += h[j];
		}
	}

	histograms.changed = true;
	std::cout << "done (" << s1.get_elapsed_time() << "s)" << std::endl;

//#endif

	if (prepare_btn)
		prepare_btn->set("color", "");

	post_redraw();

	std::cout << "Data is ready (" << s0.get_elapsed_time() << "s)" << std::endl;
	return true;
}

void viewer::extract_sarcomere_segments() {

	auto& segments = dataset.sarcomere_segments;

	float volume_height = dataset.stack_height * dataset.volume_scaling;

	vec3 offset(0.5f, 0.5f, 0.5f * volume_height);

	for (unsigned i = 0; i < sarcomeres.size(); i += 2) {
		vec3 a = sarcomeres[i + 0];
		vec3 b = sarcomeres[i + 1];

		vec3 na = a;
		vec3 nb = b;

		na += offset;
		nb += offset;

		na.z() /= volume_height;
		nb.z() /= volume_height;

		sarcomere_segment seg;
		seg.a = a;
		seg.b = b;
		seg.length = length(a - b);

		seg.generate_box(na, nb, 0.02f);

		if (seg.length > 0.0f)
			segments.push_back(seg);
	}



	std::sort(segments.begin(), segments.end(),
		[](const auto& a, const auto& b) {
		return a.length < b.length;
	}
	);

	dataset.sarcomere_count = dataset.sarcomere_segments.size();
	update_member(&dataset.sarcomere_count);

	int idx = 0;
	auto ctrl = find_control(seg_idx, &idx);
	if (ctrl) ctrl->set("max", dataset.sarcomere_count - 1);
	idx += 1;
	ctrl = find_control(seg_idx, &idx);
	if (ctrl) ctrl->set("max", dataset.sarcomere_count - 1);
}

void viewer::calculate_volume_gradients(context& ctx) {

	unsigned group_size = 4;

	auto& volume_tex = dataset.volume_tex;
	ivec3 vres(volume_tex.get_width(), volume_tex.get_height(), volume_tex.get_depth());
	uvec3 num_groups = ceil(vec3(vres) / (float)group_size);

	if (!dataset.gradient_tex.is_created())
		dataset.gradient_tex.create(ctx, cgv::render::TT_3D, vres[0], vres[1], vres[2]);

	// bind textures as 3D images (compare uniform declarations in compute shader gradient_3d.glcs)
	const int volume_tex_handle = (const int&)volume_tex.handle - 1;
	const int gradient_tex_handle = (const int&)dataset.gradient_tex.handle - 1;
	glBindImageTexture(0, volume_tex_handle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);
	glBindImageTexture(1, gradient_tex_handle, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

	// enable, configure, run and disable program
	auto& gradient_prog = shaders.get("gradient");
	gradient_prog.enable(ctx);
	glDispatchCompute(num_groups[0], num_groups[1], num_groups[2]);
	// wait for compute shader to finish work
	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	gradient_prog.disable(ctx);

	// clear 3D image bindings
	glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);
	glBindImageTexture(1, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
}

void viewer::percentage_threshold(cgv::data::data_view& dv, float percentage) {

	struct pixel {
		ivec2 coords;
		unsigned char val;

		pixel(ivec2 c, unsigned char v) {
			coords = c;
			val = v;
		}

		bool operator<(const pixel& other) {

			return val < other.val;
		}
	};

	std::vector<pixel> pixels;

	const data_format* src_df_ptr = dv.get_format();
	unsigned n_dims = src_df_ptr->get_nr_dimensions();

	unsigned w = src_df_ptr->get_width();
	unsigned h = src_df_ptr->get_height();

	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			unsigned char val = dv.get<unsigned char>(0, x, y);
			pixels.push_back(pixel(ivec2(y, x), val));
		}
	}

	std::sort(pixels.begin(), pixels.end());

	percentage = cgv::math::clamp(percentage, 0.0f, 1.0f);
	unsigned split_idx = static_cast<unsigned>(percentage * pixels.size());

	for (unsigned j = 0; j < split_idx; ++j) {
		const ivec2& c = pixels[j].coords;
		dv.set(c.x() + w * c.y(), 0);
	}

	for (unsigned j = split_idx; j < pixels.size(); ++j) {
		const ivec2& c = pixels[j].coords;
		dv.set(c.x() + w * c.y(), 255);
	}
}

// TOOD: check for image dimensions and components
void viewer::gaussian_blur(cgv::data::data_view& dv, unsigned radius) {

	if (radius == 0) return;

	data_view temp(dv);
	const data_format* df_ptr = dv.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	int ir = static_cast<int>(ceil(2.57f * radius)); // significant radius
	std::vector<float> kernel((2 * ir + 1) * (2 * ir + 1));

	float sigma = static_cast<float>(radius);

	float s = 2.0f * sigma * sigma;
	float sum = 0.0f;

	int kernel_size = (2 * ir + 1);

	for (int y = -ir; y <= ir; ++y) {
		for (int x = -ir; x <= ir; ++x) {
			float fx = static_cast<float>(x);
			float fy = static_cast<float>(y);
			float r_sqr = fx * fx + fy * fy;

			float value = std::exp(-r_sqr / s) / (M_PI * s);

			int idx = (x + ir) + kernel_size * (y + ir);
			kernel[idx] = value;
			sum += value;
		}
	}

	for (unsigned i = 0; i < kernel.size(); ++i) {
		kernel[i] /= sum;
	}

	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			float v = 0.0f;

			for (int ky = -ir; ky <= ir; ++ky) {
				for (int kx = -ir; kx <= ir; ++kx) {
					int x_idx = static_cast<int>(x) + kx;
					int y_idx = static_cast<int>(y) + ky;

					x_idx = cgv::math::clamp(x_idx, 0, static_cast<int>(w) - 1);
					y_idx = cgv::math::clamp(y_idx, 0, static_cast<int>(h) - 1);

					float val = static_cast<float>(temp.get<unsigned char>(0, x_idx, y_idx));

					int idx = (kx + ir) + kernel_size * (ky + ir);
					val *= kernel[idx];

					v += val;
				}
			}

			dv.set(y + w * x, static_cast<unsigned char>(cgv::math::clamp(v, 0.0f, 255.0f)));
		}
	}
}

/** From: http://blog.ivank.net/fastest-gaussian-blur.html
	gauss_boxes()
	box_blur_horizontal()
	box_blur_vertical()
	*/
std::vector<unsigned> gauss_boxes(unsigned n, unsigned r) {

	float rf = static_cast<float>(r);
	float nf = static_cast<float>(n);

	float wIdeal = sqrt((12.0f * rf * rf / nf) + 1.0f);
	int wl = static_cast<int>(floor(wIdeal));

	if (wl % 2 == 0)
		--wl;
	if (wl < 0)
		wl = 0;

	int wu = wl + 2;

	float mIdeal = (12.0f * rf * rf - nf * wl * wl - 4.0f * nf * wl - 3.0f * nf) / (-4.0f * wl - 4.0f);
	unsigned m = static_cast<int>(round(mIdeal));

	std::vector<unsigned> radii(n);
	for (int i = 0; i < n; ++i) {
		int size = i < m ? wl : wu;
		radii[i] = static_cast<unsigned>((size - 1) / 2);
	}

	return radii;
}

/*void box_blur_horizontal(data_view& src, data_view& dst, unsigned r) {

	const data_format* df_ptr = src.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	float iarr = 1.0f / (r + r + 1.0f);

	const cgv::type::uint8_type* src_ptr = src.get_ptr<cgv::type::uint8_type>();
	cgv::type::uint8_type* dst_ptr = dst.get_ptr<cgv::type::uint8_type>();

	for (unsigned y = 0; y < h; ++y) {
		unsigned ti = y * w;
		unsigned li = ti;
		unsigned ri = ti + r;

		float fv = static_cast<float>(src_ptr[ti]);
		float lv = static_cast<float>(src_ptr[ti + w - 1]);
		float val = (r + 1) * fv;

		for (int j = 0; j < r; ++j)
			val += static_cast<float>(src_ptr[ti + j]);

		for (int j = 0; j <= r; ++j) {
			val += static_cast<float>(src_ptr[ri]) - fv;
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			++ri;
			++ti;
		}

		for (int j = r + 1; j < w - r; ++j) {
			val += static_cast<float>(src_ptr[ri]) - static_cast<float>(src_ptr[li]);
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			++li;
			++ri;
			++ti;
		}

		for (int j = w - r; j < w; ++j) {
			val += lv - static_cast<float>(src_ptr[li]);
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			++li;
			++ti;
		}
	}
}

void box_blur_vertical(data_view& src, data_view& dst, unsigned r) {

	const data_format* df_ptr = src.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	float iarr = 1.0f / (r + r + 1.0f);

	const cgv::type::uint8_type* src_ptr = src.get_ptr<cgv::type::uint8_type>();
	cgv::type::uint8_type* dst_ptr = dst.get_ptr<cgv::type::uint8_type>();

	for (unsigned x = 0; x < w; ++x) {
		unsigned ti = x;
		unsigned li = ti;
		unsigned ri = ti + r * w;

		float fv = static_cast<float>(src_ptr[ti]);
		float lv = static_cast<float>(src_ptr[ti + h - 1]);
		float val = (r + 1) * fv;

		for (int j = 0; j < r; ++j)
			val += static_cast<float>(src_ptr[ti + j * w]);

		for (int j = 0; j <= r; ++j) {
			val += static_cast<float>(src_ptr[ri]) - fv;
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			ri += w;
			ti += w;
		}

		for (int j = r + 1; j < h - r; ++j) {
			val += static_cast<float>(src_ptr[ri]) - static_cast<float>(src_ptr[li]);
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			li += w;
			ri += w;
			ti += w;
		}

		for (int j = h - r; j < h; ++j) {
			val += lv - static_cast<float>(src_ptr[li]);
			dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));

			li += w;
			ti += w;
		}
	}
}

void viewer::fast_gaussian_blur(cgv::data::data_view& dv, unsigned radius) {

	if (radius == 0) return;

	std::vector<unsigned> box_radii = gauss_boxes(3u, radius);

	data_view temp(dv.get_format());

	box_blur_horizontal(dv, temp, box_radii[0]);
	box_blur_vertical(temp, dv, box_radii[0]);
	box_blur_horizontal(dv, temp, box_radii[1]);
	box_blur_vertical(temp, dv, box_radii[1]);
	box_blur_horizontal(dv, temp, box_radii[2]);
	box_blur_vertical(temp, dv, box_radii[2]);
}*/

void viewer::fast_gaussian_blur(cgv::data::data_view& dv, unsigned radius) {

	if(radius == 0) return;

	const auto& fmt = *dv.get_format();
	const auto& type_id = fmt.get_component_type();

	switch(type_id) {
	case cgv::type::info::TI_UINT8:  fast_gaussian_blur_impl<cgv::type::uint8_type,  cgv::type::int32_type>(dv, radius); break;
	case cgv::type::info::TI_UINT16: fast_gaussian_blur_impl<cgv::type::uint16_type, cgv::type::int32_type>(dv, radius); break;
	case cgv::type::info::TI_UINT32: fast_gaussian_blur_impl<cgv::type::uint32_type, cgv::type::flt32_type>(dv, radius); break;
	case cgv::type::info::TI_UINT64: fast_gaussian_blur_impl<cgv::type::uint64_type, cgv::type::flt32_type>(dv, radius); break;
	case cgv::type::info::TI_FLT32:  fast_gaussian_blur_impl<cgv::type::flt32_type,  cgv::type::flt32_type>(dv, radius); break;
	case cgv::type::info::TI_FLT64:  fast_gaussian_blur_impl<cgv::type::flt64_type,  cgv::type::flt64_type>(dv, radius); break;
	default: std::cout << "Warning: Could not find implementation of fast_gaussian_blur that matches the given component type." << std::endl; break;
	}
}

template<typename T>
inline T box_blur_normalize_value(T& v, T& norm, T& norm_half) {
	return v * norm;
}

inline cgv::type::int32_type box_blur_normalize_value(cgv::type::int32_type& v, cgv::type::int32_type& norm, cgv::type::int32_type& norm_half) {
	return (v + norm_half) / norm;
}

template<typename T>
inline void box_blur_get_normalization_factors(unsigned& r, T& norm, T& norm_half) {
	T _r = static_cast<T>(r);
	norm = (T)1 / (_r + _r + (T)1);
	norm_half = norm / (T)2;
}

inline void box_blur_get_normalization_factors(unsigned& r, cgv::type::int32_type& norm, cgv::type::int32_type& norm_half) {
	cgv::type::int32_type _r = static_cast<cgv::type::int32_type>(r);
	norm = _r + _r + (cgv::type::int32_type)1;
	norm_half = norm / (cgv::type::int32_type)2;
}

// TODO: don't forget to round when using floating point accumulation with integer source values
// dst_ptr[ti] = static_cast<unsigned char>(round(val * iarr));
template<typename S, typename T>
void box_blur_horizontal(const S* src_ptr, S* dst_ptr, unsigned w, unsigned h, unsigned r) {

	T norm, norm_half;
	box_blur_get_normalization_factors(r, norm, norm_half);

	for(unsigned y = 0; y < h; ++y) {
		unsigned ti = y * w;
		unsigned li = ti;
		unsigned ri = ti + r;

		T fv = static_cast<T>(src_ptr[ti]);
		T lv = static_cast<T>(src_ptr[ti + w - 1]);
		T val = static_cast<T>(r + 1) * fv;

		for(int j = 0; j < r; ++j)
			val += static_cast<T>(src_ptr[ti + j]);

		for(int j = 0; j <= r; ++j) {
			val += static_cast<T>(src_ptr[ri]) - fv;
			dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

			++ri;
			++ti;
		}

		for(int j = r + 1; j < w - r; ++j) {
			val += static_cast<T>(src_ptr[ri]);
			val -= static_cast<T>(src_ptr[li]);
			dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

			++li;
			++ri;
			++ti;
		}

		for(int j = w - r; j < w; ++j) {
			val += lv - static_cast<T>(src_ptr[li]);
			dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

			++li;
			++ti;
		}
	}
}

template<typename S, typename T>
void box_blur_vertical(const S* src_ptr, S* dst_ptr, unsigned w, unsigned h, unsigned r) {

		T norm, norm_half;
		box_blur_get_normalization_factors(r, norm, norm_half);

		for(unsigned x = 0; x < w; ++x) {
			unsigned ti = x;
			unsigned li = ti;
			unsigned ri = ti + r * w;

			T fv = static_cast<T>(src_ptr[ti]);
			T lv = static_cast<T>(src_ptr[ti + w * (h - 1)]);
			T val = static_cast<T>(r + 1) * fv;

			for(int j = 0; j < r; ++j)
				val += static_cast<T>(src_ptr[ti + j * w]);

			for(int j = 0; j <= r; ++j) {
				val += static_cast<T>(src_ptr[ri]) - fv;
				dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

				ri += w;
				ti += w;
			}

			for(int j = r + 1; j < h - r; ++j) {
				val += static_cast<T>(src_ptr[ri]);
				val -= static_cast<T>(src_ptr[li]);
				dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

				li += w;
				ri += w;
				ti += w;
			}

			for(int j = h - r; j < h; ++j) {
				val += lv - static_cast<T>(src_ptr[li]);
				dst_ptr[ti] = static_cast<S>(box_blur_normalize_value(val, norm, norm_half));

				li += w;
				ti += w;
			}
		}
	}

template<typename S, typename T>
void viewer::fast_gaussian_blur_impl(cgv::data::data_view& dv, unsigned radius) {

	std::vector<unsigned> box_radii = gauss_boxes(3u, radius);

	data_view temp(dv.get_format());

	const data_format* df_ptr = dv.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	S* src_ptr = dv.get_ptr<S>();
	S* dst_ptr = temp.get_ptr<S>();

	box_blur_horizontal<S, T>(src_ptr, dst_ptr, w, h, box_radii[0]);
	box_blur_vertical<S, T>(dst_ptr, src_ptr, w, h, box_radii[0]);
	box_blur_horizontal<S, T>(src_ptr, dst_ptr, w, h, box_radii[1]);
	box_blur_vertical<S, T>(dst_ptr, src_ptr, w, h, box_radii[1]);
	box_blur_horizontal<S, T>(src_ptr, dst_ptr, w, h, box_radii[2]);
	box_blur_vertical<S, T>(dst_ptr, src_ptr, w, h, box_radii[2]);
}

void box_blur_horizontalu(data_view& src, data_view& dst, unsigned r) {

	const data_format* df_ptr = src.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	unsigned div = r + r + 1;
	unsigned div_half = div / 2;

	const cgv::type::uint8_type* src_ptr = src.get_ptr<cgv::type::uint8_type>();
	cgv::type::uint8_type* dst_ptr = dst.get_ptr<cgv::type::uint8_type>();

	for (unsigned y = 0; y < h; ++y) {
		unsigned ti = y * w;
		unsigned li = ti;
		unsigned ri = ti + r;

		int fv = src_ptr[ti];
		int lv = src_ptr[ti + w - 1];
		int val = (r + 1) * fv;

		for (int j = 0; j < r; ++j)
			val += src_ptr[ti + j];

		for (int j = 0; j <= r; ++j) {
			val += src_ptr[ri] - fv;
			dst_ptr[ti] = (val + div_half) / div;

			++ri;
			++ti;
		}

		for (int j = r + 1; j < w - r; ++j) {
			val += src_ptr[ri];
			val -= src_ptr[li];
			dst_ptr[ti] = (val + div_half) / div;

			++li;
			++ri;
			++ti;
		}

		for (int j = w - r; j < w; ++j) {
			val += lv - src_ptr[li];
			dst_ptr[ti] = (val + div_half) / div;

			++li;
			++ti;
		}
	}
}

void box_blur_verticalu(data_view& src, data_view& dst, unsigned r) {

	const data_format* df_ptr = src.get_format();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	unsigned div = r + r + 1;
	unsigned div_half = div / 2;

	const cgv::type::uint8_type* src_ptr = src.get_ptr<cgv::type::uint8_type>();
	cgv::type::uint8_type* dst_ptr = dst.get_ptr<cgv::type::uint8_type>();

	for (unsigned x = 0; x < w; ++x) {
		unsigned ti = x;
		unsigned li = ti;
		unsigned ri = ti + r * w;

		int fv = src_ptr[ti];
		int lv = src_ptr[ti + w*(h - 1)];
		int val = (r + 1) * fv;

		for (int j = 0; j < r; ++j)
			val += src_ptr[ti + j * w];

		for (int j = 0; j <= r; ++j) {
			val += src_ptr[ri] - fv;
			dst_ptr[ti] = (val + div_half) / div;

			ri += w;
			ti += w;
		}

		for (int j = r + 1; j < h - r; ++j) {
			val += src_ptr[ri];
			val -= src_ptr[li];
			dst_ptr[ti] = (val + div_half) / div;

			li += w;
			ri += w;
			ti += w;
		}

		for (int j = h - r; j < h; ++j) {
			val += lv - src_ptr[li];
			dst_ptr[ti] = (val + div_half) / div;

			li += w;
			ti += w;
		}
	}
}

void viewer::fast_gaussian_bluru(cgv::data::data_view& dv, unsigned radius) {

	if (radius == 0) return;

	std::vector<unsigned> box_radii = gauss_boxes(3u, radius);

	data_view temp(dv.get_format());

	box_blur_horizontalu(dv, temp, box_radii[0]);
	box_blur_verticalu(temp, dv, box_radii[0]);
	box_blur_horizontalu(dv, temp, box_radii[1]);
	box_blur_verticalu(temp, dv, box_radii[1]);
	box_blur_horizontalu(dv, temp, box_radii[2]);
	box_blur_verticalu(temp, dv, box_radii[2]);
}

void viewer::multiply(cgv::data::data_view& dv, float a) {

	const data_format* src_df_ptr = dv.get_format();
	unsigned n_dims = src_df_ptr->get_nr_dimensions();

	unsigned w = src_df_ptr->get_width();
	unsigned h = src_df_ptr->get_height();

	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) {
			unsigned char val = dv.get<unsigned char>(0, x, y);

			float fval = a * static_cast<float>(val) / 255.0f;
			fval = cgv::math::clamp(fval, 0.0f, 1.0f);

			dv.set(y + w * x, static_cast<unsigned char>(fval * 255.0f));
		}
	}
}

std::vector<unsigned> viewer::calculate_histogram(cgv::data::data_view& dv) {

	std::vector<unsigned> histogram(histograms.num_bins, 0);

	if(histograms.num_bins == 0)
		return histogram;

	const auto& type_id = dv.get_format()->get_component_type();
	
	switch(type_id) {
	case cgv::type::info::TI_UINT8: calculate_histogram_impl<cgv::type::uint8_type>(dv, histogram, 255); break;
	case cgv::type::info::TI_UINT16: calculate_histogram_impl<cgv::type::uint16_type>(dv, histogram, 65535); break;
	default: std::cout << "Warning: Could not find implementation of histogram that matches the given component type." << std::endl; break;
	}

	return histogram;
}

template<typename T>
void viewer::calculate_histogram_impl(cgv::data::data_view& dv, std::vector<unsigned>& histogram, unsigned max_value) {
	
	const T* data_ptr = dv.get_ptr<T>();

	const data_format* df_ptr = dv.get_format();
	unsigned n_dims = df_ptr->get_nr_dimensions();
	unsigned w = df_ptr->get_width();
	unsigned h = df_ptr->get_height();

	h = h == 0 ? 1 : h;

	unsigned n = w * h;

	float normalize_factor = 1.0f / static_cast<float>(max_value);
	unsigned max_bin = static_cast<unsigned>(histogram.size()) - 1;

	for(unsigned i = 0; i < n; ++i) {
		float val = static_cast<float>(data_ptr[i]) * normalize_factor;
		unsigned index = cgv::math::clamp(static_cast<unsigned>(val * max_bin), 0u, max_bin);
		++histogram[index];
	}
}

// fills the spheres for the sallimus dots
void viewer::create_sallimus_dots_geometry() {

	auto& segments = dataset.sarcomere_segments;
	// clear previous data
	sallimus_dots_rd.clear();

	for (unsigned i = 0; i < segments.size(); ++i) {
		sallimus_dots_rd.add(segments[i].a);
		sallimus_dots_rd.add(segments[i].b);
	}

	// notify the render data that we changed geometry
	sallimus_dots_rd.set_out_of_date();
}

// fills the cylinders for the sarcomeres
// call this method if you changed the selected cluster and want to set sarcomere colors
// this method should only be called once whenever your cluster selection changes, not on every frame
void viewer::create_sarcomeres_geometry() {

	auto& segments = dataset.sarcomere_segments;
	// clear previous data
	sarcomeres_rd.clear();

	for (unsigned i = 0; i < segments.size(); ++i) {
		// add start and end positions
		sarcomeres_rd.add(segments[i].a, segments[i].b);
		// add color for both positions (use color from style)
		// we could also omit the color all together
		sarcomeres_rd.add(sarcomere_style.surface_color);
	}

	// notify the render data that we changed geometry
	sarcomeres_rd.set_out_of_date();
}

// method for creating spheres and cones that show the sarcomere segments
void viewer::create_segment_render_data() {

	create_sallimus_dots_geometry();
	create_sarcomeres_geometry();
}

#include "lib_begin.h"

#include <cgv/base/register.h>
extern cgv::base::object_registration<viewer> viewer_reg("viewer");
//cgv::base::registration_order_definition ro_def("stereo_view_interactor;viewer;transfer_function_editor");
cgv::base::registration_order_definition ro_def("stereo_view_interactor;viewer");
#ifdef CGV_FORCE_STATIC
#include <myofibrils_shader_inc.h>
#endif

