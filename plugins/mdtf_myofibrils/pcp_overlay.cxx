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

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the line renderer with a shader program capable of drawing 2d lines
	m_line_renderer_relations = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_line_renderer_widgets = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
}

void pcp_overlay::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);

	m_line_renderer_relations.destruct(ctx);
	m_line_renderer_widgets.destruct(ctx);
	m_lines_relations.destruct(ctx);
	m_lines_widgets.destruct(ctx);
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

	update_member(member_ptr);
	post_redraw();
}

bool pcp_overlay::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_line_renderer_relations.init(ctx);
	success &= m_line_renderer_widgets.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

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

	// make sure to reset the color buffer if we update the content from scratch
	if(total_count == 0) {
		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// the amount of lines that will be drawn in each step
	int count = 100000;

	// make sure to not draw more lines than available
	if(total_count + count > m_lines_relations.get_vertex_count())
		count = m_lines_relations.get_vertex_count() - total_count;

	auto& line_prog_widgets = m_line_renderer_widgets.ref_prog();
	line_prog_widgets.enable(ctx);
	content_canvas.set_view(ctx, line_prog_widgets);
	line_prog_widgets.disable(ctx);
	m_line_renderer_widgets.render(ctx, PT_LINES, m_lines_widgets);

	// get the shader program of the line renderer
	auto& line_prog_relations = m_line_renderer_relations.ref_prog();
	// enable before we change anything
	line_prog_relations.enable(ctx);
	// Sets uniforms of the line shader program according to the content canvas,
	// which are needed to calculate pixel coordinates.
	content_canvas.set_view(ctx, line_prog_relations);
	// Disable, since the renderer will enable and disable its shader program automatically
	// and we cannot enable a program that is already enabled.
	line_prog_relations.disable(ctx);
	// draw the lines from the given geometry with offset and count
	m_line_renderer_relations.render(ctx, PT_LINES, m_lines_relations, total_count, count);

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	// accumulate the total amount of so-far drawn lines
	total_count += count;

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < m_lines_relations.get_vertex_count();
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

	// as the line style does not change during rendering, we can set it here once
	auto& line_prog_relations = m_line_renderer_relations.ref_prog();
	line_prog_relations.enable(ctx);
	m_line_style_relations.apply(ctx, line_prog_relations);
	line_prog_relations.disable(ctx);

	auto& line_prog_widgets = m_line_renderer_widgets.ref_prog();
	line_prog_widgets.enable(ctx);
	m_line_style_widgets.apply(ctx, line_prog_widgets);
	line_prog_widgets.disable(ctx);

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

void pcp_overlay::update_content() {
	
	if(data.empty())
		return;

	// reset previous total count and line geometry
	total_count = 0;
	m_lines_relations.clear();
	m_lines_widgets.clear();

	addWidgets();

	// setup plot origin and sizes
	vec2 org = static_cast<vec2>(domain.pos());
	float h = domain.size().y();
	float step = static_cast<float>(domain.size().x()) / 3.0f;

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		const vec4& v = data[i];

		// calculate the average to allow filtering with the given threshold
		float avg = v[0] + v[1] + v[2] + v[3];
		avg *= 0.25f;

		if(avg > threshold) {
			// scale the values from [0,1] to the plot height
			vec4 scaled = v * h + org.y();

			// add a total of 3 lines, connecting the 4 parallel coordinate axes
			m_lines_relations.add(vec2(org.x() + 0 * step, scaled[0]));
			m_lines_relations.add(vec2(org.x() + 1 * step, scaled[1]));

			m_lines_relations.add(vec2(org.x() + 1 * step, scaled[1]));
			m_lines_relations.add(vec2(org.x() + 2 * step, scaled[2]));

			m_lines_relations.add(vec2(org.x() + 2 * step, scaled[2]));
			m_lines_relations.add(vec2(org.x() + 3 * step, scaled[3]));
		}
	}

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}

void pcp_overlay::initWidgets() {
	m_stored_widget_lines.clear();

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();
	/// General order: Left, centered and right line for relations, then closing line
	// Left widget
	const ivec2 x_left_0 {static_cast<int32_t>(sizeX * 0.3f), static_cast<int32_t>(sizeY * 0.95f)};
	const ivec2 x_left_1 {static_cast<int32_t>(sizeX * 0.35f), static_cast<int32_t>(sizeY * 0.75f)};
	const ivec2 x_left_2 {static_cast<int32_t>(sizeX * 0.23f), static_cast<int32_t>(sizeY * 0.45f)};
	const ivec2 x_left_3 {static_cast<int32_t>(sizeX * 0.1f), static_cast<int32_t>(sizeY * 0.45f)};
	m_stored_widget_lines.push_back(line({x_left_0, x_left_1}));
	m_stored_widget_lines.push_back(line({x_left_1, x_left_2}));
	m_stored_widget_lines.push_back(line({x_left_2, x_left_3}));
	m_stored_widget_lines.push_back(line({x_left_3, x_left_0}));

	// Right widget
	const ivec2 x_right_0{static_cast<int32_t>(sizeX * 0.7f), static_cast<int32_t>(sizeY * 0.95f)};
	const ivec2 x_right_1{static_cast<int32_t>(sizeX * 0.65f), static_cast<int32_t>(sizeY * 0.75f)};
	const ivec2 x_right_2{static_cast<int32_t>(sizeX * 0.77f), static_cast<int32_t>(sizeY * 0.45f)};
	const ivec2 x_right_3{static_cast<int32_t>(sizeX * 0.9f), static_cast<int32_t>(sizeY * 0.45f)};
	m_stored_widget_lines.push_back(line({x_right_0, x_right_1}));
	m_stored_widget_lines.push_back(line({x_right_1, x_right_2}));
	m_stored_widget_lines.push_back(line({x_right_2, x_right_3}));
	m_stored_widget_lines.push_back(line({x_right_3, x_right_0}));

	// Right widget
	const ivec2 x_bottom_0{static_cast<int32_t>(sizeX * 0.23f), static_cast<int32_t>(sizeY * 0.05f)};
	const ivec2 x_bottom_1{static_cast<int32_t>(sizeX * 0.33f), static_cast<int32_t>(sizeY * 0.25f)};
	const ivec2 x_bottom_2{static_cast<int32_t>(sizeX * 0.67f), static_cast<int32_t>(sizeY * 0.25f)};
	const ivec2 x_bottom_3{static_cast<int32_t>(sizeX * 0.77f), static_cast<int32_t>(sizeY * 0.05f)};
	m_stored_widget_lines.push_back(line({x_bottom_0, x_bottom_1}));
	m_stored_widget_lines.push_back(line({x_bottom_1, x_bottom_2}));
	m_stored_widget_lines.push_back(line({x_bottom_2, x_bottom_3}));
	m_stored_widget_lines.push_back(line({x_bottom_3, x_bottom_0}));

	// Center widget, order: Left, right, bottom
	const ivec2 x_center_0{static_cast<int32_t>(sizeX * 0.4f), static_cast<int32_t>(sizeY * 0.4f)};
	const ivec2 x_center_1{static_cast<int32_t>(sizeX * 0.5f), static_cast<int32_t>(sizeY * 0.6f)};
	const ivec2 x_center_2{static_cast<int32_t>(sizeX * 0.6f), static_cast<int32_t>(sizeY * 0.4f)};
	m_stored_widget_lines.push_back(line({x_center_0, x_center_1}));
	m_stored_widget_lines.push_back(line({x_center_1, x_center_2}));
	m_stored_widget_lines.push_back(line({x_center_2, x_center_0}));
}


void pcp_overlay::addWidgets() {
	for (const auto l : m_stored_widget_lines) {
		m_lines_widgets.add(l.a);
		m_lines_widgets.add(l.b);
	}
}