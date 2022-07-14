/** BEGIN - MFLEURY **/

#include "tf_editor_lines.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

#include "utils_functions.h"

tf_editor_lines::tf_editor_lines()
{	
	set_name("PCP Overlay");
	// prevent the mouse events from reaching through this overlay to the underlying elements
	block_events = true;

	// setup positioning and size
	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(-3));
	// set the size with an aspect ratio that makes lets the editor nicely fit inside
	// aspect ratio is w:h = 1:0.875
	set_overlay_size(ivec2(600, 525));
	
	// add a color attachment to the content frame buffer with support for transparency (alpha)
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	// change its size to be the same as the overlay
	fbc.set_size(get_overlay_size());

	fbc_plot.add_attachment("color", "flt32[R,G,B,A]");
	fbc_plot.set_size(get_overlay_size());

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);
	content_canvas.register_shader("arrow", cgv::glutil::canvas::shaders_2d::arrow);
	content_canvas.register_shader("plot_tone_mapping", "plot_tone_mapping.glpr");

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main
	// frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the renderers
	m_line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_point_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);
	m_polygon_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::polygon);

	// callbacks for the moving of centroids
	m_point_handles.set_drag_callback(std::bind(&tf_editor_lines::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_lines::end_drag, this));
}

void tf_editor_lines::clear(cgv::render::context& ctx) {
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);
	fbc_plot.clear(ctx);

	m_line_renderer.destruct(ctx);
	m_line_geometry_relations.destruct(ctx);
	m_line_geometry_widgets.destruct(ctx);
	m_line_geometry_strip_borders.destruct(ctx);

	m_font.destruct(ctx);
	m_font_renderer.destruct(ctx);

	m_polygon_renderer.destruct(ctx);

	m_point_renderer.destruct(ctx);
	m_point_geometry.destruct(ctx);
	m_point_geometry_interacted.destruct(ctx);
}

bool tf_editor_lines::self_reflect(cgv::reflect::reflection_handler& _rh) {
	return true;
}

bool tf_editor_lines::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if (et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;
		if (ke.get_action() == cgv::gui::KA_PRESS && ke.get_key() == 'M') {
			vis_mode == VM_QUADSTRIP ? vis_mode = VM_GTF : vis_mode = VM_QUADSTRIP;
			on_set(&vis_mode);
			update_content();

			return true;
		}
	}
	else if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;

		const auto mpos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));
		// Search for points if RMB is pressed
		if (me.get_button() == cgv::gui::MB_RIGHT_BUTTON) {
			find_clicked_centroid(mpos.x(), mpos.y());
		}

		// Set width if a scroll is done
		else if (me.get_action() == cgv::gui::MA_WHEEL && m_is_point_clicked) {
			const auto modifiers = e.get_modifiers();
			const auto negative_change = me.get_dy() > 0 ? true : false;
			const auto shift_pressed = modifiers & cgv::gui::EM_SHIFT ? true : false;

			scroll_centroid_width(mpos.x(), mpos.y(), negative_change, shift_pressed);
		}

		bool handled = false;
		handled |= m_point_handles.handle(e, last_viewport_size, container);

		if (handled)
			post_redraw();
		
		return handled;
	}

	return false;
}

void tf_editor_lines::on_set(void* member_ptr) {
	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &line_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}
	// change the labels if the GUI index is updated
	for (int i = 0; i < 4; i++) {
		if (member_ptr == &m_text_ids[i]) {
			m_text_ids[i] = cgv::math::clamp(m_text_ids[i], 0, 3);
			if (m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3 && m_labels.size() > 1)
				m_labels.set_text(i, m_data_set_ptr->stain_names[m_text_ids[i]]);
			break;
		}
	}

	if (member_ptr == &vis_mode) {
		update_content();
	}

	// look for updated centroid data
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); ++i) {
		auto value = 0.0f;

		for (int c_protein_i = 0; c_protein_i < 4; c_protein_i++) {
			if (member_ptr == &m_shared_data_ptr->centroids.at(i).centroids[c_protein_i] || 
				member_ptr == &m_shared_data_ptr->centroids.at(i).color ||
				member_ptr == &m_shared_data_ptr->centroids.at(i).widths[c_protein_i]) {
				// Move the according points if their position was changed
				if (member_ptr == &m_shared_data_ptr->centroids.at(i).centroids[c_protein_i]) {
					value = m_shared_data_ptr->centroids.at(i).centroids[c_protein_i];
					const auto index = c_protein_i * 3;

					m_points[i][index].pos = m_widget_lines.at(index + (index / 3)).interpolate(value);
					m_points[i][index + 1].pos = m_widget_lines.at(index + 1 + (index / 3)).interpolate(value);
					m_points[i][index + 2].pos = m_widget_lines.at(index + 2 + (index / 3)).interpolate(value);
					// Set the strips
					m_interacted_id_set = true;
					utils_functions::set_interacted_centroid_ids(m_interacted_centroid_ids, i, c_protein_i);
				}
				// Every centroid for this index has to be redrawn if he width was adjusted
				else if (member_ptr == &m_shared_data_ptr->centroids.at(i).widths[c_protein_i]) {
					utils_functions::set_interacted_centroid_ids(m_interacted_centroid_ids, i, c_protein_i);
				}
				// In all cases, we need to resynchronize
				m_shared_data_ptr->set_synchronized();
				break;
			}
		}
	}
	update_member(member_ptr);
	redraw(false);
}

bool tf_editor_lines::init(cgv::render::context& ctx) {

	bool success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= fbc_plot.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_line_renderer.init(ctx);
	success &= m_font_renderer.init(ctx);
	success &= m_point_renderer.init(ctx);
	success &= m_polygon_renderer.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if (success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if (m_font.init(ctx)) {
		m_labels.set_msdf_font(&m_font);
		m_labels.set_font_size(m_font_size);
	}

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

		init_widgets();
		add_widget_lines();

		m_point_handles.set_constraint(domain);

		create_labels();

		update_point_positions();
		
		// update the quad positions after a resize
		m_strips_created = false;
		m_create_all_values = true;

		has_damage = true;
		reset_plot = true;
	}
}

void tf_editor_lines::draw(cgv::render::context& ctx) {

	if(!show)
		return;

	// disable depth testing to place this overlay on top of everything in the viewport
	glDisable(GL_DEPTH_TEST);

	// redraw the contents if they are damaged (i.e. they need to change)
	if(has_damage)
		draw_content(ctx);
	
	// draw frame buffer texture to screen using a rectangle shape
	viewport_canvas.enable_shader(ctx, "rectangle");
	// enable the color attachment from the offline frame buffer as a texture
	fbc.enable_attachment(ctx, "color", 0);
	// Invoke the drawing of a simple shape with given position and size.
	// Since the rectange shader is active, this will produce a rectangle
	// and since we configured the shader with our style, the final color
	// of the rectangle will be determined by the texture.
	viewport_canvas.draw_shape(ctx, get_overlay_position(), get_overlay_size());
	fbc.disable_attachment(ctx, "color");
	viewport_canvas.disable_current_shader(ctx);
	
	// make sure to re-enable the depth test after we are done
	glEnable(GL_DEPTH_TEST);
}

void tf_editor_lines::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	
	// first draw the plot (the method will check internally if it needs an update)
	bool done = draw_plot(ctx);

	// enable the offline framebuffer
	fbc.enable(ctx);
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// draw the plot content from its own framebuffer texture
	fbc_plot.enable_attachment(ctx, "color", 0);

	auto& rectangle_prog = content_canvas.enable_shader(ctx, use_tone_mapping ? "plot_tone_mapping" : "rectangle");

	if (use_tone_mapping) {
		rectangle_prog.set_uniform(ctx, "normalization_factor", 1.0f / static_cast<float>(std::max(tm_normalization_count, 1u)));
		rectangle_prog.set_uniform(ctx, "alpha", tm_alpha);
		rectangle_prog.set_uniform(ctx, "gamma", tm_gamma);
	}

	m_plot_line_style.use_blending = use_tone_mapping;
	m_plot_line_style.apply(ctx, rectangle_prog);

	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");

	// draw lines first
	auto& line_prog = m_line_renderer.ref_prog();
	line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_line_style_widgets.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_line_renderer.render(ctx, PT_LINES, m_line_geometry_widgets);

	// then arrows on top
	draw_arrows(ctx);

	// Now create the centroid boundaries and strips
	create_centroid_strips();

	if (vis_mode == VM_QUADSTRIP) {
		auto& line_prog_polygon = m_polygon_renderer.ref_prog();
		line_prog_polygon.enable(ctx);
		content_canvas.set_view(ctx, line_prog_polygon);
		line_prog_polygon.disable(ctx);

		// draw the lines from the given geometry with offset and count
		glEnable(GL_PRIMITIVE_RESTART);
		glPrimitiveRestartIndex(0xFFFFFFFF);
		m_polygon_renderer.render(ctx, PT_TRIANGLE_STRIP, m_strips);
		glDisable(GL_PRIMITIVE_RESTART);
	}

	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
		// Strip borders
		m_line_style_strip_borders.border_color = rgba{ m_shared_data_ptr->centroids.at(i).color, 1.0f };
		create_strip_borders(i);

		line_prog.enable(ctx);
		content_canvas.set_view(ctx, line_prog);
		m_line_style_strip_borders.apply(ctx, line_prog);
		line_prog.disable(ctx);
		m_line_renderer.render(ctx, PT_LINES, m_line_geometry_strip_borders);
	}

	
	// then labels
	auto& font_prog = m_font_renderer.ref_prog();
	font_prog.enable(ctx);
	content_canvas.set_view(ctx, font_prog);
	font_prog.disable(ctx);
	m_font_renderer.render(ctx, get_overlay_size(), m_labels);

	// draggables are the last thing to be drawn so they are above everything else
	draw_draggables(ctx);

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = !done;
}

void tf_editor_lines::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &tf_editor_lines::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Line Alpha", line_alpha, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "other_threshold", other_threshold, "check");

	add_member_control(this, "Use Tone Mapping", use_tone_mapping, "check");
	add_member_control(this, "TM Norm Count", tm_normalization_count, "value_slider", "min=1;max=1000000;step=0.0001;log=true;ticks=true");
	add_member_control(this, "TM Alpha", tm_alpha, "value_slider", "min=0;max=50;step=0.0001;log=true;ticks=true");
	add_member_control(this, "TM Gamma", tm_gamma, "value_slider", "min=0;max=10;step=0.0001;log=true;ticks=true");

	add_member_control(this, "Interpolation", vis_mode, "dropdown", "enums=Quadstrip Mode, GTF Mode");

	// Create new centroids
	auto const add_centroid_button = add_button("Add centroid");
	connect_copy(add_centroid_button->click, rebind(this, &tf_editor_lines::add_centroids));

	// Add GUI controls for the centroid
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++ ) {
		const auto header_string = "Centroid " + std::to_string(i) + " parameters:";
		add_decorator(header_string, "heading", "level=3");
		// Color widget
		add_member_control(this, "Color centroid", m_shared_data_ptr->centroids.at(i).color, "", "");
		// Centroid parameters themselves
		add_member_control(this, "Centroid Myosin", m_shared_data_ptr->centroids.at(i).centroids[0], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid Actin", m_shared_data_ptr->centroids.at(i).centroids[1], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid Obscurin", m_shared_data_ptr->centroids.at(i).centroids[2], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid Sallimus", m_shared_data_ptr->centroids.at(i).centroids[3], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		// Gaussian width
		add_member_control(this, "Width Myosin", m_shared_data_ptr->centroids.at(i).widths[0], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Width Actin", m_shared_data_ptr->centroids.at(i).widths[1], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Width Obscurin", m_shared_data_ptr->centroids.at(i).widths[2], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Width Salimus", m_shared_data_ptr->centroids.at(i).widths[3], "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
	}
}

void tf_editor_lines::resynchronize() {

	m_points.clear();
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
		add_centroid_draggables(false, i);
	}

	m_point_handles.clear();
	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			m_point_handles.add(&m_points[i][j]);
		}
	}
	m_create_all_values = true;

	redraw(false);
}

void tf_editor_lines::init_styles(cgv::render::context& ctx) {

	// configure style for rendering the plot framebuffer texture
	m_plot_line_style.use_texture = true;
	m_plot_line_style.apply_gamma = false;
	m_plot_line_style.feather_width = 0.0f;

	m_line_style_relations.use_blending = true;
	m_line_style_relations.use_fill_color = false;
	m_line_style_relations.apply_gamma = false;
	m_line_style_relations.fill_color = rgba(rgb(0.0f), line_alpha);
	m_line_style_relations.width = 1.0f;

	m_line_style_widgets.use_blending = true;
	m_line_style_widgets.use_fill_color = true;
	m_line_style_widgets.apply_gamma = false;
	m_line_style_widgets.fill_color = m_gray_widgets;
	m_line_style_widgets.width = 3.0f;

	m_line_style_polygons.use_blending = true;
	m_line_style_polygons.use_fill_color = false;
	m_line_style_polygons.apply_gamma = false;
	m_line_style_polygons.fill_color = rgba(1.0f, 0.0f, 0.0f, line_alpha);

	m_line_style_strip_borders.use_blending = true;
	m_line_style_strip_borders.use_fill_color = false;
	m_line_style_strip_borders.border_width = 1.5f;
	m_line_style_strip_borders.apply_gamma = false;

	// TODO: the polygon does not use a line style
	auto& line_prog = m_polygon_renderer.ref_prog();
	line_prog.enable(ctx);
	m_line_style_polygons.apply(ctx, line_prog);
	line_prog.disable(ctx);

	m_draggable_style.position_is_center = true;
	m_draggable_style.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style.border_width = 1.5f;
	m_draggable_style.use_blending = true;

	m_draggable_style_interacted.position_is_center = true;
	m_draggable_style_interacted.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style_interacted.border_width = 1.5f;
	m_draggable_style_interacted.use_blending = true;

	m_arrow_style.head_width = 10.0f;
	m_arrow_style.absolute_head_length = 8.0f;
	m_arrow_style.stem_width = 0.0f;
	m_arrow_style.feather_width = 0.0f;
	m_arrow_style.head_length_is_relative = false;
	m_arrow_style.fill_color = m_gray_arrows;
	m_arrow_style.border_color = m_gray_arrows;
	m_arrow_style.use_fill_color = true;
	m_arrow_style.use_blending = true;

	cgv::glutil::shape2d_style plot_rect_style;
	plot_rect_style.use_texture = true;
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	plot_rect_style.apply(ctx, rectangle_prog);
	content_canvas.disable_current_shader(ctx);

	cgv::glutil::shape2d_style text_style;
	text_style.fill_color = rgba(rgb(0.0f), 1.0f);
	text_style.border_color.alpha() = 0.0f;
	text_style.border_width = 0.333f;
	text_style.use_blending = true;
	text_style.apply_gamma = false;

	auto& font_prog = m_font_renderer.ref_prog();
	font_prog.enable(ctx);
	text_style.apply(ctx, font_prog);
	font_prog.disable(ctx);

	// configure style for final blending of whole overlay
	overlay_style.fill_color = rgba(1.0f);
	overlay_style.use_texture = true;
	overlay_style.use_blending = false;
	overlay_style.apply_gamma = true;
	overlay_style.border_color = rgba(rgb(0.5f), 1.0f);
	overlay_style.border_width = 3.0f;
	overlay_style.feather_width = 0.0f;

	// also does not change, so is only set whenever this method is called
	auto& overlay_prog = viewport_canvas.enable_shader(ctx, "rectangle");
	overlay_style.apply(ctx, overlay_prog);
	viewport_canvas.disable_current_shader(ctx);
}

void tf_editor_lines::create_labels() {

	m_labels.clear();

	// Set the font texts
	if(m_font.is_initialized() && m_widget_polygons.size() > 3) {

		vec2 centers[4];
		for(int i = 0; i < 4; i++)
			centers[i] = m_widget_polygons[m_text_ids[i]].get_center();
		
		m_labels.add_text("0", ivec2(centers[0]), cgv::render::TA_NONE, 60.0f);
		m_labels.add_text("1", ivec2(centers[1]), cgv::render::TA_NONE, -60.0f);
		m_labels.add_text("2", ivec2(centers[2]), cgv::render::TA_NONE, 0.0);
		m_labels.add_text("3", ivec2(centers[3]), cgv::render::TA_NONE, 0.0f);

		for(int i = 0; i < 4; i++) {
			on_set(&m_text_ids[i]);
		}
	}
}

void tf_editor_lines::update_content() {
	
	if(!m_data_set_ptr || m_data_set_ptr->voxel_data.empty())
		return;

	const auto& data = m_data_set_ptr->voxel_data;

	m_create_all_values = true;
	m_interacted_id_set = false;

	// reset previous total count and line geometry
	total_count = 0;
	m_line_geometry_relations.clear();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		const vec4& v = data[i];

		// calculate the average to allow filtering with the given threshold
		auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		bool force = false;
		if(other_threshold) {
			force =
				v[0] > threshold ||
				v[1] > threshold ||
				v[2] > threshold ||
				v[3] > threshold;
		}

		if(avg > threshold || force) {

			rgb color_rgb(0.0f);
			
			if (vis_mode == VM_GTF) {
				for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
					const auto& centroid = m_shared_data_ptr->centroids.at(i);

					auto alpha = utils_functions::gaussian_transfer_function(v, centroid.centroids, centroid.widths);
					alpha *= centroid.color.alpha();

					color_rgb += alpha * rgb{ centroid.color.R(), centroid.color.G(), centroid.color.B() };
				}
			}

			color_rgb.R() = cgv::math::clamp(color_rgb.R(), 0.0f, 1.0f);
			color_rgb.G() = cgv::math::clamp(color_rgb.G(), 0.0f, 1.0f);
			color_rgb.B() = cgv::math::clamp(color_rgb.B(), 0.0f, 1.0f);

			rgba col(color_rgb, line_alpha);

			// Left to right
			m_line_geometry_relations.add(m_widget_lines.at(0).interpolate(v[m_text_ids[0]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(6).interpolate(v[m_text_ids[1]]), col);
			// Left to center
			m_line_geometry_relations.add(m_widget_lines.at(1).interpolate(v[m_text_ids[0]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(12).interpolate(v[m_text_ids[3]]), col);
			// Left to bottom
			m_line_geometry_relations.add(m_widget_lines.at(2).interpolate(v[m_text_ids[0]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(8).interpolate(v[m_text_ids[2]]), col);
			// Right to bottom
			m_line_geometry_relations.add(m_widget_lines.at(4).interpolate(v[m_text_ids[1]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(10).interpolate(v[m_text_ids[2]]), col);
			// Right to center
			m_line_geometry_relations.add(m_widget_lines.at(5).interpolate(v[m_text_ids[1]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(13).interpolate(v[m_text_ids[3]]), col);
			// Bottom to center
			m_line_geometry_relations.add(m_widget_lines.at(9).interpolate(v[m_text_ids[2]]), col);
			m_line_geometry_relations.add(m_widget_lines.at(14).interpolate(v[m_text_ids[3]]), col);
		}
	}
	// content was updated, so redraw
	redraw(false);
}

void tf_editor_lines::init_widgets() {
	m_widget_lines.clear();
	m_widget_polygons.clear();

	// Sizing constants for the widgets
	const float a = 1.0f; // Distance from origin (center of central triangle) to its corners
	const float b = 1.0f; // Orthogonal distance from center widget line to the outer widget line
	const float c = 2.0f*sin(cgv::math::deg2rad(60.0f))*b; // distance between two opposing outer widget lines

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
	for(size_t i = 0; i < 3; ++i)
		center[i] = scale * (center[i] + center_offset) + offset;

	for(size_t i = 0; i < 4; ++i) {
		left[i] = scale * (left[i] + center_offset) + offset;
		bottom[i] = scale * (bottom[i] + center_offset) + offset;
		right[i] = scale * (right[i] + center_offset) + offset;
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
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets which basically is the 4th line, they don't need boundaries
		if ((i + 1) % 4 != 0) {
			const auto direction = normalize(m_widget_lines.at(i).b - m_widget_lines.at(i).a);
			const auto ortho_direction = cgv::math::ortho(direction);

			const auto boundary_left = m_widget_lines.at(i).interpolate(0.0f);
			const auto boundary_right = m_widget_lines.at(i).interpolate(1.0f);

			m_widget_lines.push_back(
				tf_editor_shared_data_types::line({boundary_left - 5.0f * ortho_direction, boundary_left + 3.0f * ortho_direction}));
			m_widget_lines.push_back(
				tf_editor_shared_data_types::line({boundary_right - 5.0f * ortho_direction, boundary_right + 3.0f * ortho_direction}));
		}
	}
}

void tf_editor_lines::add_widget_lines() {
	m_line_geometry_widgets.clear();

	for (const auto l : m_widget_lines) {
		m_line_geometry_widgets.add(l.a);
		m_line_geometry_widgets.add(l.b);
	}
}

void tf_editor_lines::add_centroids() {
	// Hardcoded boundary, this might change later
	if (m_shared_data_ptr->centroids.size() == 5) {
		return;
	}

	// Create a new centroid and store it
	shared_data::centroid centr;
	m_shared_data_ptr->centroids.push_back(centr);
	add_centroid_draggables();
	// Add a corresponding point for every centroid
	m_point_handles.clear();
	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			m_point_handles.add(&m_points[i][j]);
		}
	}
	// A new centroid was added, so we need to redraw completely
	m_create_all_values = true;
	// Synchronize all other editors
	m_shared_data_ptr->set_synchronized();

	redraw(true);
}

void tf_editor_lines::add_centroid_draggables(bool new_point, int centroid_index) {
	std::vector<tf_editor_shared_data_types::point_line> points;
	// Add the new centroid points to the widget lines, start with the left side
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets
		if ((i + 1) % 4 != 0) {
			float value;
			if (new_point) {
				value = 0.0f;
			}
			else {
				int index;
				if (i < 3) {
					index = 0;
				}
				else if (i >= 4 && i < 7) {
					index = 1;
				}
				else if (i >= 8 && i < 11) {
					index = 2;
				}
				else if (i >= 12 && i < 115) {
					index = 3;
				}
				value = m_shared_data_ptr->centroids.at(centroid_index).centroids[index];
			}
			points.push_back(tf_editor_shared_data_types::point_line(vec2(m_widget_lines.at(i).interpolate(value)), &m_widget_lines.at(i)));
		}
	}
	m_points.push_back(points);
}

bool tf_editor_lines::draw_plot(cgv::render::context& ctx) {

	// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	if (use_tone_mapping) {
		glBlendFunc(GL_ONE, GL_ONE);
	}

	// make sure to reset the color buffer if we update the content from scratch
	if (total_count == 0 || reset_plot) {
		if (use_tone_mapping)
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		else
			glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		reset_plot = false;
	}

	// the amount of lines that will be drawn in each step
	int count = 1000000;

	// make sure not to draw more lines than available
	if(total_count + count > m_line_geometry_relations.get_render_count())
		count = m_line_geometry_relations.get_render_count() - total_count;
	// draw the relations
	if(count > 0) {
		auto& line_prog = m_line_renderer.ref_prog();
		line_prog.enable(ctx);
		content_canvas.set_view(ctx, line_prog);
		m_line_style_relations.apply(ctx, line_prog);
		line_prog.disable(ctx);
		m_line_renderer.render(ctx, PT_LINES, m_line_geometry_relations, total_count, count);
	}

	// accumulate the total amount of so-far drawn lines
	total_count += count;

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc_plot.disable(ctx);

	// reset the blend function
	if (use_tone_mapping) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < m_line_geometry_relations.get_render_count();
	if(run) {
		post_redraw();
	} else {
		//std::cout << "done" << std::endl;
		m_strips_created = false;
	}
	return !run;
}

void tf_editor_lines::draw_draggables(cgv::render::context& ctx) {
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); ++i) {
		// Clear for each centroid because colors etc might change
		m_point_geometry.clear();
		m_point_geometry_interacted.clear();

		const auto color = m_shared_data_ptr->centroids.at(i).color;
		// Apply color to the centroids, always do full opacity
		m_draggable_style.fill_color = rgba{color.R(), color.G(), color.B(), 1.0f};
		m_draggable_style_interacted.fill_color = rgba{ color.R(), color.G(), color.B(), 1.0f };

		for (int j = 0; j < m_points[i].size(); j++) {
			// Add the points based on if they have been interacted with
			std::find(m_interacted_points.begin(), m_interacted_points.end(), &m_points[i][j]) != m_interacted_points.end() ?
				m_point_geometry_interacted.add(m_points[i][j].get_render_position()) :
				m_point_geometry.add(m_points[i][j].get_render_position());
		}

		// Draw 
		shader_program& point_prog = m_point_renderer.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_draggable_style.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(12.0f));
		point_prog.disable(ctx);
		m_point_renderer.render(ctx, PT_POINTS, m_point_geometry);

		point_prog.enable(ctx);
		m_draggable_style_interacted.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(16.0f));
		point_prog.disable(ctx);
		m_point_renderer.render(ctx, PT_POINTS, m_point_geometry_interacted);
	}
}

void tf_editor_lines::draw_arrows(cgv::render::context& ctx) {
	auto& arrow_prog = content_canvas.enable_shader(ctx, "arrow");
	m_arrow_style.apply(ctx, arrow_prog);
	
	// draw arrows indicating where the maximum value is
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need arrows
		if ((i + 1) % 4 != 0) {
			content_canvas.draw_shape2(ctx, m_widget_lines.at(i).interpolate(0.85f), m_widget_lines.at(i).b, m_gray_widgets, m_gray_widgets);
		}
	}
	// dont forget to disable the curent shader when we don't need it anymore
	content_canvas.disable_current_shader(ctx);
}

bool tf_editor_lines::create_centroid_boundaries() {
	float relative_position;
	float boundary_left;
	float boundary_right;

	const auto calculate_values = [&](int i, int protein_index) {
		// Get the relative position of the centroid and it's left and right boundary
		relative_position = m_shared_data_ptr->centroids.at(i).centroids[protein_index];
		boundary_left = relative_position - (m_shared_data_ptr->centroids.at(i).widths[protein_index] / 2.0f);
		boundary_right = relative_position + (m_shared_data_ptr->centroids.at(i).widths[protein_index] / 2.0f);
	};

	// The case where all values have to be drawn
	if (m_create_all_values) {
		m_strip_border_points.clear();

		for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
			// For each centroid, we want to create the lines of the boundaries
			std::vector<float> centroid_boundary_values;

			// Each widget has three points which will always generate the same values, 
			// so do less calculations by doing only every 4th point
			for (int j = 0; j < m_points.at(i).size(); j += 3) {
				// Get the correct protein
				calculate_values(i, j / 3);

				// Store the values
				centroid_boundary_values.push_back(boundary_left);
				centroid_boundary_values.push_back(boundary_right);
			}

			// Now the strips
			int boundary_index = 0;
			std::vector<vec2> strip_coordinates;

			// Iterate over widgets, ignore the back widget as usual
			for (int i = 0; i < 15; i += 4) {
				for (int j = 0; j < 3; j++) {
					const auto vec_left = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index));
					const auto vec_right = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index + 1));

					// Push back two vectors for the corresponding widget line
					strip_coordinates.push_back(vec_left);
					strip_coordinates.push_back(vec_right);
				}
				boundary_index += 2;
			}

			m_strip_border_points.push_back(strip_coordinates);
		}
		m_create_all_values = false;

		return true;
	}
	// If a centroid is dragged in the editor, it would make no sense to redraw everything
	// So we redraw only the centroids that were updated
	else if (m_interacted_id_set) {
		// Recalculate values
		calculate_values(m_interacted_centroid_ids[0], m_interacted_centroid_ids[1] / 3);

		// No need to iterate over the first array entry
		for (int i = 1; i < 4; i++) {
			// Get the overall centroid layer and the parent line
			const auto centroid_layer = m_interacted_centroid_ids[0];
			auto& line = m_points[centroid_layer][m_interacted_centroid_ids[i]].m_parent_line;

			const auto id_left = m_interacted_centroid_ids[i] * 2;
			const auto id_right = m_interacted_centroid_ids[i] * 2 + 1;
			// Update the other centroids belonging to the widget as well
			m_strip_border_points[centroid_layer][id_left] = line->interpolate(boundary_left);
			m_strip_border_points[centroid_layer][id_right] = line->interpolate(boundary_right);
		}
		return true;
	}
	return false;
}

void tf_editor_lines::create_centroid_strips() {
	// Don't do anything if there are no points yet
	if (m_points.empty()) {
		return;
	}

	if (!m_strips_created) {
		if (!create_centroid_boundaries()) {
			return;
		}
		if (vis_mode == VM_QUADSTRIP) {
			m_strips.clear();
			m_line_geometry_strip_borders.clear();

			int strip_index = 0;

			// Add four points to the strip, because every strip is between two widgets with two points each
			const auto add_points_to_strips = [&](int strip_id_1, int strip_id_2, int strip_id_3, int strip_id_4, int i, rgba color) {
				m_strips.add(m_strip_border_points.at(i).at(strip_id_1), color);
				m_strips.add(m_strip_border_points.at(i).at(strip_id_2), color);
				m_strips.add(m_strip_border_points.at(i).at(strip_id_3), color);
				m_strips.add(m_strip_border_points.at(i).at(strip_id_4), color);
			};
			// Add indices for the strips
			const auto add_indices_to_strips = [&](int offset_start, int offset_end) {
				for (int i = offset_start; i < offset_end; i++) {
					m_strips.add_idx(strip_index + i);
				}
				// If done, end this strip
				m_strips.add_idx(0xFFFFFFFF);
			};

			// Now strips themselves
			for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
				const auto color = m_shared_data_ptr->centroids.at(i).color;

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
	if (m_strip_border_points.at(index).empty()) {
		return;
	}

	m_line_geometry_strip_borders.clear();

	const auto add_indices_to_strip_borders = [&](int index, int a, int b) {
		m_line_geometry_strip_borders.add(m_strip_border_points.at(index).at(a));
		m_line_geometry_strip_borders.add(m_strip_border_points.at(index).at(b));
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

void tf_editor_lines::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();
	m_interacted_points.clear();
	
	if (m_is_point_clicked) {
		m_is_point_clicked = false;
	}

	const auto set_points = [&](int index_row, int index_col, int pos_1, int pos_2) {
		const auto relative_position = (m_points[index_row][index_col].get_relative_line_position() - 0.1f) / 0.8f;
		m_points[index_row][index_col + pos_1].move_along_line(relative_position);
		m_points[index_row][index_col + pos_2].move_along_line(relative_position);
		m_interacted_centroid_ids[2] = index_col + pos_1;
		m_interacted_centroid_ids[3] = index_col + pos_2;
	};

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating centroid points in the widget have to be updated
			if (&m_points[i][j] == m_point_handles.get_dragged()) {
				m_interacted_points.push_back(&m_points[i][j]);

				m_interacted_centroid_ids[0] = i;
				m_interacted_centroid_ids[1] = j;

				// Left widget point was moved, update center and right
				if (j % 3 == 0) {
					set_points(i, j, 1, 2);
				}
				// Center widget point was moved, update left and right
				else if (j % 3 == 1) {
					set_points(i, j, -1, 1);
				}
				// Right widget point was moved, update left and center
				else if (j % 3 == 2) {
					set_points(i, j, -1, -2);
				}

				m_interacted_id_set = true;

				int protein_index = j / 3;
				// Remap to correct GUI vals
				const auto GUI_value = (m_points[i][j].get_relative_line_position() - 0.1f) / 0.8f;
				m_shared_data_ptr->centroids.at(i).centroids[protein_index] = GUI_value;
				update_member(&m_shared_data_ptr->centroids.at(i).centroids[protein_index]);

				m_shared_data_ptr->set_synchronized();
			}
		}
	}

	redraw(false);
}

void tf_editor_lines::update_point_positions()
{
	// update the point positions after a resize
	if (m_shared_data_ptr && m_points.size() == m_shared_data_ptr->centroids.size()) {
		for (size_t i = 0; i < m_points.size(); ++i) {
			auto& points = m_points[i];
			auto& centroid = m_shared_data_ptr->centroids[i];

			size_t idx = 0;
			if (points.size() >= 4 * 3) {
				for (int j = 0; j < 15; j++) {
					// ignore the "back" lines of the widgets
					if ((j + 1) % 4 != 0) {
						float c = centroid.centroids[j / 4];

						points[idx].pos = m_widget_lines.at(j).interpolate(c);
						//points.push_back(utils_data_types::point(vec2(m_widget_lines.at(i).interpolate(0.0f)), &m_widget_lines.at(i)));
						++idx;
					}
				}
			}
		}
	}
}

void tf_editor_lines::find_clicked_centroid(int x, int y) {
	const auto input_vec = vec2{ static_cast<float>(x), static_cast<float>(y)};
	auto found = false;
	int found_index;
	// Search all points
	for (int i = 0; i < m_points.size(); i++) {
		for (int j = 0; j < m_points.at(i).size(); j++) {
			// If the mouse was clicked inside a point, store all point addresses belonging to the 
			// corresponding layer
			if (m_points.at(i).at(j).is_inside(input_vec)) {
				m_interacted_points.clear();
				for (int k = 0; k < m_points.at(i).size(); k++) {
					m_interacted_points.push_back(&m_points.at(i).at(k));
				}
				found = true;
				found_index = i;
				break;
			}
		}
	}

	m_is_point_clicked = found;
	// If we found something, redraw 
	if (found) {
		m_clicked_centroid_id = found_index;
		redraw(false);
	}
}

void tf_editor_lines::scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed) {
	auto found = false;
	int found_index;
	// Search through all polygons
	for (int i = 0; i < m_widget_polygons.size(); i++) {
		// If we found a polygon, update the corresponding width
		if (m_widget_polygons.at(i).is_point_in_polygon(x, y)) {
			// Stronger change for pressed shift
			auto change = shift_pressed ? 0.05f : 0.02f;
			if (negative_change) {
				change *= -1.0f;
			}
			auto& width = m_shared_data_ptr->centroids[m_clicked_centroid_id].widths[i];
			width += change;
			width = cgv::math::clamp(width, 0.0f, 1.0f);

			found = true;
			found_index = i;
			break;
		}
	}
	// If we found something, we have to set the corresponding point ids and redraw
	if (found) {
		m_interacted_id_set = true;
		utils_functions::set_interacted_centroid_ids(m_interacted_centroid_ids, m_clicked_centroid_id, found_index);

		m_shared_data_ptr->set_synchronized();

		redraw(true);
	}
}

/** END - MFLEURY **/