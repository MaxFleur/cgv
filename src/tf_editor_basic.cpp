/** BEGIN - MFLEURY **/

#include "tf_editor_basic.h"

tf_editor_basic::tf_editor_basic()
{
	// prevent the mouse events from reaching through this overlay to the underlying elements
	block_events = true;

	// setup positioning and size
	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(-3));

	// add a color attachment to the content frame buffer with support for transparency (alpha)
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	// change its size to be the same as the overlay
	fbc.set_size(get_overlay_size());

	fbc_plot.add_attachment("color", "flt32[R,G,B,A]");
	fbc_plot.set_size(get_overlay_size());

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);
	content_canvas.register_shader("plot_tone_mapping", "plot_tone_mapping.glpr");

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main
	// frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	m_renderer_draggables = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);

	vis_mode = VM_SHAPES;
}

void tf_editor_basic::draw(cgv::render::context& ctx) {

	if (!show)
		return;

	// disable depth testing to place this overlay on top of everything in the viewport
	glDisable(GL_DEPTH_TEST);

	// redraw the contents if they are damaged (i.e. they need to change)
	if (has_damage)
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

void tf_editor_basic::clear(cgv::render::context& ctx) {
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);
	fbc_plot.clear(ctx);
}

void tf_editor_basic::create_gui_basic() {
	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &tf_editor_basic::update_content));

	add_decorator("Basic Parameters", "heading", "level=3");
	add_member_control(this, "Threshold", m_threshold, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Line Alpha", m_alpha, "value_slider", "min=0.0;max=1.0;step=0.0001;log=true;ticks=true");
}

void tf_editor_basic::create_gui_tm() {
	if (begin_tree_node("Tone Mapping", this, true)) {
		align("\a");

		add_member_control(this, "Use Tone Mapping", use_tone_mapping, "check");
		add_member_control(this, "TM Norm Count", tm_normalization_count, "value_slider", "min=1;max=1000000;step=0.0001;log=true;ticks=true");
		add_member_control(this, "TM Alpha", tm_alpha, "value_slider", "min=0;max=50;step=0.0001;log=true;ticks=true");
		add_member_control(this, "TM Gamma", tm_gamma, "value_slider", "min=0;max=10;step=0.0001;log=true;ticks=true");

		align("\b");
		end_tree_node(this);
	}
}

void tf_editor_basic::create_gui_coloring() {
	add_decorator("Coloring Mode", "heading", "level=3");
	add_member_control(this, "Coloring", vis_mode, "dropdown", "enums=Basic, Gaussian");
}

/** END - MFLEURY **/