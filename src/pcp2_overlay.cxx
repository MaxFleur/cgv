#include "pcp2_overlay.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

pcp2_overlay::pcp2_overlay() {
	
	set_name("PCP 2 Overlay");
	// prevent the mouse events from reaching throug this overlay to the underlying elements
	block_events = true;

	// setup positioning and size
	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(-3));
	set_overlay_size(ivec2(600, 200));

	// add a color attachment to the content frame buffer with support for transparency (alpha)
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	// change its size to be the same as the overlay
	fbc.set_size(get_overlay_size());
	
	// also add a color attachment to the plot frame buffer with support for transparency (alpha)
	pcp_fbc.add_attachment("color", "flt32[R,G,B,A]");
	// initialize its size to the overlay size (will be resized to the plot doamin later on)
	pcp_fbc.set_size(get_overlay_size());

	// register a custom mapping shader or the content canvas that is used to map the accumulated counts to actual colors
	content_canvas.register_shader("map", "map.glpr");

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);
	
	// initialize the line renderer with a shader program capable of drawing 2d lines
	line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
}

void pcp2_overlay::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	pcp_canvas.destruct(ctx);
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);
	pcp_fbc.clear(ctx);

	line_renderer.destruct(ctx);
	lines.destruct(ctx);
}

bool pcp2_overlay::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool pcp2_overlay::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	return false;
}

void pcp2_overlay::on_set(void* member_ptr) {

	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &line_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	update_member(member_ptr);
	post_redraw();
}

bool pcp2_overlay::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= pcp_fbc.ensure(ctx);
	success &= pcp_canvas.init(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= line_renderer.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	return success;
}

void pcp2_overlay::init_frame(cgv::render::context& ctx) {

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
		pcp_fbc.set_size(domain.size());
		pcp_fbc.ensure(ctx);

		// set resolutions of the canvases
		pcp_canvas.set_resolution(ctx, domain.size());
		content_canvas.set_resolution(ctx, overlay_size);
		viewport_canvas.set_resolution(ctx, get_viewport_size());
	}
}

void pcp2_overlay::draw(cgv::render::context& ctx) {

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

void pcp2_overlay::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// This time we use a simple blend function taht will only add the two color values together.
	// This way, all lines will get accumulated in the buffer, where each pixel has a count representing
	// the total number of lines that cover this pixel.
	glBlendFunc(GL_ONE, GL_ONE);

	// first draw into the seperate plot frame buffer
	pcp_fbc.enable(ctx);

	// make sure to reset the color buffer if we update the content from scratch
	if(total_count == 0) {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// the amount of lines that will be drawn in each step
	int count = 100000;

	// make sure to not draw more lines than available
	if(total_count + count > lines.get_render_count())
		count = lines.get_render_count() - total_count;

	// get the shader program of the line renderer
	auto& line_prog = line_renderer.ref_prog();
	// enable before we change anything
	line_prog.enable(ctx);
	// this time, we set the view parameters of the plot canvas
	pcp_canvas.set_view(ctx, line_prog);
	// Disable, since the renderer will enable and disable its shader program automatically
	// and we cannot enable a program that is already enabled.
	line_prog.disable(ctx);
	// draw the lines from the given geometry with offset and count
	line_renderer.render(ctx, PT_LINES, lines, total_count, count);

	// disable the plot frame buffer
	pcp_fbc.disable(ctx);

	// change the blending function to the usual one
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// enable the overlay frame buffer
	fbc.enable(ctx);

	// clear the content
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// enable the plot freame buffer as a texture
	pcp_fbc.enable_attachment(ctx, "color", 0);
	// enable our special mapping shader, whcih will draw a rectangle, read our texture and map it to a color
	auto& map_prog = content_canvas.enable_shader(ctx, "map");
	// set the additional uniforms
	map_prog.set_uniform(ctx, "normalize_count", normalize_count);
	map_prog.set_uniform(ctx, "exponent", exponent);
	// draw the shape
	content_canvas.draw_shape(ctx, domain.pos(), domain.size());
	content_canvas.disable_current_shader(ctx);
	pcp_fbc.disable_attachment(ctx, "color");

	// disable the overlay content frame buffer
	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	// accumulate the total amount of so-far drawn lines
	total_count += count;

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < lines.get_render_count();
	if(run)
		post_redraw();
	else
		std::cout << "done" << std::endl;

	has_damage = run;
}

void pcp2_overlay::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &pcp2_overlay::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Line Alpha", line_alpha, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Normalize Count", normalize_count, "value_slider", "min=1.0;max=1000000.0;step=1.0;log=true;ticks=true");
	add_member_control(this, "Exponent", exponent, "value_slider", "min=0.0;max=5.0;step=0.00001;log=true;ticks=true");
}

void pcp2_overlay::init_styles(cgv::render::context& ctx) {

	// configure style for the pcp lines
	cgv::glutil::line2d_style line_style;
	line_style.use_blending = true;
	line_style.use_fill_color = true;
	line_style.apply_gamma = false;
	line_style.fill_color = rgba(rgb(0.0f), line_alpha);
	line_style.width = 1.0f;

	// as the line style does not change during rendering, we can set it here once
	auto& line_prog = line_renderer.ref_prog();
	line_prog.enable(ctx);
	line_style.apply(ctx, line_prog);
	line_prog.disable(ctx);

	// configure style for mapping aggregated count values to color
	cgv::glutil::shape2d_style mapping_style;
	mapping_style.apply_gamma = false;
	mapping_style.feather_width = 0.0f;

	auto& mapping_prog = content_canvas.enable_shader(ctx, "map");
	mapping_style.apply(ctx, mapping_prog);
	content_canvas.disable_current_shader(ctx);

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

void pcp2_overlay::update_content() {
	
	if(data.empty())
		return;

	// reset previous total count and line geometry
	total_count = 0;
	lines.clear();

	// setup plot origin and sizes
	vec2 org = vec2(0.0f); // since we draw into a separate plot frame buffer this time, the origin is at (0,0)
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
			lines.add(vec2(org.x() + 0 * step, scaled[0]));
			lines.add(vec2(org.x() + 1 * step, scaled[1]));

			lines.add(vec2(org.x() + 1 * step, scaled[1]));
			lines.add(vec2(org.x() + 2 * step, scaled[2]));

			lines.add(vec2(org.x() + 2 * step, scaled[2]));
			lines.add(vec2(org.x() + 3 * step, scaled[3]));
		}
	}

	std::cout << "Drawing " << lines.get_render_count() / 2 << " lines" << std::endl;

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}
