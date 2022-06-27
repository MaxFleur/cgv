#include "sp_overlay.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>
#include <cgv_glutil/color_map.h>

sp_overlay::sp_overlay() {
	
	set_name("PCP Overlay");
	// prevent the mouse events from reaching throug this overlay to the underlying elements
	block_events = true;

	// setup positioning and size
	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(-3));
	set_overlay_size(ivec2(700, 700));
	
	// add a color attachment to the content frame buffer with support for transparency (alpha)
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	// change its size to be the same as the overlay
	fbc.set_size(get_overlay_size());

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the point renderer with a shader program capable of drawing 2d circles
	point_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);

	m_line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
}

void sp_overlay::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);

	point_renderer.destruct(ctx);
	points.destruct(ctx);

	m_line_renderer.destruct(ctx);

	font.destruct(ctx);
	font_renderer.destruct(ctx);
}

bool sp_overlay::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool sp_overlay::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	return false;
}

void sp_overlay::on_set(void* member_ptr) {

	// react to changes of the point alpha parameter and update the styles
	if(member_ptr == &alpha || member_ptr == &blur) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	if(member_ptr == &x_idx) {
		x_idx = cgv::math::clamp(x_idx, 0, 3);
		if(names.size() > 3 && labels.size() > 1)
			labels.set_text(0, names[x_idx]);
	}

	if(member_ptr == &y_idx) {
		y_idx = cgv::math::clamp(y_idx, 0, 3);
		if(names.size() > 3 && labels.size() > 1)
			labels.set_text(1, names[y_idx]);
	}

	update_member(member_ptr);
	post_redraw();
}

bool sp_overlay::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and point renderer
	success &= fbc.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= point_renderer.init(ctx);
	success &= font_renderer.init(ctx);
	success &= m_line_renderer.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if(font.init(ctx)) {
		labels.set_msdf_font(&font);
		labels.set_font_size(font_size);
	}

	return success;
}

void sp_overlay::init_frame(cgv::render::context& ctx) {

	// react to changes in the overlay size
	if(ensure_overlay_layout(ctx)) {
		ivec2 overlay_size = get_overlay_size();
		
		// calculate the new domain size
		int label_space = 20;
		domain.set_pos(ivec2(13) + label_space); // 13 pixel padding (inner space from border) + 20 pixel space for labels
		domain.set_size(overlay_size - 2 * ivec2(13) - label_space); // scale size to fit in leftover inner space

		// udpate the offline frame buffer to the new size
		fbc.set_size(overlay_size);
		fbc.ensure(ctx);

		// set resolutions of the canvases
		content_canvas.set_resolution(ctx, overlay_size);
		viewport_canvas.set_resolution(ctx, get_viewport_size());

		if(font.is_initialized()) {
			labels.clear();

			const auto x = domain.pos().x() + 100;
			const auto y = domain.pos().y() - label_space / 2;

			labels.add_text("Myosin", ivec2(domain.box.get_center().x() * 0.35f, y), cgv::render::TA_NONE);
			labels.add_text("Salimus", ivec2(domain.box.get_center().x(), y), cgv::render::TA_NONE);
			labels.add_text("Obscurin", ivec2(domain.box.get_center().x() * 1.60f, y), cgv::render::TA_NONE);

			labels.add_text("Actin", ivec2(x, domain.box.get_center().y() * 0.35f), cgv::render::TA_NONE);
			labels.add_text("Obscurin", ivec2(x, domain.box.get_center().y()), cgv::render::TA_NONE);
			labels.add_text("Salimus", ivec2(x, domain.box.get_center().y() * 1.65f), cgv::render::TA_NONE);
		}
	}
}

void sp_overlay::draw(cgv::render::context& ctx) {

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

void sp_overlay::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	
	// enable the offline frame buffer, so all things are drawn into its attached textures
	fbc.enable(ctx);

	// make sure to reset the color buffer if we update the content from scratch
	if(total_count == 0) {
		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// also draw the plot domain frame...
		content_canvas.enable_shader(ctx, "rectangle"); // enabling the shader via the canvas will directly set the view uniforms
		content_canvas.draw_shape(ctx, domain.pos(), domain.size());
		content_canvas.disable_current_shader(ctx);

		// ...and axis labels
		// this is pretty much the same as for the generic renderer
		auto& font_prog = font_renderer.ref_prog();
		font_prog.enable(ctx);
		content_canvas.set_view(ctx, font_prog);
		font_prog.disable(ctx);
		// draw the first label only
		font_renderer.render(ctx, get_overlay_size(), labels, 0, 1);
		font_renderer.render(ctx, get_overlay_size(), labels, 1, 1);
		font_renderer.render(ctx, get_overlay_size(), labels, 2, 1);

		// save the current view matrix
		content_canvas.push_modelview_matrix();
		// Rotate the view, so the second label is drawn sideways.
		// Objects are rotated around the origin, so we first need to move the text to the origin.
		// Transformations are applied in reverse order:
		//  3 - move text to origin
		//  2 - rotate 90 degrees
		//  1 - move text back to its position
		content_canvas.mul_modelview_matrix(ctx, cgv::math::translate2h(labels.ref_texts()[1].position));	// 1
		content_canvas.mul_modelview_matrix(ctx, cgv::math::rotate2h(90.0f));								// 2
		content_canvas.mul_modelview_matrix(ctx, cgv::math::translate2h(-labels.ref_texts()[1].position));	// 3

		// now render the second label
		font_prog.enable(ctx);
		content_canvas.set_view(ctx, font_prog);
		font_prog.disable(ctx);
		// font_renderer.render(ctx, get_overlay_size(), labels, 3, 1);
		// font_renderer.render(ctx, get_overlay_size(), labels, 4, 1);
		// font_renderer.render(ctx, get_overlay_size(), labels, 5, 1);

		// restore the previous view matrix
		content_canvas.pop_modelview_matrix(ctx);
	}

	create_grid_lines();
	add_grid_lines();

	// draw the grid lines
	auto& line_prog = m_line_renderer.ref_prog();
	line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_line_style_grid.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_line_renderer.render(ctx, PT_LINES, m_line_geometry_grid);

	// the amount of points that will be drawn in each step
	int count = 50000;

	// make sure to not draw more points than available
	if(total_count + count > points.get_render_count())
		count = points.get_render_count() - total_count;

	// get the shader program of the point renderer
	auto& point_prog = point_renderer.ref_prog();
	// enable before we change anything
	point_prog.enable(ctx);
	// Sets uniforms of the point shader program according to the content canvas,
	// which are needed to calculate pixel coordinates.
	content_canvas.set_view(ctx, point_prog);

	point_prog.set_attribute(ctx, "size", vec2(radius));

	// Disable, since the renderer will enable and disable its shader program automatically
	// and we cannot enable a program that is already enabled.
	point_prog.disable(ctx);
	// draw the points from the given geometry with offset and count
	point_renderer.render(ctx, PT_POINTS, points, total_count, count);

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	// accumulate the total amount of so-far drawn points
	total_count += count;

	// Stop the process if we have drawn all available points,
	// otherwise request drawing of another frame.
	bool run = total_count < points.get_render_count();
	if(run)
		post_redraw();
	else
		std::cout << "done" << std::endl;

	has_damage = run;
}

void sp_overlay::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &sp_overlay::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0;max=1;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Alpha", alpha, "value_slider", "min=0;max=1;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Blur", blur, "value_slider", "min=0;max=20;step=0.0001;ticks=true");
	add_member_control(this, "Radius", radius, "value_slider", "min=0;max=10;step=0.0001;ticks=true");

	add_decorator("Stain Indices", "heading", "level=3;font_style=regular");
	add_decorator("", "separator", "h=2");
	add_member_control(this, "X Index", x_idx, "value", "min=0;max=3;step=1");
	add_member_control(this, "Y Index", y_idx, "value", "min=0;max=3;step=1");
}

void sp_overlay::init_styles(cgv::render::context& ctx) {

	// configure style for the plot frame
	cgv::glutil::shape2d_style frame_style;
	frame_style.border_color = rgba(rgb(0.0f), 1.0f);
	frame_style.border_width = 1.0f;
	frame_style.feather_width = 0.0f;
	frame_style.apply_gamma = false;

	auto& frame_prog = content_canvas.enable_shader(ctx, "rectangle");
	frame_style.apply(ctx, frame_prog);
	content_canvas.disable_current_shader(ctx);

	// configure style for the plot points
	cgv::glutil::shape2d_style point_style;
	point_style.use_blending = true;
	point_style.use_fill_color = false;
	point_style.apply_gamma = false;
	point_style.position_is_center = true;
	point_style.feather_width = blur;

	m_line_style_grid.use_blending = true;
	m_line_style_grid.use_fill_color = true;
	m_line_style_grid.apply_gamma = false;
	m_line_style_grid.fill_color = m_color_gray;
	m_line_style_grid.width = 3.0f;

	// as the point style does not change during rendering, we can set it here once
	auto& point_prog = point_renderer.ref_prog();
	point_prog.enable(ctx);
	point_style.apply(ctx, point_prog);
	point_prog.disable(ctx);

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

	// also does not chnage, so is only set whenever this method is called
	auto& overlay_prog = viewport_canvas.enable_shader(ctx, "rectangle");
	overlay_style.apply(ctx, overlay_prog);
	viewport_canvas.disable_current_shader(ctx);
}

void sp_overlay::update_content() {

	if(data.empty())
		return;

	// reset previous total count and point geometry
	total_count = 0;
	points.clear();

	// setup plot origin and sizes
	vec2 org = static_cast<vec2>(domain.pos());
	vec2 size = domain.size();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		const vec4& v = data[i];

		// calculate the average to allow filtering with the given threshold
		float avg = v[0] + v[1] + v[2] + v[3];
		avg *= 0.25f;

		if(avg > threshold) {
			// Draw the points for each protein
			// First column
			float offset = 0.0f;

			// Myosin - Actin, Myosin - Obscurin, Myosin - Salimus
			for (int i = 1; i < 4; i++) {
				vec2 pos(v[0], v[i]);

				const auto x = pos.x();
				const auto y = pos.y();

				pos.set(x * 0.33f, (y / 3.0f) + offset);
				pos *= size;
				pos += org;

				// add one point
				points.add(pos, rgba(rgb(0.0f), alpha));

				offset += 0.33f;
			}
			// Second col
			// Salimus - Actin, Salimus - Obscurin
			offset = 0.0f;
			for (int i = 1; i < 3; i++) {
				vec2 pos(v[3], v[i]);

				const auto x = pos.x();
				const auto y = pos.y();

				pos.set((x * 0.33f) + 0.33f, (y / 3.0f) + offset);
				pos *= size;
				pos += org;

				// add one point
				points.add(pos, rgba(rgb(0.0f), alpha));

				offset += 0.33f;
			}
			// Obscurin - Actin
			vec2 pos(v[2], v[1]);

			const auto x = pos.x();
			const auto y = pos.y();

			pos.set((x * 0.33f) + 0.66f, (y / 3.0f));
			pos *= size;
			pos += org;

			// add one point
			points.add(pos, rgba(rgb(0.0f), alpha));

			offset += 0.33f;
		}
	}

	std::cout << "Drawing " << points.get_render_count() << " points" << std::endl;

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}

void sp_overlay::create_grid_lines() {
	m_lines_grid.clear();

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();

	// Coordinates for the most left and down line
	vec2 horiz_left{ sizeX * 0.05f, sizeY * 0.05f };
	vec2 horiz_right{ sizeX * 1.05f, sizeY * 0.05f };
	vec2 vert_down{ sizeX * 0.05f, sizeY * 0.05f };
	vec2 vert_up{ sizeX * 0.05f, sizeY * 1.05f };
	m_lines_grid.push_back(utils_data_types::line({ horiz_left, horiz_right }));
	m_lines_grid.push_back(utils_data_types::line({ vert_down, vert_up }));

	// Modify for all following positions
	horiz_left.set(sizeX * 0.38f, sizeY * 0.05f);
	horiz_right.set(sizeX * 0.38f, sizeY * 1.05f);
	vert_down.set(sizeX * 0.05f, sizeY * 0.38f);
	vert_up.set(sizeX * 1.05f, sizeY * 0.38f);
	m_lines_grid.push_back(utils_data_types::line({ horiz_left, horiz_right }));
	m_lines_grid.push_back(utils_data_types::line({ vert_down, vert_up }));

	horiz_left.set(sizeX * 0.05f, sizeY * 0.71f);
	horiz_right.set(sizeX * 0.71f, sizeY * 0.71f);
	vert_down.set(sizeX * 0.71f, sizeY * 0.05f);
	vert_up.set(sizeX * 0.71f, sizeY * 0.71f);
	m_lines_grid.push_back(utils_data_types::line({ horiz_left, horiz_right }));
	m_lines_grid.push_back(utils_data_types::line({ vert_down, vert_up }));

	horiz_left.set(sizeX * 0.05f, sizeY * 1.05f);
	horiz_right.set(sizeX * 0.38f, sizeY * 1.05f);
	vert_down.set(sizeX * 1.05f, sizeY * 0.05f);
	vert_up.set(sizeX * 1.05f, sizeY * 0.38f);
	m_lines_grid.push_back(utils_data_types::line({ horiz_left, horiz_right }));
	m_lines_grid.push_back(utils_data_types::line({ vert_down, vert_up }));
}

void sp_overlay::add_grid_lines() {
	m_line_geometry_grid.clear();

	for (const auto l : m_lines_grid) {
		m_line_geometry_grid.add(l.a);
		m_line_geometry_grid.add(l.b);
	}
}
