/** BEGIN - MFLEURY **/

#include "tf_editor_lines.h"

#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

#include "tf_editor_shared_functions.h"

tf_editor_lines::tf_editor_lines() {
	set_name("TF Editor Lines Overlay");
	// set the size with an aspect ratio that makes lets the editor nicely fit inside
	// aspect ratio is w:h = 1:0.875
	set_overlay_size(ivec2(600, 525));
	// Register an additional arrow shader
	content_canvas.register_shader("arrow", cgv::glutil::canvas::shaders_2d::arrow);

	// initialize the renderers
	m_renderer_lines = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_renderer_strips = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::polygon);

	// callbacks for the moving of draggables
	m_point_handles.set_drag_callback(std::bind(&tf_editor_lines::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_lines::end_drag, this));
}

void tf_editor_lines::clear(cgv::render::context& ctx) {
	m_renderer_lines.destruct(ctx);
	m_geometry_relations.destruct(ctx);
	m_geometry_widgets.destruct(ctx);
	m_geometry_strip_borders.destruct(ctx);

	cgv::glutil::ref_msdf_font(ctx, -1);
	cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx, -1);

	m_renderer_strips.destruct(ctx);

	m_renderer_draggables.destruct(ctx);
	m_geometry_draggables.destruct(ctx);
	m_geometry_draggables_interacted.destruct(ctx);


	glDeleteBuffers(1, &plot_buffer);
	plot_texture.destruct(ctx);
}

bool tf_editor_lines::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if(et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;

		if (ke.get_action() == cgv::gui::KA_PRESS) {
			if (ke.get_key() == cgv::gui::KEY_Space && is_hit((ke.get_x(), ke.get_y()))) {
				is_peak_mode = !is_peak_mode;
				// User might have adjusted many values while peak mode was active, so recreate everything
				if (!is_peak_mode) {
					m_create_all_values = true;
				}

				redraw();
				return true;
			}
			else {
				return tf_editor_basic::handle_key_input(ke.get_key(), m_interacted_primitive_ids[0]);
			}
		}
	} else if(et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;

		const auto mouse_pos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));

		// Reset dragging by clicking the left mouse
		if (me.get_action() == cgv::gui::MA_PRESS && me.get_button() == cgv::gui::MB_LEFT_BUTTON && !m_currently_dragging) {
			point_clicked(mouse_pos);
		}
		// Set width if a scroll is done
		else if(me.get_action() == cgv::gui::MA_WHEEL && is_interacting) {
			const auto negative_change = me.get_dy() > 0 ? true : false;
			const auto shift_pressed = e.get_modifiers() & cgv::gui::EM_SHIFT ? true : false;

			scroll_centroid_width(mouse_pos.x(), mouse_pos.y(), negative_change, shift_pressed);
		}

		auto handled = false;
		handled |= m_point_handles.handle(e, last_viewport_size, container);

		if(handled)
			post_redraw();

		return handled;
	}

	return false;
}

void tf_editor_lines::on_set(void* member_ptr) {
	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &m_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}
	// Update if the vis mode is changed
	if(member_ptr == &vis_mode || member_ptr == &m_threshold) {
		update_content();
	}

	update_member(member_ptr);
	redraw();
}

bool tf_editor_lines::init(cgv::render::context& ctx) {

	auto success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= fbc_plot.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_renderer_lines.init(ctx);
	success &= m_renderer_draggables.init(ctx);
	success &= m_renderer_strips.init(ctx);

	auto& font = cgv::glutil::ref_msdf_font(ctx, 1);
	cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx, 1);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if(font.is_initialized()) {
		m_labels.set_msdf_font(&font);
		m_labels.set_font_size(m_font_size);
	}










	shaders.add("hist", "lp_hist");
	shaders.add("transfer", "lp_ssbo_to_tex2d");
	shaders.add("clear", "lp_ssbo_clear");

	shaders.add("hist2", "lp_hist2");
	shaders.add("transfer2", "lp_ssbo_to_tex2d2");
	shaders.add("clear2", "lp_ssbo_clear2");

	shaders.add("plot_line", "plot2d_line.glpr");
	shaders.add("plot_line2", "plot2d_line2.glpr");
	success &= shaders.load_shaders(ctx, "tf_editor_lines::init");

	glGenBuffers(1, &plot_buffer);

	



	plot_resolution = ivec2(128, 128);

	/*
	std::vector<float> data(4 * 256 * 256, 1.0f);

	for(size_t i = 0; i < 256*256; ++i) {
		float mul = static_cast<float>(i / 256) + 1.0f;
		float val = static_cast<float>(i % 256) * 0.2f*mul;
		data[4 * i + 0] = val;
		data[4 * i + 1] = val;
		data[4 * i + 2] = val;
		data[4 * i + 3] = val;
	}

	cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(256, 256, TI_FLT32, cgv::data::CF_RGBA), data.data());
	*/

	glGenBuffers(6, plot_buffers2);

	for(size_t i = 0; i < 6; ++i) {
		plot_textures[i] = cgv::render::texture("flt32[R,G,B,A]", cgv::render::TF_LINEAR, cgv::render::TF_LINEAR);
		plot_textures[i].create(ctx, TT_2D, plot_resolution.x(), plot_resolution.y());

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, plot_buffers2[i]);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * sizeof(float) * plot_resolution.x() * plot_resolution.y(), (void*)0, GL_DYNAMIC_COPY);
	}

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);







	unsigned test_n = 10000;

	std::vector<int> test_values(test_n);
	for(size_t i = 0; i < test_n; ++i)
		test_values[i] = i;

	GLuint test_buffer, out_buffer;
	glGenBuffers(1, &test_buffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, test_buffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int) * test_n, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &out_buffer);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, out_buffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(int) * test_n, (void*)0, GL_DYNAMIC_COPY);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


	sac.set_data_type_override("int x");

	sac.init(ctx, test_n);

	sac.execute(ctx, test_buffer, out_buffer);





	return success;
}

void tf_editor_lines::init_frame(cgv::render::context& ctx) {

	// react to changes in the overlay size
	if(ensure_overlay_layout(ctx)) {
		ivec2 overlay_size = get_overlay_size();

		// calculate the new domain size
		domain.set_pos(ivec2(13)); // 13 pixel padding (inner space from border)
		domain.set_size(overlay_size - 2 * ivec2(13)); // scale size to fit in inner space left over by padding

		// udpate the offline frame buffer to the new size
		fbc.set_size(overlay_size);
		fbc.ensure(ctx);

		// udpate the offline plot frame buffer to the domain size
		fbc_plot.set_size(overlay_size);
		fbc_plot.ensure(ctx);

		// set resolutions of the canvases
		content_canvas.set_resolution(ctx, overlay_size);
		viewport_canvas.set_resolution(ctx, get_viewport_size());

		create_widget_lines();
		add_widget_lines();

		m_point_handles.set_constraint(domain);

		create_labels();

		update_point_positions();

		// update the quad positions after a resize
		m_strips_created = false;
		m_create_all_values = true;

		has_damage = true;
		m_reset_plot = true;









		std::vector<float> data(overlay_size.x() * overlay_size.y() * 4, 1.0f);
		cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(overlay_size.x(), overlay_size.y(), TI_FLT32, cgv::data::CF_RGBA), data.data());

		if(plot_texture.is_created())
			plot_texture.destruct(ctx);
		plot_texture = cgv::render::texture("flt32[R,G,B,A]", cgv::render::TF_LINEAR, cgv::render::TF_LINEAR);
		plot_texture.create(ctx, dv, 0);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, plot_buffer);
		glBufferData(GL_SHADER_STORAGE_BUFFER, 4 * sizeof(float)*overlay_size.x()*overlay_size.y(), (void*)0, GL_DYNAMIC_COPY);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);


	}
}

void tf_editor_lines::create_gui() {

	create_overlay_gui();

	tf_editor_basic::create_gui_basic();
	tf_editor_basic::create_gui_coloring();
	tf_editor_basic::create_gui_tm();

	add_member_control(this, "Render Mode", m_mode, "dropdown", "enums='Compute Shader, Vertex Shader, Geometry Shader, Compute 2'");
}

// Called if something in the primitives has been updated
void tf_editor_lines::resynchronize() {
	// Clear and readd points
	m_points.clear();
	for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
		add_draggables(i);
	}

	set_point_handles();
	m_create_all_values = true;
	// Then redraw strips etc
	redraw();
}

void tf_editor_lines::primitive_added() {
	add_draggables(m_shared_data_ptr->primitives.size() - 1);
	// Add a corresponding draggable point for every centroid
	set_point_handles();

	// A new primitive was added, so we need to redraw completely
	m_create_all_values = true;

	// Synchronize everything else and redraw
	m_shared_data_ptr->set_synchronized();
	redraw();
}

void tf_editor_lines::init_styles(cgv::render::context& ctx) {

	// configure style for rendering the plot framebuffer texture
	m_style_plot.use_blending = true;
	m_style_plot.use_texture = true;
	m_style_plot.feather_width = 0.0f;

	m_style_relations.use_blending = true;
	m_style_relations.use_fill_color = false;
	m_style_relations.width = 1.0f;

	m_style_widgets.use_blending = true;
	m_style_widgets.use_fill_color = true;
	m_style_widgets.fill_color = m_gray_widgets;
	m_style_widgets.width = 1.0f;

	m_style_polygons.use_blending = true;
	m_style_polygons.use_fill_color = false;
	m_style_polygons.fill_color = rgba(1.0f, 0.0f, 0.0f, m_alpha);

	m_style_strip_borders.use_blending = true;
	m_style_strip_borders.use_fill_color = false;
	m_style_strip_borders.border_width = 1.5f;

	// TODO: the polygon does not use a line style
	auto& line_prog = m_renderer_strips.ref_prog();
	line_prog.enable(ctx);
	m_style_polygons.apply(ctx, line_prog);
	line_prog.disable(ctx);

	m_style_draggables.position_is_center = true;
	m_style_draggables.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables.border_width = 1.5f;
	m_style_draggables.use_blending = true;

	m_style_draggables_interacted.position_is_center = true;
	m_style_draggables_interacted.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables_interacted.border_width = 1.5f;
	m_style_draggables_interacted.use_blending = true;

	m_style_arrows.head_width = 10.0f;
	m_style_arrows.relative_head_length = 1.0f;
	m_style_arrows.head_length_is_relative = true;
	m_style_arrows.stem_width = 0.0f;
	m_style_arrows.feather_width = 1.0f;
	m_style_arrows.fill_color = m_gray_arrows;
	m_style_arrows.border_color = m_gray_arrows;
	m_style_arrows.use_fill_color = true;
	m_style_arrows.use_blending = true;

	cgv::glutil::shape2d_style plot_rect_style;
	plot_rect_style.use_texture = true;
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	plot_rect_style.apply(ctx, rectangle_prog);
	content_canvas.disable_current_shader(ctx);

	//cgv::glutil::shape2d_style text_style;
	m_style_text.fill_color = rgba(rgb(0.0f), 1.0f);
	m_style_text.border_color.alpha() = 0.0f;
	m_style_text.border_width = 0.333f;
	m_style_text.use_blending = true;

	// configure style for final blending of whole overlay
	overlay_style.fill_color = rgba(1.0f);
	overlay_style.use_texture = true;
	overlay_style.use_blending = false;
	overlay_style.border_color = rgba(rgb(0.5f), 1.0f);
	overlay_style.border_width = 3.0f;
	overlay_style.feather_width = 0.0f;

	// also does not change, so is only set whenever this method is called
	auto& overlay_prog = viewport_canvas.enable_shader(ctx, "rectangle");
	overlay_style.apply(ctx, overlay_prog);
	viewport_canvas.disable_current_shader(ctx);
}

void tf_editor_lines::update_content() {

	if(!m_data_set_ptr || m_data_set_ptr->voxel_data.empty())
		return;



	auto ctx_ptr = get_context();
	if(!ctx_ptr)
		return;
	auto& ctx = *ctx_ptr;















	GLuint time_query = 0;
	glGenQueries(1, &time_query);
	
	glBeginQuery(GL_TIME_ELAPSED, time_query);

	if(m_mode == M_COMPUTE) {
		// setup group and plot size
		const unsigned group_size = 128;
		const ivec2 resolution(
			plot_texture.get_width(),
			plot_texture.get_height()
		);

		// calculate the necessary number of work groups
		unsigned num_groups_image_order = (resolution.x() * resolution.y() + group_size - 1) / group_size;

		unsigned width = m_data_set_ptr->volume_tex.get_resolution(0);
		unsigned height = m_data_set_ptr->volume_tex.get_resolution(1);
		unsigned depth = m_data_set_ptr->volume_tex.get_resolution(2);

		unsigned n = width * height * depth; // number of data points
		unsigned num_groups_data_order = (n + group_size - 1) / group_size;

		// bind shader storage buffer
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, plot_buffer);

		// clear the previous contents
		auto& clear_prog = shaders.get("clear");
		clear_prog.enable(ctx);
		clear_prog.set_uniform(ctx, "resolution", resolution);

		glDispatchCompute(num_groups_image_order, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		clear_prog.disable(ctx);

		// plot the data lines to the plot
		auto& plot_prog = shaders.get("hist");
		plot_prog.enable(ctx);

		// set general uniforms
		plot_prog.set_uniform(ctx, "num_data_values", static_cast<int>(n));
		plot_prog.set_uniform(ctx, "resolution", resolution);
		plot_prog.set_uniform(ctx, "threshold", m_threshold);

		auto set_relation_uniforms = [&](unsigned idx, unsigned li0, unsigned li1, unsigned i0, unsigned i1) {
			const auto& l0 = m_widget_lines[li0];
			const auto& l1 = m_widget_lines[li1];

			vec4 line_a(l0.a.x(), l0.a.y(), l0.b.x(), l0.b.y());
			vec4 line_b(l1.a.x(), l1.a.y(), l1.b.x(), l1.b.y());

			std::string name = "relations[" + std::to_string(idx) + "].";

			plot_prog.set_uniform(ctx, name + "indices", ivec2(i0, i1));
			plot_prog.set_uniform(ctx, name + "line_a", line_a);
			plot_prog.set_uniform(ctx, name + "line_b", line_b);
		};

		set_relation_uniforms(0, 0, 6, 0, 1);
		set_relation_uniforms(1, 1, 12, 0, 3);
		set_relation_uniforms(2, 2, 8, 0, 2);
		set_relation_uniforms(3, 4, 10, 1, 2);
		set_relation_uniforms(4, 5, 13, 1, 3);
		set_relation_uniforms(5, 9, 14, 2, 3);

		// set transfer function uniforms
		const int mdtf_size = static_cast<int>(m_shared_data_ptr->primitives.size());
		plot_prog.set_uniform(ctx, "num_mdtf_primitives", mdtf_size);

		for(int i = 0; i < mdtf_size; i++) {
			const auto& p = m_shared_data_ptr->primitives[i];

			std::string id = "mdtf[" + std::to_string(i) + "].";

			plot_prog.set_uniform(ctx, id + "type", static_cast<int>(p.type));
			plot_prog.set_uniform(ctx, id + "cntrd", p.centr_pos);
			plot_prog.set_uniform(ctx, id + "width", p.centr_widths);
			plot_prog.set_uniform(ctx, id + "color", p.color);
		}

		// bind the source 3D texture containing the data values
		const int src_texture_handle = (const int&)(m_data_set_ptr->volume_tex.handle) - 1;
		glBindImageTexture(0, src_texture_handle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);

		glDispatchCompute(num_groups_data_order, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		// unbind the source data texture
		glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);

		plot_prog.disable(ctx);

		// transfer the shader storage buffer contents to the plot texture
		auto& transfer_prog = shaders.get("transfer");
		transfer_prog.enable(ctx);
		transfer_prog.set_uniform(ctx, "resolution", resolution);

		// bind the plot texture
		const int handle = (const int&)(plot_texture.handle) - 1;
		glBindImageTexture(0, handle, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

		glDispatchCompute(num_groups_image_order, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// unbind texture and buffer
		glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);

		transfer_prog.disable(ctx);
	} else if(m_mode == M_VERTEX) {
		fbc_plot.enable(ctx);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		const ivec2 resolution(
			fbc_plot.ref_frame_buffer().get_width(),
			fbc_plot.ref_frame_buffer().get_height()
		);

		unsigned width = m_data_set_ptr->volume_tex.get_resolution(0);
		unsigned height = m_data_set_ptr->volume_tex.get_resolution(1);
		unsigned depth = m_data_set_ptr->volume_tex.get_resolution(2);

		unsigned n = width * height * depth; // number of data points

		auto& plot_prog = shaders.get("plot_line");
		plot_prog.enable(ctx);

		plot_prog.set_uniform(ctx, "resolution", resolution);
		plot_prog.set_uniform(ctx, "threshold", m_threshold);

		auto set_relation_uniforms = [&](unsigned idx, unsigned li0, unsigned li1, unsigned i0, unsigned i1) {
			const auto& l0 = m_widget_lines[li0];
			const auto& l1 = m_widget_lines[li1];

			vec4 line_a(l0.a.x(), l0.a.y(), l0.b.x(), l0.b.y());
			vec4 line_b(l1.a.x(), l1.a.y(), l1.b.x(), l1.b.y());

			std::string name = "relations[" + std::to_string(idx) + "].";

			plot_prog.set_uniform(ctx, name + "indices", ivec2(i0, i1));
			plot_prog.set_uniform(ctx, name + "line_a", line_a);
			plot_prog.set_uniform(ctx, name + "line_b", line_b);
		};

		set_relation_uniforms(0, 0, 6, 0, 1);
		set_relation_uniforms(1, 1, 12, 0, 3);
		set_relation_uniforms(2, 2, 8, 0, 2);
		set_relation_uniforms(3, 4, 10, 1, 2);
		set_relation_uniforms(4, 5, 13, 1, 3);
		set_relation_uniforms(5, 9, 14, 2, 3);

		m_data_set_ptr->volume_tex.enable(ctx, 0);
		glDrawArrays(GL_LINES, 0, 2 * 6 * n);
		m_data_set_ptr->volume_tex.disable(ctx);

		plot_prog.disable(ctx);

		// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
		fbc_plot.disable(ctx);

		// reset blending and depth test
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	} else if(m_mode == M_GEOM) {
		fbc_plot.enable(ctx);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		const ivec2 resolution(
			fbc_plot.ref_frame_buffer().get_width(),
			fbc_plot.ref_frame_buffer().get_height()
		);

		unsigned width = m_data_set_ptr->volume_tex.get_resolution(0);
		unsigned height = m_data_set_ptr->volume_tex.get_resolution(1);
		unsigned depth = m_data_set_ptr->volume_tex.get_resolution(2);

		unsigned n = width * height * depth; // number of data points

		auto& plot_prog = shaders.get("plot_line2");
		plot_prog.enable(ctx);

		plot_prog.set_uniform(ctx, "resolution", resolution);
		plot_prog.set_uniform(ctx, "threshold", m_threshold);

		auto set_relation_uniforms = [&](unsigned idx, unsigned li0, unsigned li1, unsigned i0, unsigned i1) {
			const auto& l0 = m_widget_lines[li0];
			const auto& l1 = m_widget_lines[li1];

			vec4 line_a(l0.a.x(), l0.a.y(), l0.b.x(), l0.b.y());
			vec4 line_b(l1.a.x(), l1.a.y(), l1.b.x(), l1.b.y());

			std::string name = "relations[" + std::to_string(idx) + "].";

			plot_prog.set_uniform(ctx, name + "indices", ivec2(i0, i1));
			plot_prog.set_uniform(ctx, name + "line_a", line_a);
			plot_prog.set_uniform(ctx, name + "line_b", line_b);
		};

		set_relation_uniforms(0, 0, 6, 0, 1);
		set_relation_uniforms(1, 1, 12, 0, 3);
		set_relation_uniforms(2, 2, 8, 0, 2);
		set_relation_uniforms(3, 4, 10, 1, 2);
		set_relation_uniforms(4, 5, 13, 1, 3);
		set_relation_uniforms(5, 9, 14, 2, 3);

		m_data_set_ptr->volume_tex.enable(ctx, 0);
		glDrawArrays(GL_POINTS, 0, n);
		m_data_set_ptr->volume_tex.disable(ctx);

		plot_prog.disable(ctx);

		// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
		fbc_plot.disable(ctx);

		// reset blending and depth test
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	} else {
		// setup group and plot size
		const unsigned group_size = 128;
		
		// calculate the necessary number of work groups
		unsigned num_groups_image_order = (plot_resolution.x() * plot_resolution.y() + group_size - 1) / group_size;

		unsigned width = m_data_set_ptr->volume_tex.get_resolution(0);
		unsigned height = m_data_set_ptr->volume_tex.get_resolution(1);
		unsigned depth = m_data_set_ptr->volume_tex.get_resolution(2);

		unsigned n = width * height * depth; // number of data points
		unsigned num_groups_data_order = (n + group_size - 1) / group_size;

		// bind shader storage buffers
		for(size_t i = 0; i < 6; ++i)
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, plot_buffers2[i]);

		// clear the previous contents
		auto& clear_prog = shaders.get("clear2");
		clear_prog.enable(ctx);
		clear_prog.set_uniform(ctx, "resolution", plot_resolution);

		glDispatchCompute(num_groups_image_order, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		clear_prog.disable(ctx);

		// plot the data lines to the plot
		auto& plot_prog = shaders.get("hist2");
		plot_prog.enable(ctx);

		// set general uniforms
		plot_prog.set_uniform(ctx, "num_data_values", static_cast<int>(n));
		plot_prog.set_uniform(ctx, "resolution", plot_resolution);
		plot_prog.set_uniform(ctx, "threshold", m_threshold);

		plot_prog.set_uniform(ctx, "indices[0]", ivec2(1, 0));
		plot_prog.set_uniform(ctx, "indices[1]", ivec2(0, 3));
		plot_prog.set_uniform(ctx, "indices[2]", ivec2(0, 2));
		plot_prog.set_uniform(ctx, "indices[3]", ivec2(1, 2));
		plot_prog.set_uniform(ctx, "indices[4]", ivec2(1, 3));
		plot_prog.set_uniform(ctx, "indices[5]", ivec2(3, 2));
		
		// set transfer function uniforms
		const int mdtf_size = static_cast<int>(m_shared_data_ptr->primitives.size());
		plot_prog.set_uniform(ctx, "num_mdtf_primitives", mdtf_size);

		for(int i = 0; i < mdtf_size; i++) {
			const auto& p = m_shared_data_ptr->primitives[i];

			std::string id = "mdtf[" + std::to_string(i) + "].";

			plot_prog.set_uniform(ctx, id + "type", static_cast<int>(p.type));
			plot_prog.set_uniform(ctx, id + "cntrd", p.centr_pos);
			plot_prog.set_uniform(ctx, id + "width", p.centr_widths);
			plot_prog.set_uniform(ctx, id + "color", p.color);
		}

		// bind the source 3D texture containing the data values
		const int src_texture_handle = (const int&)(m_data_set_ptr->volume_tex.handle) - 1;
		glBindImageTexture(0, src_texture_handle, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);

		glDispatchCompute(num_groups_data_order, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		// unbind the source data texture
		glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA8UI);

		plot_prog.disable(ctx);

		// transfer the shader storage buffer contents to the plot texture
		auto& transfer_prog = shaders.get("transfer2");
		transfer_prog.enable(ctx);
		transfer_prog.set_uniform(ctx, "resolution", plot_resolution);

		// bind the plot textures
		for(unsigned i = 0; i < 6; ++i) {
			const int handle = (const int&)(plot_textures[i].handle) - 1;
			glBindImageTexture(i, handle, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		}

		glDispatchCompute(num_groups_image_order, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// unbind textures and buffers
		for(size_t i = 0; i < 6; ++i) {
			glBindImageTexture(i, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, 0);
		}

		transfer_prog.disable(ctx);
	}

	glEndQuery(GL_TIME_ELAPSED);

	GLint done = false;
	while(!done) {
		glGetQueryObjectiv(time_query, GL_QUERY_RESULT_AVAILABLE, &done);
	}
	GLuint64 elapsed_time = 0;
	glGetQueryObjectui64v(time_query, GL_QUERY_RESULT, &elapsed_time);

	double time = static_cast<double>(elapsed_time) / 1000000.0;
	std::cout << "TIME: " << time << " ms" << std::endl;
































	/*
	const auto& data = m_data_set_ptr->voxel_data;
	// Everythin needs to be updated, also nothing is dragged the moment the button is pressed
	m_create_all_values = true;
	m_is_point_dragged = false;

	// reset previous total count and line geometry
	m_total_count = 0;
	m_geometry_relations.clear();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		const auto& v = data[i];

		// calculate the average to allow filtering with the given threshold
		auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		if(avg > m_threshold) {
			rgb color_rgb(0.0f);
			if(vis_mode == VM_GTF) {
				color_rgb = tf_editor_shared_functions::get_color(v, m_shared_data_ptr->primitives);
			}
			// Use full alpha for enabled tone mapping
			rgba col(color_rgb, use_tone_mapping ? 1.0f : m_alpha);

			// Left to right
			m_geometry_relations.add(m_widget_lines.at(0).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(6).interpolate(v[1]), col);
			// Left to center
			m_geometry_relations.add(m_widget_lines.at(1).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(12).interpolate(v[3]), col);
			// Left to bottom
			m_geometry_relations.add(m_widget_lines.at(2).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(8).interpolate(v[2]), col);
			// Right to bottom
			m_geometry_relations.add(m_widget_lines.at(4).interpolate(v[1]), col);
			m_geometry_relations.add(m_widget_lines.at(10).interpolate(v[2]), col);
			// Right to center
			m_geometry_relations.add(m_widget_lines.at(5).interpolate(v[1]), col);
			m_geometry_relations.add(m_widget_lines.at(13).interpolate(v[3]), col);
			// Bottom to center
			m_geometry_relations.add(m_widget_lines.at(9).interpolate(v[2]), col);
			m_geometry_relations.add(m_widget_lines.at(14).interpolate(v[3]), col);
		}
	}
	*/
	// content was updated, so redraw
	redraw();
}

void tf_editor_lines::create_labels() {

	m_labels.clear();

	// Set the font texts
	if(auto ctx_ptr = get_context()) {
		auto& font = cgv::glutil::ref_msdf_font(*ctx_ptr);
		if(font.is_initialized() && m_widget_polygons.size() > 3) {
			vec2 centers[4];
			for(int i = 0; i < 4; i++)
				centers[i] = m_widget_polygons[i].get_center();

			m_labels.add_text("0", ivec2(centers[0]), cgv::render::TA_NONE, 60.0f);
			m_labels.add_text("1", ivec2(centers[1]), cgv::render::TA_NONE, -60.0f);
			m_labels.add_text("2", ivec2(centers[2]), cgv::render::TA_NONE, 0.0);
			m_labels.add_text("3", ivec2(centers[3]), cgv::render::TA_NONE, 0.0f);

			for(int i = 0; i < 4; i++) {
				m_labels.set_text(i, m_data_set_ptr->stain_names[i]);
			}
		}
	}
}

void tf_editor_lines::create_widget_lines() {
	m_widget_lines.clear();
	m_widget_polygons.clear();

	// Sizing constants for the widgets
	const float a = 1.0f; // Distance from origin (center of central triangle) to its corners
	const float b = 1.0f; // Orthogonal distance from center widget line to the outer widget line
	const float c = 2.0f * sin(cgv::math::deg2rad(60.0f)) * b; // distance between two opposing outer widget lines

	// Constant rotation matrices
	const mat2 R = cgv::math::rotate2(120.0f);
	const mat2 R2 = cgv::math::rotate2(240.0f);
	const mat2 R_half = cgv::math::rotate2(60.0f);
	// Help vector
	const vec2 up(0.0f, 1.0f);

	// Final points
	std::vector<vec2> center(3), left(4), bottom(4), right(4);

	// Create center widget points
	center[0] = a * up;
	center[1] = R * center[0];
	center[2] = R2 * center[0];

	// Calculate help directions
	vec2 b_dir = normalize(R_half * up);
	vec2 c_dir = normalize(center[1]);

	// Create left outer widget corners
	left[1] = center[0] + b * b_dir;
	left[2] = center[1] + b * b_dir;
	left[0] = left[1] + c * up;
	left[3] = left[2] + c * c_dir;

	// Rotate corners of left widget to get the other outer widget points
	for(size_t i = 0; i < 4; ++i) {
		bottom[i] = R * left[i];
		right[i] = R2 * left[i];
	}

	// Calculate the bounding box of all the corners
	box2 box;

	for(size_t i = 0; i < 3; ++i)
		box.add_point(center[i]);

	for(size_t i = 0; i < 4; ++i) {
		box.add_point(left[i]);
		box.add_point(bottom[i]);
		box.add_point(right[i]);
	}

	// Offset applied before scaling (in unit coordinates) to move the widget center of gravity to the origin
	vec2 center_offset = -box.get_center();

	// Offset applied after scaling (in pixel coordinates) to move the widgets to the center of the domain
	vec2 offset = domain.box.get_center();

	// Calculate a uniform scaling factor to fit the widget bounding box inside the domain
	vec2 ext = box.get_extent();
	// Factor to scale from unit to pixel coordinates
	float scale = std::min(domain.size().x() / ext.x(), domain.size().y() / ext.y());

	// Apply offsets and scale
	for(size_t i = 0; i < 3; ++i) {
		center[i] = scale * (center[i] + center_offset) + offset;
		center[i] = cgv::math::round(center[i]) + 0.5f;
	}

	for(size_t i = 0; i < 4; ++i) {
		left[i] = scale * (left[i] + center_offset) + offset;
		bottom[i] = scale * (bottom[i] + center_offset) + offset;
		right[i] = scale * (right[i] + center_offset) + offset;

		left[i] = cgv::math::round(left[i]) + 0.5f;
		bottom[i] = cgv::math::round(bottom[i]) + 0.5f;
		right[i] = cgv::math::round(right[i]) + 0.5f;
	}

	// Helper function to add a new line
	const auto add_line = [&](vec2 v_0, vec2 v_1) {
		m_widget_lines.push_back(tf_editor_shared_data_types::line({ v_0, v_1 }));
	};

	// Left widget
	add_line(left[0], left[1]);
	add_line(left[2], left[1]);
	add_line(left[3], left[2]);
	add_line(left[3], left[0]);

	// Right widget
	add_line(right[1], right[0]);
	add_line(right[2], right[1]);
	add_line(right[3], right[2]);
	add_line(right[3], right[0]);

	// Bottom widget
	add_line(bottom[0], bottom[1]);
	add_line(bottom[1], bottom[2]);
	add_line(bottom[2], bottom[3]);
	add_line(bottom[3], bottom[0]);

	// Center widget, order: Left, right, bottom
	add_line(center[1], center[0]);
	add_line(center[0], center[2]);
	add_line(center[1], center[2]);

	// Create a polygon out of each widget
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(left));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(right));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(bottom));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(center));

	// draw smaller boundaries on the relations borders
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets which basically is the 4th line, they don't need boundaries
		if((i + 1) % 4 != 0) {
			const auto direction = normalize(m_widget_lines.at(i).b - m_widget_lines.at(i).a);
			const auto ortho_direction = cgv::math::ortho(direction);

			const auto boundary_left = m_widget_lines.at(i).interpolate(0.0f);
			const auto boundary_right = m_widget_lines.at(i).interpolate(1.0f);

			add_line(boundary_left - 4.0f * ortho_direction, boundary_left + 4.0f * ortho_direction);
		}
	}
}

void tf_editor_lines::create_centroid_boundaries() {
	float boundary_left;
	float boundary_right;

	const auto calculate_values = [&](int i, int protein_index) {
		// Get the relative position of the centroid and its left and right boundary
		const auto relative_position = m_shared_data_ptr->primitives.at(i).centr_pos[protein_index];
		boundary_left = relative_position - (m_shared_data_ptr->primitives.at(i).centr_widths[protein_index] / 2.0f);
		boundary_right = relative_position + (m_shared_data_ptr->primitives.at(i).centr_widths[protein_index] / 2.0f);
	};

	// The case where all values have to be drawn
	if(m_create_all_values) {
		m_strip_boundary_points.clear();

		for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
			// For each centroid, we want to create the lines of the boundaries
			std::vector<float> centroid_boundary_values;

			// Each widget has three points which will always generate the same values, 
			// so do less calculations by doing only every 4th point
			for(int j = 0; j < m_points.at(i).size(); j += 3) {
				// Get the correct protein
				calculate_values(i, j / 3);

				// Store the values
				centroid_boundary_values.push_back(boundary_left);
				centroid_boundary_values.push_back(boundary_right);
			}

			// Now the strips
			auto boundary_index = 0;
			std::vector<vec2> strip_coordinates;

			// Iterate over widgets, ignore the back widget as usual
			for(int i = 0; i < 15; i += 4) {
				for(int j = 0; j < 3; j++) {
					const auto vec_left = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index));
					const auto vec_right = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index + 1));

					// Push back two vectors for the corresponding widget line
					strip_coordinates.push_back(vec_left);
					strip_coordinates.push_back(vec_right);
				}
				boundary_index += 2;
			}

			m_strip_boundary_points.push_back(strip_coordinates);
		}
		m_create_all_values = false;
	}
	// If a centroid's position is dragged in the editor, it would make no sense to redraw everything
	// So we redraw only for the positionss that were updated
	else if(m_is_point_dragged) {
		// Recalculate values
		calculate_values(m_interacted_primitive_ids[0], m_interacted_primitive_ids[1] / 3);

		// No need to iterate over the first array entry
		for(int i = 1; i < 4; i++) {
			// Get the overall primitive layer and the parent line
			const auto primitive_layer = m_interacted_primitive_ids[0];
			auto& line = m_points[primitive_layer][m_interacted_primitive_ids[i]].m_parent_line;

			const auto id_left = m_interacted_primitive_ids[i] * 2;
			const auto id_right = m_interacted_primitive_ids[i] * 2 + 1;
			// Update the other centroids belonging to the widget as well
			m_strip_boundary_points[primitive_layer][id_left] = line->interpolate(boundary_left);
			m_strip_boundary_points[primitive_layer][id_right] = line->interpolate(boundary_right);
		}
	}
}

void tf_editor_lines::create_strips() {
	// Don't do anything if there are no points yet
	if(m_points.empty()) {
		return;
	}

	if(!m_strips_created) {
		create_centroid_boundaries();

		// Only draw quadstrips for the shape mode
		if(vis_mode == VM_SHAPES) {
			m_geometry_strips.clear();

			auto strip_index = 0;

			// Add four points to the strip, because every strip is between two widgets with two points each
			const auto add_points_to_strips = [&](int strip_id_1, int strip_id_2, int strip_id_3, int strip_id_4, int i, rgba color) {
				m_geometry_strips.add(m_strip_boundary_points.at(i).at(strip_id_1), color, vec2(0.0f, 0.0f));
				m_geometry_strips.add(m_strip_boundary_points.at(i).at(strip_id_2), color, vec2(0.0f, 1.0f));
				m_geometry_strips.add(m_strip_boundary_points.at(i).at(strip_id_3), color, vec2(1.0f, 0.0f));
				m_geometry_strips.add(m_strip_boundary_points.at(i).at(strip_id_4), color, vec2(1.0f, 1.0f));
			};
			// Add indices for the strips
			const auto add_indices_to_strips = [&](int offset_start, int offset_end) {
				for(int i = offset_start; i < offset_end; i++) {
					m_geometry_strips.add_idx(strip_index + i);
				}
				// If done, end this strip
				m_geometry_strips.add_idx(0xFFFFFFFF);
			};

			// Now strips themselves
			for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
				const auto color = m_shared_data_ptr->primitives.at(i).color;

				add_points_to_strips(0, 1, 10, 11, i, color);
				add_indices_to_strips(0, 4);
				add_points_to_strips(3, 2, 19, 18, i, color);
				add_indices_to_strips(4, 8);
				add_points_to_strips(5, 4, 13, 12, i, color);
				add_indices_to_strips(8, 12);
				add_points_to_strips(7, 6, 17, 16, i, color);
				add_indices_to_strips(12, 16);
				add_points_to_strips(9, 8, 21, 20, i, color);
				add_indices_to_strips(16, 20);
				add_points_to_strips(14, 15, 22, 23, i, color);
				add_indices_to_strips(20, 24);

				strip_index += 24;
			}

			m_strips_created = true;
		}
	}
}

void tf_editor_lines::create_strip_borders(int index) {
	// if there are no values yet, do not do anything
	if(m_strip_boundary_points.at(index).empty()) {
		return;
	}

	m_geometry_strip_borders.clear();

	const auto add_indices_to_strip_borders = [&](int index, int a, int b) {
		m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(a), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
		m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(b), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
	};

	add_indices_to_strip_borders(index, 0, 10);
	add_indices_to_strip_borders(index, 1, 11);
	add_indices_to_strip_borders(index, 2, 18);
	add_indices_to_strip_borders(index, 3, 19);
	add_indices_to_strip_borders(index, 4, 12);
	add_indices_to_strip_borders(index, 5, 13);
	add_indices_to_strip_borders(index, 6, 16);
	add_indices_to_strip_borders(index, 7, 17);
	add_indices_to_strip_borders(index, 8, 20);
	add_indices_to_strip_borders(index, 9, 21);
	add_indices_to_strip_borders(index, 14, 22);
	add_indices_to_strip_borders(index, 15, 23);
}

void tf_editor_lines::add_widget_lines() {
	m_geometry_widgets.clear();

	for(const auto l : m_widget_lines) {
		m_geometry_widgets.add(l.a, m_gray_widgets);
		m_geometry_widgets.add(l.b, m_gray_widgets);
	}
}

void tf_editor_lines::add_draggables(int primitive_index) {
	std::vector<tf_editor_shared_data_types::point_line> points;
	// Add the new draggable points to the widget lines, start with the left side
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets
		if((i + 1) % 4 != 0) {
			int index;
			if(i < 3) {
				index = 0;
			} else if(i >= 4 && i < 7) {
				index = 1;
			} else if(i >= 8 && i < 11) {
				index = 2;
			} else if(i >= 12 && i < 115) {
				index = 3;
			}

			const auto value = m_shared_data_ptr->primitives.at(primitive_index).centr_pos[index];
			points.push_back(tf_editor_shared_data_types::point_line(vec2(m_widget_lines.at(i).interpolate(value)), &m_widget_lines.at(i)));
		}
	}
	m_points.push_back(points);
}

void tf_editor_lines::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// first draw the plot (the method will check internally if it needs an update)
	//bool done = draw_plot(ctx);

	// enable the offline framebuffer
	fbc.enable(ctx);
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);










	auto& tone_mapping_prog = content_canvas.enable_shader(ctx, "plot_tone_mapping");
	tone_mapping_prog.set_uniform(ctx, "normalization_factor", 1.0f / static_cast<float>(std::max(tm_normalization_count, 1u)));
	tone_mapping_prog.set_uniform(ctx, "alpha", tm_alpha);
	tone_mapping_prog.set_uniform(ctx, "use_color", vis_mode == VM_GTF);

	m_style_plot.apply(ctx, tone_mapping_prog);

	if(m_mode == M_COMPUTE) {
		plot_texture.enable(ctx, 0);
		content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
		plot_texture.disable(ctx);
	} else if(m_mode == M_COMPUTE_2) {

		int li0[6] = { 0, 12, 8, 10, 13, 9 };
		int li1[6] = { 6, 1, 2, 4, 5, 14 };

		for(size_t i = 0; i < 6; ++i) {
			plot_textures[i].enable(ctx, 0);

			auto& l0 = m_widget_lines[li0[i]];
			auto& l1 = m_widget_lines[li1[i]];

			vec2 delta = l0.b - l0.a;

			vec2 scale(
				length(l1.a - l0.a),
				0.8f*l0.get_length()
			);

			vec2 offset = l1.a + 0.1f*delta;

			vec2 dir = normalize(delta);
			float angle = atan2(-dir.x(), dir.y());

			// 0 myo
			// 1 act
			// 2 obs
			// 3 sal

			// 0, 1
			// 0, 3 -> 3, 0
			// 0, 2 -> 2, 0
			// 1, 2 -> 2, 1
			// 1, 3 -> 3, 1
			// 2, 3

			content_canvas.push_modelview_matrix();
			content_canvas.mul_modelview_matrix(ctx, cgv::math::translate2h(offset));
			content_canvas.mul_modelview_matrix(ctx, cgv::math::rotate2h(cgv::math::rad2deg(angle)));
			content_canvas.mul_modelview_matrix(ctx, cgv::math::scale2h(scale / plot_resolution));

			content_canvas.set_view(ctx, tone_mapping_prog);

			content_canvas.draw_shape(ctx, ivec2(0), ivec2(plot_resolution));

			content_canvas.pop_modelview_matrix(ctx);
			plot_textures[i].disable(ctx);
		}
	} else {
		fbc_plot.enable_attachment(ctx, "color", 0);
		content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
		fbc_plot.disable_attachment(ctx, "color");
	}
	content_canvas.disable_current_shader(ctx);












	/*// draw the plot content from its own framebuffer texture
	fbc_plot.enable_attachment(ctx, "color", 0);

	auto& rectangle_prog = content_canvas.enable_shader(ctx, use_tone_mapping ? "plot_tone_mapping" : "rectangle");
	// Apply tone mapping via the tone mapping shader
	if(use_tone_mapping) {
		rectangle_prog.set_uniform(ctx, "normalization_factor", 1.0f / static_cast<float>(std::max(tm_normalization_count, 1u)));
		rectangle_prog.set_uniform(ctx, "alpha", tm_alpha);
		rectangle_prog.set_uniform(ctx, "use_color", vis_mode == VM_GTF);
	}

	m_style_plot.use_blending = use_tone_mapping;
	m_style_plot.apply(ctx, rectangle_prog);

	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");*/



	
	// draw widget lines first
	auto& line_prog = m_renderer_lines.ref_prog();
	/*line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_style_widgets.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_renderer_lines.render(ctx, PT_LINES, m_geometry_widgets);

	// then arrows on top
	auto& arrow_prog = content_canvas.enable_shader(ctx, "arrow");
	m_style_arrows.apply(ctx, arrow_prog);
	// draw arrows indicating where the maximum value is
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need arrows
		if((i + 1) % 4 != 0) {
			const auto& l = m_widget_lines.at(i);
			vec2 a = l.interpolate(1.0f);
			vec2 b = a + 10.0f*normalize(l.b - l.a);
			content_canvas.draw_shape2(ctx, a, b);
		}
	}
	content_canvas.disable_current_shader(ctx);*/
	
	// Do not draw quadstrips and the border lines for peak mode
	if (!is_peak_mode) {
		// Now create the centroid boundaries and strips
		create_strips();

		if (vis_mode == VM_SHAPES) {
			auto& line_prog_polygon = m_renderer_strips.ref_prog();
			line_prog_polygon.enable(ctx);
			content_canvas.set_view(ctx, line_prog_polygon);
			line_prog_polygon.disable(ctx);

			// draw the lines from the given geometry with offset and count
			glEnable(GL_PRIMITIVE_RESTART);
			glPrimitiveRestartIndex(0xFFFFFFFF);
			m_renderer_strips.render(ctx, PT_TRIANGLE_STRIP, m_geometry_strips);
			glDisable(GL_PRIMITIVE_RESTART);
		}

		for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
			// Strip borders
			create_strip_borders(i);

			line_prog.enable(ctx);
			content_canvas.set_view(ctx, line_prog);
			const auto color = m_shared_data_ptr->primitives.at(i).color;
			m_style_strip_borders.border_color = rgba{ color.R(), color.G(), color.B(), 1.0f };
			m_style_strip_borders.apply(ctx, line_prog);
			line_prog.disable(ctx);
			m_renderer_lines.render(ctx, PT_LINES, m_geometry_strip_borders);
		}
	}

	// then labels
	auto& font_renderer = cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx);
	font_renderer.render(ctx, content_canvas, m_labels, m_style_text);

	// draggables are the last thing to be drawn so they are above everything else
	tf_editor_basic::draw_draggables(ctx, m_points, m_interacted_primitive_ids[0]);

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = false;// !done;
}

bool tf_editor_lines::draw_plot(cgv::render::context& ctx) {

	/*// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	if(use_tone_mapping) {
		glBlendFunc(GL_ONE, GL_ONE);
	}

	// make sure to reset the color buffer if we update the content from scratch
	if(m_total_count == 0 || m_reset_plot) {
		use_tone_mapping ? glClearColor(0.0f, 0.0f, 0.0f, 0.0f) : glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		m_reset_plot = false;
	}

	// the amount of lines that will be drawn in each step
	auto count = 1000000;

	// make sure not to draw more lines than available
	if(m_total_count + count > m_geometry_relations.get_render_count())
		count = m_geometry_relations.get_render_count() - m_total_count;
	// draw the relations
	if(count > 0) {
		auto& line_prog = m_renderer_lines.ref_prog();
		line_prog.enable(ctx);
		content_canvas.set_view(ctx, line_prog);
		m_style_relations.fill_color = rgba(rgb(0.0f), m_alpha);
		m_style_relations.apply(ctx, line_prog);
		line_prog.disable(ctx);
		m_renderer_lines.render(ctx, PT_LINES, m_geometry_relations, m_total_count, count);
	}

	// accumulate the total amount of so-far drawn lines
	m_total_count += count;

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc_plot.disable(ctx);

	// reset the blend function
	if(use_tone_mapping) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	auto run = m_total_count < m_geometry_relations.get_render_count();
	if(run) {
		post_redraw();
	} else {
		//std::cout << "done" << std::endl;
		m_strips_created = false;
	}
	return !run;*/
	return true;
}

void tf_editor_lines::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();
	m_currently_dragging = true;

	const auto set_points = [&](int index_row, int index_col, int pos_1, int pos_2) {
		const auto relative_position = (m_points[index_row][index_col].get_relative_line_position() - 0.1f) / 0.8f;
		m_points[index_row][index_col + pos_1].move_along_line(relative_position);
		m_points[index_row][index_col + pos_2].move_along_line(relative_position);
		m_interacted_primitive_ids[2] = index_col + pos_1;
		m_interacted_primitive_ids[3] = index_col + pos_2;
	};

	for(unsigned i = 0; i < m_points.size(); ++i) {
		for(int j = 0; j < m_points[i].size(); j++) {
			// Now the relating draggables in the widget have to be updated
			if(&m_points[i][j] == m_point_handles.get_dragged()) {
				m_interacted_primitive_ids[0] = i;
				m_interacted_primitive_ids[1] = j;

				// Left widget draggable was moved, update center and right
				if(j % 3 == 0) {
					set_points(i, j, 1, 2);
				}
				// Center widget draggable was moved, update left and right
				else if(j % 3 == 1) {
					set_points(i, j, -1, 1);
				}
				// Right widget draggable was moved, update left and center
				else if(j % 3 == 2) {
					set_points(i, j, -1, -2);
				}

				m_is_point_dragged = true;
				is_interacting = true;

				int protein_index = j / 3;
				// Remap to correct GUI vals
				const auto GUI_value = (m_points[i][j].get_relative_line_position() - 0.1f) / 0.8f;
				m_shared_data_ptr->primitives.at(i).centr_pos[protein_index] = GUI_value;
				update_member(&m_shared_data_ptr->primitives.at(i).centr_pos[protein_index]);

				m_shared_data_ptr->set_synchronized();
			}
		}
	}

	if(vis_mode == VM_GTF)
		update_content();

	redraw();
}

void tf_editor_lines::set_point_handles() {
	m_point_handles.clear();
	for(unsigned i = 0; i < m_points.size(); ++i) {
		for(int j = 0; j < m_points[i].size(); j++) {
			m_point_handles.add(&m_points[i][j]);
		}
	}
}

void tf_editor_lines::update_point_positions() {
	// update the point positions after a resize
	if(m_shared_data_ptr && m_points.size() == m_shared_data_ptr->primitives.size()) {
		for(size_t i = 0; i < m_points.size(); ++i) {
			auto& points = m_points[i];
			auto& centroid = m_shared_data_ptr->primitives[i];

			size_t idx = 0;
			if(points.size() >= 4 * 3) {
				for(int j = 0; j < 15; j++) {
					// ignore the "back" lines of the widgets
					if((j + 1) % 4 != 0) {
						float c = centroid.centr_pos[j / 4];

						points[idx].pos = m_widget_lines.at(j).interpolate(c);
						++idx;
					}
				}
			}
		}
	}
}

void tf_editor_lines::point_clicked(const vec2& mouse_pos) {
	m_is_point_dragged = false;
	auto found = false;

	const auto set_points = [&](int index_row, int index_col, int pos_1, int pos_2) {
		const auto relative_position = (m_points[index_row][index_col].get_relative_line_position() - 0.1f) / 0.8f;
		m_interacted_primitive_ids[2] = index_col + pos_1;
		m_interacted_primitive_ids[3] = index_col + pos_2;
	};

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating draggables in the widget have to be updated
			if (m_points[i][j].is_inside(mouse_pos)) {
				m_interacted_primitive_ids[0] = i;
				m_interacted_primitive_ids[1] = j;

				// Left widget draggable was moved, update center and right
				if (j % 3 == 0) {
					set_points(i, j, 1, 2);
				}
				// Center widget draggable was moved, update left and right
				else if (j % 3 == 1) {
					set_points(i, j, -1, 1);
				}
				// Right widget draggable was moved, update left and center
				else if (j % 3 == 2) {
					set_points(i, j, -1, -2);
				}

				found = true;
			}
		}
	}

	is_interacting = found;
	m_shared_data_ptr->is_primitive_selected = found;
	m_shared_data_ptr->selected_primitive_id = m_interacted_primitive_ids[0];
	if (!found) {
		m_interacted_primitive_ids[0] = INT_MAX;
	}
	redraw();
}

void tf_editor_lines::scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed) {
	auto found = false;
	int found_index;
	// Search through all polygons
	for(int i = 0; i < m_widget_polygons.size(); i++) {
		// If we found a polygon, update the corresponding width
		if(m_widget_polygons.at(i).is_point_in_polygon(x, y)) {
			// Stronger change for pressed shift
			auto change = shift_pressed ? 0.05f : 0.02f;
			if(negative_change) {
				change *= -1.0f;
			}
			auto& width = m_shared_data_ptr->primitives[m_interacted_primitive_ids[0]].centr_widths[i];
			width += change;
			width = cgv::math::clamp(width, 0.0f, 10.0f);

			found = true;
			found_index = i;
			break;
		}
	}
	// If we found something, we have to set the corresponding point ids and redraw
	if(found) {
		m_is_point_dragged = true;
		tf_editor_shared_functions::set_interacted_centroid_ids(m_interacted_primitive_ids, found_index);

		m_shared_data_ptr->set_synchronized();
		redraw();
	}
}

/** END - MFLEURY **/