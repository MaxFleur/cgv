#include "pcp_overlay.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

pcp_overlay::pcp_overlay() {
	
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

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main
	// frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the line renderer with a shader program capable of drawing 2d lines
	m_line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
}

void pcp_overlay::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);

	m_line_renderer.destruct(ctx);
	m_line_geometry_relations.destruct(ctx);
	m_line_geometry_widgets.destruct(ctx);

	font.destruct(ctx);
	font_renderer.destruct(ctx);
}

bool pcp_overlay::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool pcp_overlay::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	return false;
}

void pcp_overlay::on_set(void* member_ptr) {

	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &line_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	// change the labels if the GUI index is updated
	if (member_ptr == &m_id_left) {
		m_id_left = cgv::math::clamp(m_id_left, 0, 3);
		if (m_protein_names.size() > 3 && labels.size() > 1) 
			labels.set_text(0, m_protein_names[m_id_left]);
	}
	if (member_ptr == &m_id_right) {
		m_id_right = cgv::math::clamp(m_id_right, 0, 3);
		if (m_protein_names.size() > 3 && labels.size() > 1)
			labels.set_text(1, m_protein_names[m_id_right]);
	}
	if (member_ptr == &m_id_bottom) {
		m_id_bottom = cgv::math::clamp(m_id_bottom, 0, 3);
		if (m_protein_names.size() > 3 && labels.size() > 1)
			labels.set_text(2, m_protein_names[m_id_bottom]);
	}
	if (member_ptr == &m_id_center) {
		m_id_center = cgv::math::clamp(m_id_center, 0, 3);
		if (m_protein_names.size() > 3 && labels.size() > 1)
			labels.set_text(3, m_protein_names[m_id_center]);
	}

	update_member(member_ptr);
	post_redraw();
}

bool pcp_overlay::init(cgv::render::context& ctx) {

	bool success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_line_renderer.init(ctx);
	success &= font_renderer.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if (success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if (font.init(ctx)) {
		labels.set_msdf_font(&font);
		labels.set_font_size(font_size);
	}

	return success;
}

void pcp_overlay::init_frame(cgv::render::context& ctx) {

	// react to changes in the overlay size
	if(ensure_overlay_layout(ctx)) {
		ivec2 overlay_size = get_overlay_size();
		
		// calculate the new domain size
		domain.set_pos(ivec2(13)); // 13 pixel padding (inner space from border)
		domain.set_size(overlay_size - 2 * ivec2(13)); // scale size to fit in inner space left over by padding

		// udpate the offline frame buffer to the new size
		fbc.set_size(overlay_size);
		fbc.ensure(ctx);

		// set resolutions of the canvases
		content_canvas.set_resolution(ctx, overlay_size);
		viewport_canvas.set_resolution(ctx, get_viewport_size());

		initWidgets();

		if (font.is_initialized()) {
			labels.clear();

			labels.add_text("0", ivec2(domain.size().x() * 0.27f, domain.size().y() * 0.70f), cgv::render::TA_NONE);
			labels.add_text("1", ivec2(domain.size().x() * 0.73f, domain.size().y() * 0.70f), cgv::render::TA_NONE);
			labels.add_text("2", ivec2(domain.size().x() * 0.5f, domain.size().y() * 0.18f), cgv::render::TA_NONE);
			labels.add_text("3", ivec2(domain.size().x() * 0.5f, domain.size().y() * 0.48f), cgv::render::TA_NONE);

			on_set(&m_id_left);
			on_set(&m_id_right);
			on_set(&m_id_bottom);
			on_set(&m_id_center);
		}
	}
}

void pcp_overlay::draw(cgv::render::context& ctx) {

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

void pcp_overlay::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	
	// enable the offline frame buffer, so all things are drawn into its attached textures
	fbc.enable(ctx);

	auto& line_prog = m_line_renderer.ref_prog();

	// make sure to reset the color buffer if we update the content from scratch
	if(total_count == 0) {
		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		auto& font_prog = font_renderer.ref_prog();
		font_prog.enable(ctx);
		content_canvas.set_view(ctx, font_prog);
		font_prog.disable(ctx);
		for (int i = 0; i < labels.size(); i++) {
			font_renderer.render(ctx, get_overlay_size(), labels, i, 1);
		}

		line_prog.enable(ctx);
		content_canvas.set_view(ctx, line_prog);
		m_line_style_widgets.apply(ctx, line_prog);
		line_prog.disable(ctx);
		m_line_renderer.render(ctx, PT_LINES, m_line_geometry_widgets);
	}

	// the amount of lines that will be drawn in each step
	int count = 100000;

	// make sure to not draw more lines than available
	if (total_count + count > m_line_geometry_relations.get_vertex_count())
		count = m_line_geometry_relations.get_vertex_count() - total_count;

	line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_line_style_relations.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_line_renderer.render(ctx, PT_LINES, m_line_geometry_relations, total_count, count);

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	// accumulate the total amount of so-far drawn lines
	total_count += count;

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < m_line_geometry_relations.get_vertex_count();
	if(run)
		post_redraw();
	else
		std::cout << "done" << std::endl;

	has_damage = run;
}

void pcp_overlay::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &pcp_overlay::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Line Alpha", line_alpha, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Protein left:", m_id_left, "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein right:", m_id_right, "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein bottom:", m_id_bottom, "value", "min=0;max=3;step=1");
	add_member_control(this, "Protein center:", m_id_center, "value", "min=0;max=3;step=1");

	auto const add_centroid_button = add_button("Add centroid");
	connect_copy(add_centroid_button->click, rebind(this, &pcp_overlay::addCentroid));

	for (int i = 0; i < m_centroids.size(); i++ ) {
		const auto header_string = "Centroid " + std::to_string(i) + " parameters:";
		add_decorator(header_string, "heading", "level=3");

		add_member_control(this, "Centroid myosin", m_centroids.at(i).centr_myosin, "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid actin", m_centroids.at(i).centr_actin, "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid obscurin", m_centroids.at(i).centr_obscurin, "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");
		add_member_control(this, "Centroid sallimus", m_centroids.at(i).centr_sallimus, "value_slider",
						   "min=0.0;max=1.0;step=0.0001;ticks=true");

		const auto width_string = "Gaussian width centroid " + std::to_string(i);
		add_decorator(width_string, "heading", "level=3");
		add_member_control(this, "", m_centroids.at(i).gaussian_width, "value_slider", "min=0.0;max=1.0;step=0.0001;ticks=true");
	}
}

void pcp_overlay::init_styles(cgv::render::context& ctx) {

	m_line_style_relations.use_blending = true;
	m_line_style_relations.use_fill_color = true;
	m_line_style_relations.apply_gamma = false;
	m_line_style_relations.fill_color = rgba(rgb(0.0f), line_alpha);
	m_line_style_relations.width = 1.0f;

	m_line_style_widgets.use_blending = true;
	m_line_style_widgets.use_fill_color = true;
	m_line_style_widgets.apply_gamma = false;
	m_line_style_widgets.fill_color = rgba(1.0f, 0.0f, 0.0f, 1.0f);
	m_line_style_widgets.width = 3.0f;

	// configure style for the plot labels
	cgv::glutil::shape2d_style text_style;
	text_style.fill_color = rgba(rgb(0.0f), 1.0f);
	text_style.border_color.alpha() = 0.0f;
	text_style.border_width = 0.333f;
	text_style.use_blending = true;
	text_style.apply_gamma = false;

	auto& font_prog = font_renderer.ref_prog();
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

void pcp_overlay::update_content() {
	
	if(data.empty())
		return;

	// reset previous total count and line geometry
	total_count = 0;
	m_line_geometry_relations.clear();
	m_line_geometry_widgets.clear();

	addWidgets();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		// Map the vector values to a range between 0.1 and 0.9 
		// so the outmost parts of the widget lines stay clear
		const vec4& v = (data[i] * 0.8f) + 0.1f;

		// calculate the average to allow filtering with the given threshold
		const auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		if(avg > threshold) {
			// Left to right
			m_line_geometry_relations.add(m_widget_lines.at(0).interpolate(v[m_id_left]));
			m_line_geometry_relations.add(m_widget_lines.at(6).interpolate(v[m_id_right]));
			// Left to center
			m_line_geometry_relations.add(m_widget_lines.at(1).interpolate(v[m_id_left]));
			m_line_geometry_relations.add(m_widget_lines.at(12).interpolate(v[m_id_center]));
			// Left to bottom
			m_line_geometry_relations.add(m_widget_lines.at(2).interpolate(v[m_id_left]));
			m_line_geometry_relations.add(m_widget_lines.at(8).interpolate(v[m_id_bottom]));
			// Right to bottom
			m_line_geometry_relations.add(m_widget_lines.at(4).interpolate(v[m_id_right]));
			m_line_geometry_relations.add(m_widget_lines.at(10).interpolate(v[m_id_bottom]));
			// Right to center
			m_line_geometry_relations.add(m_widget_lines.at(5).interpolate(v[m_id_right]));
			m_line_geometry_relations.add(m_widget_lines.at(13).interpolate(v[m_id_center]));
			// Bottom to center
			m_line_geometry_relations.add(m_widget_lines.at(9).interpolate(v[m_id_bottom]));
			m_line_geometry_relations.add(m_widget_lines.at(14).interpolate(v[m_id_center]));
		}
	}

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}

void pcp_overlay::initWidgets() {
	m_widget_lines.clear();

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();
	/// General line generation order: Left, center, right relations line, then a last line for closing
	// Left widget
	const vec2 x_left_0 {sizeX * 0.3f, sizeY * 0.95f};
	const vec2 x_left_1 {sizeX * 0.35f, sizeY * 0.75f};
	const vec2 x_left_2 {sizeX * 0.23f, sizeY * 0.45f};
	const vec2 x_left_3 {sizeX * 0.1f, sizeY * 0.45f};
	m_widget_lines.push_back(line({x_left_0, x_left_1}));
	m_widget_lines.push_back(line({x_left_1, x_left_2}));
	m_widget_lines.push_back(line({x_left_2, x_left_3}));
	m_widget_lines.push_back(line({x_left_3, x_left_0}));

	// Right widget
	const vec2 x_right_0{sizeX * 0.9f, sizeY * 0.45f};
	const vec2 x_right_1{sizeX * 0.77f, sizeY * 0.45f};
	const vec2 x_right_2{sizeX * 0.65f, sizeY * 0.75f};
	const vec2 x_right_3{sizeX * 0.7f, sizeY * 0.95f};
	m_widget_lines.push_back(line({x_right_0, x_right_1}));
	m_widget_lines.push_back(line({x_right_1, x_right_2}));
	m_widget_lines.push_back(line({x_right_2, x_right_3}));
	m_widget_lines.push_back(line({x_right_3, x_right_0}));

	// Bottom widget
	const vec2 x_bottom_0{sizeX * 0.23f, sizeY * 0.05f};
	const vec2 x_bottom_1{sizeX * 0.33f, sizeY * 0.25f};
	const vec2 x_bottom_2{sizeX * 0.67f, sizeY * 0.25f};
	const vec2 x_bottom_3{sizeX * 0.77f, sizeY * 0.05f};
	m_widget_lines.push_back(line({x_bottom_0, x_bottom_1}));
	m_widget_lines.push_back(line({x_bottom_1, x_bottom_2}));
	m_widget_lines.push_back(line({x_bottom_2, x_bottom_3}));
	m_widget_lines.push_back(line({x_bottom_3, x_bottom_0}));

	// Center widget, order: Left, right, bottom
	const vec2 x_center_0{sizeX * 0.4f, sizeY * 0.4f};
	const vec2 x_center_1{sizeX * 0.5f, sizeY * 0.6f};
	const vec2 x_center_2{sizeX * 0.6f, sizeY * 0.4f};
	m_widget_lines.push_back(line({x_center_0, x_center_1}));
	m_widget_lines.push_back(line({x_center_1, x_center_2}));
	m_widget_lines.push_back(line({x_center_2, x_center_0}));

	// draw smaller boundaries on the relations borders
	for (int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need boundaries
		if ((i + 1) % 4 != 0) {
			const auto direction = normalize(m_widget_lines.at(i).b - m_widget_lines.at(i).a);
			const auto ortho_direction = cgv::math::ortho(direction);

			const auto boundary_left = m_widget_lines.at(i).interpolate(0.1f);
			const auto boundary_right = m_widget_lines.at(i).interpolate(0.9f);

			m_widget_lines.push_back(
				line({boundary_left - 5.0f * ortho_direction, boundary_left + 3.0f * ortho_direction}));
			m_widget_lines.push_back(
				  line({boundary_right - 5.0f * ortho_direction, boundary_right + 3.0f * ortho_direction}));
		}
	}

}

void pcp_overlay::addWidgets() {
	m_line_geometry_widgets.clear();

	for (const auto l : m_widget_lines) {
		m_line_geometry_widgets.add(l.a);
		m_line_geometry_widgets.add(l.b);
	}
}