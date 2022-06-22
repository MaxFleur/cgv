#include "bpcp_overlay.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

bpcp_overlay::bpcp_overlay() {
	
	set_name("PCP Overlay");
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

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the line renderer with a shader program capable of drawing 2d lines
	//line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	polygon_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::polygon);
}

void bpcp_overlay::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);

	//line_renderer.destruct(ctx);
	polygon_renderer.destruct(ctx);
	//lines.destruct(ctx);
	blocks.destruct(ctx);
}

bool bpcp_overlay::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool bpcp_overlay::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	return false;
}

void bpcp_overlay::on_set(void* member_ptr) {

	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	update_member(member_ptr);
	post_redraw();
}

bool bpcp_overlay::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	//success &= line_renderer.init(ctx);
	success &= polygon_renderer.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	return success;
}

void bpcp_overlay::init_frame(cgv::render::context& ctx) {

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
	}
}

void bpcp_overlay::draw(cgv::render::context& ctx) {

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

void bpcp_overlay::draw_content(cgv::render::context& ctx) {

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
	int count = 9*100000;

	// make sure to not draw more lines than available
	if(total_count + count > blocks.get_render_count())
		count = blocks.get_render_count() - total_count;

	// get the shader program of the line renderer
	auto& line_prog = polygon_renderer.ref_prog();
	// enable before we change anything
	line_prog.enable(ctx);
	// Sets uniforms of the line shader program according to the content canvas,
	// which are needed to calculate pixel coordinates.
	content_canvas.set_view(ctx, line_prog);
	// Disable, since the renderer will enable and disable its shader program automatically
	// and we cannot enable a program that is already enabled.
	line_prog.disable(ctx);
	// draw the lines from the given geometry with offset and count
	glEnable(GL_PRIMITIVE_RESTART);
	glPrimitiveRestartIndex(0xFFFFFFFF);
	polygon_renderer.render(ctx, PT_TRIANGLE_STRIP, blocks, total_count, count);
	glDisable(GL_PRIMITIVE_RESTART);

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	// accumulate the total amount of so-far drawn lines
	total_count += count;

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < blocks.get_render_count();
	if(run)
		post_redraw();
	else
		std::cout << "done" << std::endl;

	has_damage = run;
}

void bpcp_overlay::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &bpcp_overlay::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Alpha", alpha, "value_slider", "min=0.0;max=100.0;step=0.0001;log=true;ticks=true");
}

void bpcp_overlay::init_styles(cgv::render::context& ctx) {

	// configure style for the plot lines
	cgv::glutil::shape2d_style line_style;
	line_style.use_blending = true;
	line_style.use_fill_color = false;
	line_style.apply_gamma = false;
	line_style.fill_color = rgba(rgb(0.0f), alpha);

	// as the line style does not change during rendering, we can set it here once
	auto& line_prog = polygon_renderer.ref_prog();
	line_prog.enable(ctx);
	line_style.apply(ctx, line_prog);
	line_prog.disable(ctx);

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

void bpcp_overlay::update_content() {
	
	if(minmax_data.empty() || volume_data.empty() || minmax_data.size() != 4*volume_data.size())
		return;

	// reset previous total count and line geometry
	total_count = 0;
	//lines.clear();
	blocks.clear();

	// setup plot origin and sizes
	vec2 org = static_cast<vec2>(domain.pos());
	float h = domain.size().y();
	float step = static_cast<float>(domain.size().x()) / 3.0f;

	rgba base_color = rgba(rgb(0.0f), alpha);

	unsigned agg_idx = 0;

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < minmax_data.size(); i += 8) {
		rgba color = volume_data[i / 4] * base_color;
		float avg = volume_data[i/4 + 1];

		if(avg >= threshold) {
			blocks.add(vec2(org.x() + 0 * step, org.y() + minmax_data[i + 0] * h), color);
			blocks.add(vec2(org.x() + 0 * step, org.y() + minmax_data[i + 1] * h), color);
			blocks.add(vec2(org.x() + 1 * step, org.y() + minmax_data[i + 2] * h), color);
			blocks.add(vec2(org.x() + 1 * step, org.y() + minmax_data[i + 3] * h), color);
			blocks.add(vec2(org.x() + 2 * step, org.y() + minmax_data[i + 4] * h), color);
			blocks.add(vec2(org.x() + 2 * step, org.y() + minmax_data[i + 5] * h), color);
			blocks.add(vec2(org.x() + 3 * step, org.y() + minmax_data[i + 6] * h), color);
			blocks.add(vec2(org.x() + 3 * step, org.y() + minmax_data[i + 7] * h), color);

			blocks.add_idx(agg_idx + 0);
			blocks.add_idx(agg_idx + 1);
			blocks.add_idx(agg_idx + 2);
			blocks.add_idx(agg_idx + 3);
			blocks.add_idx(agg_idx + 4);
			blocks.add_idx(agg_idx + 5);
			blocks.add_idx(agg_idx + 6);
			blocks.add_idx(agg_idx + 7);

			blocks.add_idx(0xFFFFFFFF);
			agg_idx += 8;
		}
	}

	std::cout << "Drawing " << (agg_idx/8)*6 << " triangles" << std::endl;

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}
