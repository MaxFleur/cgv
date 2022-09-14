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
	content_canvas.set_apply_gamma(false);

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main
	// frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	m_renderer_draggables_circle = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);
	m_renderer_draggables_rectangle = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::rectangle);

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

bool tf_editor_basic::handle_key_input(const char& key, const int& index) {
	switch (key)
	{
	case 'M':
		vis_mode == VM_SHAPES ? vis_mode = VM_GTF : vis_mode = VM_SHAPES;
		on_set(&vis_mode);
		update_content();
		return true;
	case '1':
	case '2':
	case '3':
		if (is_interacting) {
			// Remap to controls, decrement to set to enum values
			m_shared_data_ptr->primitives[index].type = static_cast<shared_data::Type>(key - '0' - 1);
			m_shared_data_ptr->set_synchronized();
			redraw();
		}
		return true;
	default:
		return false;
	}
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

		add_member_control(this, "TM Norm Count", tm_normalization_count, "value_slider", "min=1;max=1000000;step=0.0001;log=true;ticks=true");
		add_member_control(this, "TM Alpha", tm_alpha, "value_slider", "min=0;max=50;step=0.0001;log=true;ticks=true");

		align("\b");
		end_tree_node(this);
	}
}

void tf_editor_basic::create_gui_coloring() {
	add_decorator("Coloring Mode", "heading", "level=3");
	add_member_control(this, "Coloring", vis_mode, "dropdown", "enums=Shapes, Volume Data");
}

void tf_editor_basic::handle_mouse_click_end(bool found, bool double_clicked, int i, int j, int& interacted_point_id, bool is_lbe) {
	if (found) {
		if (double_clicked) {
			// Set width for a double click
			auto& current_width = m_shared_data_ptr->primitives.at(i).centr_widths[j];
			current_width = current_width < 2.0f ? 10.0f : 0.5f;

			m_is_point_dragged = true;
		}
	}
	else {
		interacted_point_id = INT_MAX;
	}

	is_interacting = found;
	m_shared_data_ptr->is_primitive_selected = found;
	m_shared_data_ptr->selected_primitive_id = interacted_point_id;

	m_shared_data_ptr->set_synchronized(is_lbe);
	redraw();
}

/** END - MFLEURY **/