#include "tf_editor_widget.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

tf_editor_widget::tf_editor_widget() {
	
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

	m_point_handles.set_drag_callback(std::bind(&tf_editor_widget::set_point_positions, this));
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
	m_line_geometry_centroid_lines.destruct(ctx);

	m_font.destruct(ctx);
	m_font_renderer.destruct(ctx);
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
			std::cout << "Nameand index: " << i << " " << m_protein_names[m_text_ids[i]] << std::endl;
			if (m_protein_names.size() > 3 && m_labels.size() > 1)
				m_labels.set_text(i, m_protein_names[m_text_ids[i]]);
			break;
		}
	}
	
	for (int i = 0; i < m_centroids.size(); ++i) {
		auto value = 0.0f;
		auto index = 0;

		for (int c_protein_i = 0; c_protein_i < 4; c_protein_i++) {
			if (member_ptr == &m_centroids.at(i).centroids[c_protein_i]) {
				value = m_centroids.at(i).centroids[c_protein_i];
				index = c_protein_i * 3;
				has_damage = true;
				break;
			}
		}
		if (has_damage) {
			m_points[i][index].pos = m_widget_lines.at(index + (index / 3)).interpolate((value * 0.8f) + 0.1f);
			m_points[i][index + 1].pos = m_widget_lines.at(index + 1 + (index / 3)).interpolate((value * 0.8f) + 0.1f);
			m_points[i][index + 2].pos = m_widget_lines.at(index + 2 + (index / 3)).interpolate((value * 0.8f) + 0.1f);
		}
		break;
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
	
	// then labels
	auto& font_prog = m_font_renderer.ref_prog();
	font_prog.enable(ctx);
	content_canvas.set_view(ctx, font_prog);
	font_prog.disable(ctx);
	m_font_renderer.render(ctx, get_overlay_size(), m_labels);

	//draw_centroid_lines(ctx, line_prog);

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
	for (int i = 0; i < m_centroids.size(); i++ ) {
		const auto header_string = "Centroid " + std::to_string(i) + " parameters:";
		add_decorator(header_string, "heading", "level=3");
		// Color widget
		add_member_control(this, "Color centroid", m_centroids.at(i).color, "", "");
		// Centroid parameters themselves
		add_member_control(this, "Centroid myosin", m_centroids.at(i).centroids[0], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid actin", m_centroids.at(i).centroids[1], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid obscurin", m_centroids.at(i).centroids[2], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid sallimus", m_centroids.at(i).centroids[3], "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		// Gaussian width
		add_member_control(this, "Gaussian width", m_centroids.at(i).gaussian_width, "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
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
	m_line_style_widgets.fill_color = color_gray;
	m_line_style_widgets.width = 3.0f;

	m_line_style_centroid_lines.use_blending = true;
	m_line_style_centroid_lines.use_fill_color = true;
	m_line_style_centroid_lines.apply_gamma = false;
	m_line_style_centroid_lines.width = 2.0f;

	m_draggable_style.position_is_center = true;
	m_draggable_style.fill_color = rgba(0.9f, 0.9f, 0.9f, 1.0f);
	m_draggable_style.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style.border_width = 1.5f;
	m_draggable_style.use_blending = true;

	m_arrow_style.head_width = 10.0f;
	m_arrow_style.absolute_head_length = 8.0f;
	m_arrow_style.stem_width = 0.0f;
	m_arrow_style.feather_width = 0.0f;
	m_arrow_style.head_length_is_relative = false;
	m_arrow_style.fill_color = color_gray;
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

	// reset previous total count and line geometry
	total_count = 0;
	m_line_geometry_relations.clear();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		// Map the vector values to a range between 0.1 and 0.9 
		// so the outmost parts of the widget lines stay clear
		const vec4& v = (data[i] * 0.8f) + 0.1f;

		// calculate the average to allow filtering with the given threshold
		const auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		if(avg > threshold) {
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

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();
	/// General line generation order: Left, center, right relations line, then a last line for closing
	// Left widget
	const vec2 x_left_0 {sizeX * 0.3f, sizeY * 0.95f};
	const vec2 x_left_1 {sizeX * 0.35f, sizeY * 0.75f};
	const vec2 x_left_2 {sizeX * 0.23f, sizeY * 0.45f};
	const vec2 x_left_3 {sizeX * 0.1f, sizeY * 0.45f};
	m_widget_lines.push_back(utils_data_types::line({x_left_0, x_left_1}));
	m_widget_lines.push_back(utils_data_types::line({x_left_1, x_left_2}));
	m_widget_lines.push_back(utils_data_types::line({x_left_2, x_left_3}));
	m_widget_lines.push_back(utils_data_types::line({x_left_3, x_left_0}));

	// Right widget
	const vec2 x_right_0{sizeX * 0.9f, sizeY * 0.45f};
	const vec2 x_right_1{sizeX * 0.77f, sizeY * 0.45f};
	const vec2 x_right_2{sizeX * 0.65f, sizeY * 0.75f};
	const vec2 x_right_3{sizeX * 0.7f, sizeY * 0.95f};
	m_widget_lines.push_back(utils_data_types::line({x_right_0, x_right_1}));
	m_widget_lines.push_back(utils_data_types::line({x_right_1, x_right_2}));
	m_widget_lines.push_back(utils_data_types::line({x_right_2, x_right_3}));
	m_widget_lines.push_back(utils_data_types::line({x_right_3, x_right_0}));

	// Bottom widget
	const vec2 x_bottom_0{sizeX * 0.23f, sizeY * 0.05f};
	const vec2 x_bottom_1{sizeX * 0.33f, sizeY * 0.25f};
	const vec2 x_bottom_2{sizeX * 0.67f, sizeY * 0.25f};
	const vec2 x_bottom_3{sizeX * 0.77f, sizeY * 0.05f};
	m_widget_lines.push_back(utils_data_types::line({x_bottom_0, x_bottom_1}));
	m_widget_lines.push_back(utils_data_types::line({x_bottom_1, x_bottom_2}));
	m_widget_lines.push_back(utils_data_types::line({x_bottom_2, x_bottom_3}));
	m_widget_lines.push_back(utils_data_types::line({x_bottom_3, x_bottom_0}));

	// Center widget, order: Left, right, bottom
	const vec2 x_center_0{sizeX * 0.4f, sizeY * 0.4f};
	const vec2 x_center_1{sizeX * 0.5f, sizeY * 0.6f};
	const vec2 x_center_2{sizeX * 0.6f, sizeY * 0.4f};
	m_widget_lines.push_back(utils_data_types::line({x_center_0, x_center_1}));
	m_widget_lines.push_back(utils_data_types::line({x_center_1, x_center_2}));
	m_widget_lines.push_back(utils_data_types::line({x_center_2, x_center_0}));

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
	utils_data_types::centroid centr;
	m_centroids.push_back(centr);
	add_centroid_draggables();

	m_point_handles.clear();
	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			m_point_handles.add(&m_points[i][j]);
		}
	}

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
		m_are_centroid_lines_created = true;
	}

	return !run;
}

void tf_editor_widget::draw_draggables(cgv::render::context& ctx) {
	// TODO: move creation of render data to own function and call only when necessary
	m_draggable_points.clear();
	ivec2 render_size;

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			const utils_data_types::point& p = m_points[i][j];
			m_draggable_points.add(p.get_render_position());
			render_size = p.get_render_size();
		}
	}

	m_draggable_points.set_out_of_date();

	shader_program& point_prog = m_point_renderer.ref_prog();
	point_prog.enable(ctx);
	content_canvas.set_view(ctx, point_prog);
	m_draggable_style.apply(ctx, point_prog);
	point_prog.set_attribute(ctx, "size", vec2(render_size));
	point_prog.disable(ctx);
	m_point_renderer.render(ctx, PT_POINTS, m_draggable_points);
}

void tf_editor_widget::draw_centroid_lines(cgv::render::context& ctx, cgv::render::shader_program& prog) {
	if (m_are_centroid_lines_created) {
		m_are_centroid_lines_created = false;
		create_centroid_boundaries();
	}
	/*
	m_line_geometry_centroid_lines.clear();

	for (int i = 0; i < m_centroid_boundaries.size(); i++) {
		m_line_style_centroid_lines.fill_color = m_centroids.at(i).color;

		for (const auto line : m_centroid_boundaries.at(i)) {
			m_line_geometry_centroid_lines.add(line.a);
			m_line_geometry_centroid_lines.add(line.b);
		}

		prog.enable(ctx);
		content_canvas.set_view(ctx, prog);
		m_line_style_centroid_lines.apply(ctx, prog);
		prog.disable(ctx);
		m_line_renderer.render(ctx, PT_LINES, m_line_geometry_centroid_lines);
	}*/
}

void tf_editor_widget::draw_arrows(cgv::render::context& ctx) {
	auto& arrow_prog = content_canvas.enable_shader(ctx, "arrow");
	m_arrow_style.apply(ctx, arrow_prog);
	
	// draw arrows on the right widget sides
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need arrows
		if ((i + 1) % 4 != 0) {
			content_canvas.draw_shape2(ctx, m_widget_lines.at(i).interpolate(0.85f), m_widget_lines.at(i).b, color_gray, color_gray);
		}
	}
	// dont forget to disable the curent shader when we don't need it anymore
	content_canvas.disable_current_shader(ctx);
}

void tf_editor_widget::create_centroid_boundaries() {
	m_centroid_boundaries.clear();

	for (int i = 0; i < m_centroids.size(); i++) {
		// For each centroid, we want to create the lines of the boundaries
		std::vector<float> centroid_boundary_values;
		// Each widget has three centroids which will always generate the same values, 
		// so skip and do only every 4th point
		for (int j = 0; j < m_points.at(i).size(); j += 3) {
			// Get the correct protein
			const int protein_index = j / 3;

			// Get the relative position of the centroid and it's left and right boundary
			const auto relative_line_position = m_centroids.at(i).centroids[protein_index];
			auto left_boundary = relative_line_position - (m_centroids.at(i).gaussian_width / 2.0f);
			auto right_boundary = relative_line_position + (m_centroids.at(i).gaussian_width / 2.0f);
			// Keep boundaries within distance
			if (left_boundary < 0.0f) {
				left_boundary = 0.0f;
			}
			if (right_boundary > 1.0f) {
				right_boundary = 1.0f;
			}

			// Store the values
			centroid_boundary_values.push_back(left_boundary);
			centroid_boundary_values.push_back(right_boundary);
		}

		std::vector<vec2> points;
		int boundary_index = 0;
		for (int i = 0; i < 12; i += 3) {
			for (int j = 0; j < 3; j++) {
				points.push_back(m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index)));
				points.push_back(m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index + 1)));
			}
			boundary_index += 2;
		}

		m_centroid_boundaries.push_back(points);
	}
}

void tf_editor_widget::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating centroid points in the widget have to be updated
			if (&m_points[i][j] == m_point_handles.get_dragged()) {
				// Left widget point was moved, update center and right
				if (j % 3 == 0) {
					m_points[i][j + 1].move_along_line(m_points[i][j].get_relative_line_position());
					m_points[i][j + 2].move_along_line(m_points[i][j].get_relative_line_position());
				}
				// Center widget point was moved, update left and right
				else if (j % 3 == 1) {
					m_points[i][j - 1].move_along_line(m_points[i][j].get_relative_line_position());
					m_points[i][j + 1].move_along_line(m_points[i][j].get_relative_line_position());
				}
				// Right widget point was moved, update left and center
				else if (j % 3 == 2) {
					m_points[i][j - 1].move_along_line(m_points[i][j].get_relative_line_position());
					m_points[i][j - 2].move_along_line(m_points[i][j].get_relative_line_position());
				}

				int protein_index = j / 3;
				// Remap to correct GUI vals
				const auto GUI_value = (m_points[i][j].get_relative_line_position() - 0.1f) / 0.8f;
				m_centroids.at(i).centroids[protein_index] = GUI_value;
				update_member(&m_centroids.at(i).centroids[protein_index]);
			}
			break;
		}
	}

	has_damage = true;
	post_redraw();
}

