#include "tf_editor_widget.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

#include "utils_functions.h"

tf_editor_widget::tf_editor_widget()
{	
	set_name("PCP Overlay");
	// prevent the mouse events from reaching throug this overlay to the underlying elements
	block_events = true;

	// setup positioning and size
	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(-3));
	set_overlay_size(ivec2(800, 500));
	
	// add a color attachment to the content frame buffer with support for transparency (alpha)
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	// change its size to be the same as the overlay
	fbc.set_size(get_overlay_size());

	fbc_plot.add_attachment("color", "flt32[R,G,B,A]");
	fbc_plot.set_size(get_overlay_size());

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);
	content_canvas.register_shader("arrow", cgv::glutil::canvas::shaders_2d::arrow);

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main
	// frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the line renderer with a shader program capable of drawing 2d lines
	m_line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_point_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);
	m_polygon_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::polygon);

	// callbacks for the moving of centroids
	m_point_handles.set_drag_callback(std::bind(&tf_editor_widget::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_widget::end_drag, this));
}

void tf_editor_widget::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);
	fbc_plot.clear(ctx);

	m_line_renderer.destruct(ctx);
	m_line_geometry_relations.destruct(ctx);
	m_line_geometry_widgets.destruct(ctx);
	m_line_geometry_nearest_values.destruct(ctx);
	m_line_geometry_strip_borders.destruct(ctx);

	m_font.destruct(ctx);
	m_font_renderer.destruct(ctx);

	m_polygon_renderer.destruct(ctx);
}

bool tf_editor_widget::self_reflect(cgv::reflect::reflection_handler& _rh) {
	return true;
}

bool tf_editor_widget::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if (et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;
		cgv::gui::KeyAction ka = ke.get_action();

		/*
		ka is one of:
		cgv::gui::[
		KA_PRESS,
		KA_RELEASE,
		KA_REPEAT
		]
		*/
		return false;
	}
	else if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;
		cgv::gui::MouseAction ma = me.get_action();

		if (me.get_action() == cgv::gui::MA_PRESS) {
			const auto offset = get_context()->get_height() - domain.size().y();
			check_mouse_click(me.get_x(), get_context()->get_height() - 1 - me.get_y() - offset);
		}

		bool handled = false;
		handled |= m_point_handles.handle(e, domain.size());

		if (handled)
			post_redraw();
		if (m_point_handles.handle(e, last_viewport_size, container)) {
			return true;
		}
		return false;
	}

	return false;
}

void tf_editor_widget::on_set(void* member_ptr) {
	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &line_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	// change the labels if the GUI index is updated
	}
	for (int i = 0; i < 4; i++) {
		if (member_ptr == &m_text_ids[i]) {
			m_text_ids[i] = cgv::math::clamp(m_text_ids[i], 0, 3);
			if (m_protein_names.size() > 3 && m_labels.size() > 1)
				m_labels.set_text(i, m_protein_names[m_text_ids[i]]);
			break;
		}
	}
	const auto set_dragged_centroid_ids = [&](int i, int index) {
		m_dragged_centroid_ids[0] = i;
		m_dragged_centroid_ids[1] = index;
		m_dragged_centroid_ids[2] = index + 1;
		m_dragged_centroid_ids[3] = index + 2;
	};

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

					m_points[i][index].pos = m_widget_lines.at(index + (index / 3)).interpolate((value * 0.8f) + 0.1f);
					m_points[i][index + 1].pos = m_widget_lines.at(index + 1 + (index / 3)).interpolate((value * 0.8f) + 0.1f);
					m_points[i][index + 2].pos = m_widget_lines.at(index + 2 + (index / 3)).interpolate((value * 0.8f) + 0.1f);

					set_dragged_centroid_ids(i, index);
				}
				// Every centroid for this index has to be redrawn if he width was adjusted
				else if (member_ptr == &m_shared_data_ptr->centroids.at(i).widths[c_protein_i]) {
					const auto index = c_protein_i * 3;
					set_dragged_centroid_ids(i, index);
				}
				// In all cases, we need to update
				was_updated = true;

				has_damage = true;
				break;
			}
		}
	}

	update_member(member_ptr);
	post_redraw();
}

bool tf_editor_widget::init(cgv::render::context& ctx) {

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

void tf_editor_widget::init_frame(cgv::render::context& ctx) {

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

		m_point_handles.set_constraint(domain);

		if (m_font.is_initialized()) {
			m_labels.clear();

			m_labels.add_text("0", ivec2(domain.size().x() * 0.27f, domain.size().y() * 0.70f), cgv::render::TA_NONE);
			m_labels.add_text("1", ivec2(domain.size().x() * 0.73f, domain.size().y() * 0.70f), cgv::render::TA_NONE);
			m_labels.add_text("2", ivec2(domain.size().x() * 0.5f, domain.size().y() * 0.18f), cgv::render::TA_NONE);
			m_labels.add_text("3", ivec2(domain.size().x() * 0.5f, domain.size().y() * 0.48f), cgv::render::TA_NONE);

			for (int i = 0; i < 4; i++) {
				on_set(&m_text_ids[i]);
			}
		}
	}
}

void tf_editor_widget::draw(cgv::render::context& ctx) {

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

void tf_editor_widget::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	
	// first draw the plot (the method will check internally if it needs an update
	bool done = draw_plot(ctx);

	// enable the offline framebuffer
	fbc.enable(ctx);
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// draw the plot content from its own framebuffer texture
	fbc_plot.enable_attachment(ctx, "color", 0);
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");

	// now draw the non-plot visuals
	// TODO: no need to call this every frame
	add_widget_lines();

	// draw lines first
	auto& line_prog = m_line_renderer.ref_prog();
	line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_line_style_widgets.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_line_renderer.render(ctx, PT_LINES, m_line_geometry_widgets);

	// then arrows on top
	draw_arrows(ctx);

	// Now create the centroid boundaries and strips, if the update button was pressed
	if (update_pressed) {
		create_centroid_strips();

		auto& line_prog_polygon = m_polygon_renderer.ref_prog();
		line_prog_polygon.enable(ctx);
		content_canvas.set_view(ctx, line_prog_polygon);
		line_prog_polygon.disable(ctx);

		// draw the lines from the given geometry with offset and count
		glEnable(GL_PRIMITIVE_RESTART);
		glPrimitiveRestartIndex(0xFFFFFFFF);
		m_polygon_renderer.render(ctx, PT_TRIANGLE_STRIP, m_strips);
		glDisable(GL_PRIMITIVE_RESTART);

		for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
			// Nearest value lines
			m_line_style_nearest_values.fill_color = rgba{ m_shared_data_ptr->centroids.at(i).color, 1.0f };
			create_nearest_value_lines(i);

			line_prog.enable(ctx);
			content_canvas.set_view(ctx, line_prog);
			m_line_style_nearest_values.apply(ctx, line_prog);
			line_prog.disable(ctx);
			m_line_renderer.render(ctx, PT_LINES, m_line_geometry_nearest_values);

			// Strip borders
			m_line_style_strip_borders.border_color = rgba{ m_shared_data_ptr->centroids.at(i).color, 1.0f };
			create_strip_borders(i);

			line_prog.enable(ctx);
			content_canvas.set_view(ctx, line_prog);
			m_line_style_strip_borders.apply(ctx, line_prog);
			line_prog.disable(ctx);
			m_line_renderer.render(ctx, PT_LINES, m_line_geometry_strip_borders);
		}
	}
	
	// then labels
	auto& font_prog = m_font_renderer.ref_prog();
	font_prog.enable(ctx);
	content_canvas.set_view(ctx, font_prog);
	font_prog.disable(ctx);
	m_font_renderer.render(ctx, get_overlay_size(), m_labels);

	// draggables are the last thing to be drawn
	draw_draggables(ctx);

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = !done;
}

void tf_editor_widget::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &tf_editor_widget::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Line Alpha", line_alpha, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Protein left:", m_text_ids[0], "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein right:", m_text_ids[1], "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein bottom:", m_text_ids[2], "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein center:", m_text_ids[3], "value", "min=0;max=3;step=1");

	// Create new centroids
	auto const add_centroid_button = add_button("Add centroid");
	connect_copy(add_centroid_button->click, rebind(this, &tf_editor_widget::add_centroids));

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

void tf_editor_widget::init_styles(cgv::render::context& ctx) {

	m_line_style_relations.use_blending = true;
	m_line_style_relations.use_fill_color = true;
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

	m_line_style_nearest_values.use_blending = true;
	m_line_style_nearest_values.use_fill_color = true;
	m_line_style_nearest_values.border_color = rgba{ 1.0f, 1.0f, 1.0f, 1.0f };
	m_line_style_nearest_values.border_width = 2.0f;
	m_line_style_nearest_values.ring_width = 2.5f;
	m_line_style_nearest_values.width = 7.0f;
	m_line_style_nearest_values.apply_gamma = false;

	m_line_style_strip_borders.use_blending = true;
	m_line_style_strip_borders.use_fill_color = false;
	m_line_style_strip_borders.border_width = 1.5f;
	m_line_style_strip_borders.apply_gamma = false;

	auto& line_prog = m_polygon_renderer.ref_prog();
	line_prog.enable(ctx);
	m_line_style_polygons.apply(ctx, line_prog);
	line_prog.disable(ctx);

	m_draggable_style.position_is_center = true;
	m_draggable_style.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style.border_width = 1.5f;
	m_draggable_style.use_blending = true;

	m_draggable_style_dragged.position_is_center = true;
	m_draggable_style_dragged.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style_dragged.border_width = 1.5f;
	m_draggable_style_dragged.use_blending = true;

	m_arrow_style.head_width = 10.0f;
	m_arrow_style.absolute_head_length = 8.0f;
	m_arrow_style.stem_width = 0.0f;
	m_arrow_style.feather_width = 0.0f;
	m_arrow_style.head_length_is_relative = false;
	m_arrow_style.fill_color = m_gray_arrows;
	m_arrow_style.border_color = m_gray_arrows;
	m_arrow_style.use_fill_color = true;

	cgv::glutil::shape2d_style plot_rect_style;
	plot_rect_style.use_texture = true;
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	plot_rect_style.apply(ctx, rectangle_prog);
	viewport_canvas.disable_current_shader(ctx);

	// configure style for the plot labels
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

void tf_editor_widget::update_content() {
	
	if(data.empty())
		return;

	if (!update_pressed) {
		update_pressed = true;
	}

	m_create_all_values = true;
	m_dragged_id_set = false;

	// reset previous total count and line geometry
	total_count = 0;
	m_line_geometry_relations.clear();

	m_min = std::numeric_limits<float>::max();
	m_max = -std::numeric_limits<float>::max();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		vec4& v = data[i];

		// calculate the average to allow filtering with the given threshold
		const auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		if(avg > threshold) {
			// Get the minimum and maximum value for each protein
			m_min = cgv::math::min(m_min, v);
			m_max = cgv::math::max(m_max, v);

			// Map the vector values to a range between 0.1 and 0.9 
			// so the outmost parts of the widget lines stay clear

			// Left to right
			m_line_geometry_relations.add(m_widget_lines.at(0).interpolate(v[m_text_ids[0]]));
			m_line_geometry_relations.add(m_widget_lines.at(6).interpolate(v[m_text_ids[1]]));
			// Left to center
			m_line_geometry_relations.add(m_widget_lines.at(1).interpolate(v[m_text_ids[0]]));
			m_line_geometry_relations.add(m_widget_lines.at(12).interpolate(v[m_text_ids[3]]));
			// Left to bottom
			m_line_geometry_relations.add(m_widget_lines.at(2).interpolate(v[m_text_ids[0]]));
			m_line_geometry_relations.add(m_widget_lines.at(8).interpolate(v[m_text_ids[2]]));
			// Right to bottom
			m_line_geometry_relations.add(m_widget_lines.at(4).interpolate(v[m_text_ids[1]]));
			m_line_geometry_relations.add(m_widget_lines.at(10).interpolate(v[m_text_ids[2]]));
			// Right to center
			m_line_geometry_relations.add(m_widget_lines.at(5).interpolate(v[m_text_ids[1]]));
			m_line_geometry_relations.add(m_widget_lines.at(13).interpolate(v[m_text_ids[3]]));
			// Bottom to center
			m_line_geometry_relations.add(m_widget_lines.at(9).interpolate(v[m_text_ids[2]]));
			m_line_geometry_relations.add(m_widget_lines.at(14).interpolate(v[m_text_ids[3]]));
		}
	}

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}

void tf_editor_widget::init_widgets() {
	m_widget_lines.clear();

	const auto add_lines = [&](vec2 v_0, vec2 v_1, vec2 v_2, vec2 v_3, bool invert = true) {
		m_widget_lines.push_back(utils_data_types::line({ v_0, v_1 }));
		invert ? m_widget_lines.push_back(utils_data_types::line({ v_2, v_1 })) : m_widget_lines.push_back(utils_data_types::line({ v_1, v_2 }));
		invert ? m_widget_lines.push_back(utils_data_types::line({ v_3, v_2 })) : m_widget_lines.push_back(utils_data_types::line({ v_2, v_3 }));
		m_widget_lines.push_back(utils_data_types::line({ v_3, v_0 }));
	};

	const auto add_points_to_polygon = [&](vec2 v_0, vec2 v_1, vec2 v_2, vec2 v_3) {
		utils_data_types::polygon p;
		p.points.push_back(v_0);
		p.points.push_back(v_1);
		p.points.push_back(v_2);
		p.points.push_back(v_3);
		m_widget_polygons.push_back(p);
	};

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();
	/// General line generation order: Left, center, right relations line, then a last line for closing
	// Left widget
	vec2 vec_0 {sizeX * 0.3f, sizeY * 0.95f};
	vec2 vec_1 {sizeX * 0.35f, sizeY * 0.75f};
	vec2 vec_2 {sizeX * 0.23f, sizeY * 0.45f};
	vec2 vec_3 {sizeX * 0.1f, sizeY * 0.45f};
	add_lines(vec_0, vec_1, vec_2, vec_3);
	add_points_to_polygon(vec_0, vec_1, vec_2, vec_3);

	// Right widget
	vec_0.set(sizeX * 0.9f, sizeY * 0.45f);
	vec_1.set(sizeX * 0.77f, sizeY * 0.45f);
	vec_2.set(sizeX * 0.65f, sizeY * 0.75f);
	vec_3.set(sizeX * 0.7f, sizeY * 0.95f);
	m_widget_lines.push_back(utils_data_types::line({ vec_1, vec_0 }));
	m_widget_lines.push_back(utils_data_types::line({ vec_2, vec_1 }));
	m_widget_lines.push_back(utils_data_types::line({ vec_3, vec_2 }));
	m_widget_lines.push_back(utils_data_types::line({ vec_3, vec_0 }));
	add_points_to_polygon(vec_0, vec_1, vec_2, vec_3);

	// Bottom widget
	vec_0.set(sizeX * 0.23f, sizeY * 0.05f);
	vec_1.set(sizeX * 0.33f, sizeY * 0.25f);
	vec_2.set(sizeX * 0.67f, sizeY * 0.25f);
	vec_3.set(sizeX * 0.77f, sizeY * 0.05f);
	add_lines(vec_0, vec_1, vec_2, vec_3, false);
	add_points_to_polygon(vec_0, vec_1, vec_2, vec_3);

	// Center widget, order: Left, right, bottom
	vec_0.set(sizeX * 0.4f, sizeY * 0.4f);
	vec_1.set(sizeX * 0.5f, sizeY * 0.6f);
	vec_2.set(sizeX * 0.6f, sizeY * 0.4f);
	m_widget_lines.push_back(utils_data_types::line({ vec_0, vec_1 }));
	m_widget_lines.push_back(utils_data_types::line({ vec_1, vec_2 }));
	m_widget_lines.push_back(utils_data_types::line({ vec_0, vec_2 }));

	utils_data_types::polygon p;
	p.points.push_back(vec_0);
	p.points.push_back(vec_1);
	p.points.push_back(vec_2);
	m_widget_polygons.push_back(p);

	// draw smaller boundaries on the relations borders
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need boundaries
		if ((i + 1) % 4 != 0) {
			const auto direction = normalize(m_widget_lines.at(i).b - m_widget_lines.at(i).a);
			const auto ortho_direction = cgv::math::ortho(direction);

			const auto boundary_left = m_widget_lines.at(i).interpolate(0.1f);
			const auto boundary_right = m_widget_lines.at(i).interpolate(0.9f);

			m_widget_lines.push_back(
				utils_data_types::line({boundary_left - 5.0f * ortho_direction, boundary_left + 3.0f * ortho_direction}));
			m_widget_lines.push_back(
				utils_data_types::line({boundary_right - 5.0f * ortho_direction, boundary_right + 3.0f * ortho_direction}));
		}
	}

}

void tf_editor_widget::add_widget_lines() {
	m_line_geometry_widgets.clear();

	for (const auto l : m_widget_lines) {
		m_line_geometry_widgets.add(l.a);
		m_line_geometry_widgets.add(l.b);
	}
}

void tf_editor_widget::add_centroids() {
	if (m_shared_data_ptr->centroids.size() == 5) {
		return;
	}

	// Create a new centroid and store it
	utils_data_types::centroid centr;
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

	has_damage = true;
	post_recreate_gui();
	post_redraw();
}

void tf_editor_widget::add_centroid_draggables() {
	std::vector<utils_data_types::point> points;
	// Add the new centroid points to the widget lines, start with the left side
	// Because the values are normed between 0.1f and 0.9f, start with 0.1f
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets
		if ((i + 1) % 4 != 0) {
			points.push_back(utils_data_types::point(vec2(m_widget_lines.at(i).interpolate(0.1f)), &m_widget_lines.at(i)));
		}
	}

	m_points.push_back(points);
}

bool tf_editor_widget::draw_plot(cgv::render::context& ctx) {

	// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	// make sure to reset the color buffer if we update the content from scratch
	if(total_count == 0) {
		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// the amount of lines that will be drawn in each step
	int count = 100000;

	// make sure to not draw more lines than available
	if(total_count + count > m_line_geometry_relations.get_render_count())
		count = m_line_geometry_relations.get_render_count() - total_count;

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

void tf_editor_widget::draw_draggables(cgv::render::context& ctx) {
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); ++i) {
		m_point_geometry.clear();
		m_point_geometry_dragged.clear();
		// Apply color to the centroids
		m_draggable_style.fill_color = m_shared_data_ptr->centroids.at(i).color;
		m_draggable_style_dragged.fill_color = m_shared_data_ptr->centroids.at(i).color;

		for (int j = 0; j < m_points[i].size(); j++) {
			// If a point has been dragged, check for its pointer and add it, otherwise add to the undragged ones
			if (m_dragged_point_ptr) {
				&m_points[i][j] == m_dragged_point_ptr ? m_point_geometry_dragged.add(m_points[i][j].get_render_position())
					: m_point_geometry.add(m_points[i][j].get_render_position());
			}
			// If nothing has been dragged, just add normally
			else {
				m_point_geometry.add(m_points[i][j].get_render_position());
			}
		}

		m_point_geometry.set_out_of_date();

		// Draw undragged and (if set) dragged centroids
		shader_program& point_prog = m_point_renderer.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_draggable_style.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", m_dragged_point_ptr ? vec2(10.0f) : vec2(16.0f));
		point_prog.disable(ctx);
		m_point_renderer.render(ctx, PT_POINTS, m_point_geometry);

		if (m_dragged_point_ptr) {
			point_prog.enable(ctx);
			m_draggable_style_dragged.apply(ctx, point_prog);
			point_prog.set_attribute(ctx, "size", vec2(16.0f));
			point_prog.disable(ctx);
			m_point_renderer.render(ctx, PT_POINTS, m_point_geometry_dragged);
		}
	}
}

void tf_editor_widget::draw_arrows(cgv::render::context& ctx) {
	auto& arrow_prog = content_canvas.enable_shader(ctx, "arrow");
	m_arrow_style.apply(ctx, arrow_prog);
	
	// draw arrows on the right widget sides
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need arrows
		if ((i + 1) % 4 != 0) {
			content_canvas.draw_shape2(ctx, m_widget_lines.at(i).interpolate(0.85f), m_widget_lines.at(i).b, m_gray_widgets, m_gray_widgets);
		}
	}
	// dont forget to disable the curent shader when we don't need it anymore
	content_canvas.disable_current_shader(ctx);
}

bool tf_editor_widget::create_centroid_boundaries() {
	float relative_position;
	float boundary_left;
	float boundary_right;
	float nearest_value_left;
	float nearest_value_right;

	const auto calculate_values = [&](int i, int protein_index) {

		// Get the relative position of the centroid and it's left and right boundary
		relative_position = m_shared_data_ptr->centroids.at(i).centroids[protein_index];
		boundary_left = relative_position - (m_shared_data_ptr->centroids.at(i).widths[protein_index] / 2.0f);
		boundary_right = relative_position + (m_shared_data_ptr->centroids.at(i).widths[protein_index] / 2.0f);

		// Now calculate the positions of the nearest values to the boundaries
		// Map the boundaries to the minimum and maximum protein value if they are above it
		if (boundary_left < m_min[protein_index]) {
			nearest_value_left = m_min[protein_index];
		}
		// In every other case, search for the nearest value
		else {
			nearest_value_left = utils_functions::search_nearest_boundary_value(relative_position, boundary_left, protein_index, true, data);
		}

		if (boundary_right > m_max[protein_index]) {
			nearest_value_right = m_max[protein_index];
		}
		else {
			nearest_value_right = utils_functions::search_nearest_boundary_value(relative_position, boundary_right, protein_index, false, data);
		}
	};

	if (m_create_all_values) {
		m_strip_border_points.clear();
		m_nearest_boundary_values.clear();

		for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
			// For each centroid, we want to create the lines of the boundaries
			std::vector<float> centroid_boundary_values;
			std::vector<float> centroid_nearest_values;

			// Each widget has three centroids which will always generate the same values, 
			// so skip and do only every 4th point
			for (int j = 0; j < m_points.at(i).size(); j += 3) {
				// Get the correct protein
				calculate_values(i, j / 3);

				// Store the values
				centroid_boundary_values.push_back(boundary_left);
				centroid_boundary_values.push_back(boundary_right);
				centroid_nearest_values.push_back(nearest_value_left);
				centroid_nearest_values.push_back(nearest_value_right);
			}

			int boundary_index = 0;
			std::vector<vec2> strip_coordinates;
			std::vector<utils_data_types::point> nearest_boundary_coordinates;

			// Iterate over widgets, ignore the back widget
			for (int i = 0; i < 15; i += 4) {
				for (int j = 0; j < 3; j++) {
					// Push back every two points for each widget line
					strip_coordinates.push_back(m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index)));
					strip_coordinates.push_back(m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index + 1)));

					// Apply the nearest values to points and store them
					const auto point_boundary_left = utils_data_types::point({ m_widget_lines.at(i + j).interpolate(centroid_nearest_values.at(boundary_index)),
																			   &m_widget_lines.at(i + j) });
					const auto point_boundary_right = utils_data_types::point({ m_widget_lines.at(i + j).interpolate(centroid_nearest_values.at(boundary_index + 1)),
																				&m_widget_lines.at(i + j) });
					nearest_boundary_coordinates.push_back(point_boundary_left);
					nearest_boundary_coordinates.push_back(point_boundary_right);
				}
				boundary_index += 2;
			}

			m_strip_border_points.push_back(strip_coordinates);
			m_nearest_boundary_values.push_back(nearest_boundary_coordinates);
		}
		m_create_all_values = false;

		return true;
	}
	// If a centroid is dragged in the editor, it would make no sense to redraw everything
	// So we redraw only for the centroids that were updated, if a point has been dragged
	else if (m_dragged_id_set) {
		calculate_values(m_dragged_centroid_ids[0], m_dragged_centroid_ids[1] / 3);
		for (int i = 1; i < 4; i++) {
			// Get the overall centroid and it's parent line
			const auto centroid_layer = m_dragged_centroid_ids[0];
			auto& line = m_points[centroid_layer][m_dragged_centroid_ids[i]].m_parent_line;
		
			const auto dragged_id_one = m_dragged_centroid_ids[i] * 2;
			const auto dragged_id_two = m_dragged_centroid_ids[i] * 2 + 1;
			// Update the other centroids belonging to the widget as well
			m_strip_border_points[centroid_layer][dragged_id_one] = line->interpolate(boundary_left);
			m_strip_border_points[centroid_layer][dragged_id_two] = line->interpolate(boundary_right);
			// Create points out of the newly updated centroids 
			const auto point_boundary_one = utils_data_types::point({ line->interpolate(nearest_value_left), line });
			const auto point_boundary_two = utils_data_types::point({ line->interpolate(nearest_value_right), line });

			m_nearest_boundary_values[centroid_layer][dragged_id_one] = point_boundary_one;
			m_nearest_boundary_values[centroid_layer][dragged_id_two] = point_boundary_two;
		}
		return true;
	}
	return false;
}

void tf_editor_widget::create_centroid_strips() {
	// Don't do anything if there are no points yet
	if (m_points.empty()) {
		return;
	}

	if (!m_strips_created) {
		if (!create_centroid_boundaries()) {
			return;
		}
		m_strips.clear();
		m_line_geometry_strip_borders.clear();

		int strip_index = 0;

		// Construct to add four points to the strip, because every strip is between two widgets with two points each
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

void tf_editor_widget::create_nearest_value_lines(int index) {
	// if there are no values yet, do not do anything
	if (m_nearest_boundary_values.at(index).empty()) {
		return;
	}
	m_line_geometry_nearest_values.clear();

	for (int i = 0; i < m_nearest_boundary_values.at(index).size(); i++) {
		const auto line = utils_functions::create_nearest_boundary_line(m_nearest_boundary_values.at(index).at(i));
		m_line_geometry_nearest_values.add(line.a);
		m_line_geometry_nearest_values.add(line.b);
	}
}

void tf_editor_widget::create_strip_borders(int index) {
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

void tf_editor_widget::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();

	const auto add_lines = [&](int index_row, int index_col, int pos_1, int pos_2) {
		m_points[index_row][index_col + pos_1].move_along_line(m_points[index_row][index_col].get_relative_line_position());
		m_points[index_row][index_col + pos_2].move_along_line(m_points[index_row][index_col].get_relative_line_position());
		m_dragged_centroid_ids[2] = index_col + pos_1;
		m_dragged_centroid_ids[3] = index_col + pos_2;
	};

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating centroid points in the widget have to be updated
			if (&m_points[i][j] == m_point_handles.get_dragged()) {
				m_dragged_point_ptr = &m_points[i][j];
				m_dragged_centroid_ids[0] = i;
				m_dragged_centroid_ids[1] = j;

				// Left widget point was moved, update center and right
				if (j % 3 == 0) {
					add_lines(i, j, 1, 2);
				}
				// Center widget point was moved, update left and right
				else if (j % 3 == 1) {
					add_lines(i, j, -1, 1);
				}
				// Right widget point was moved, update left and center
				else if (j % 3 == 2) {
					add_lines(i, j, -1, -2);
				}

				m_dragged_id_set = true;

				int protein_index = j / 3;
				// Remap to correct GUI vals
				const auto GUI_value = (m_points[i][j].get_relative_line_position() - 0.1f) / 0.8f;
				m_shared_data_ptr->centroids.at(i).centroids[protein_index] = GUI_value;
				update_member(&m_shared_data_ptr->centroids.at(i).centroids[protein_index]);

				was_updated = true;
			}
		}
	}

	has_damage = true;
	post_redraw();
}

void tf_editor_widget::check_mouse_click(int x, int y) {
	for (int i = 0; i < m_widget_polygons.size(); i++) {
		if (m_widget_polygons.at(i).is_point_in_polygon(x, y)) {
			break;
		}
	}
}
