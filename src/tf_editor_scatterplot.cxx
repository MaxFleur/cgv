/** BEGIN - MFLEURY **/

#include "tf_editor_scatterplot.h"

#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

#include "tf_editor_shared_functions.h"

tf_editor_scatterplot::tf_editor_scatterplot() {
	
	set_name("TF Editor Scatterplot Overlay");
	set_overlay_size(ivec2(700, 700));
	// Reset alpha so points are better visisble at the start
	m_alpha = 0.1f;

	// Register additional ellipses
	content_canvas.register_shader("ellipse", cgv::glutil::canvas::shaders_2d::ellipse);
	content_canvas.register_shader("gauss_ellipse", "gauss_ellipse2d.glpr");

	// initialize the point renderer with a shader program capable of drawing 2d circles
	m_renderer_plot_points = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);

	// callbacks for the moving of draggables
	m_point_handles.set_drag_callback(std::bind(&tf_editor_scatterplot::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_scatterplot::end_drag, this));
}

void tf_editor_scatterplot::clear(cgv::render::context& ctx) {
	m_renderer_plot_points.destruct(ctx);
	m_geometry_plot_points.destruct(ctx);
	m_geometry_draggables.destruct(ctx);
	m_geometry_draggables_interacted.destruct(ctx);

	m_renderer_draggables.destruct(ctx);

	m_font.destruct(ctx);
	m_renderer_fonts.destruct(ctx);
}

bool tf_editor_scatterplot::handle_event(cgv::gui::event& e) {
	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if (et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;
		if (ke.get_action() == cgv::gui::KA_PRESS && ke.get_key() == 'M') {
			vis_mode == VM_SHAPES ? vis_mode = VM_GTF : vis_mode = VM_SHAPES;
			on_set(&vis_mode);
			update_content();

			return true;
		}
		else if (ke.get_action() == cgv::gui::KA_PRESS && ke.get_key() == cgv::gui::KEY_Space) {
			is_peak_mode = !is_peak_mode;
			redraw();
		}
	}

	if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;

		const auto mpos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));
		// Search for points if RMB is pressed
		if (me.get_action() == cgv::gui::MA_PRESS && me.get_button() == cgv::gui::MB_RIGHT_BUTTON) {
			find_clicked_draggable(mpos.x(), mpos.y());
		}

		// Set width if a scroll is done
		else if (me.get_action() == cgv::gui::MA_WHEEL && m_is_point_clicked) {
			const auto modifiers = e.get_modifiers();
			const auto negative_change = me.get_dy() > 0 ? true : false;
			const auto ctrl_pressed = modifiers & cgv::gui::EM_CTRL ? true : false;

			scroll_centroid_width(mpos.x(), mpos.y(), negative_change, ctrl_pressed);
		}

		bool handled = false;
		handled |= m_point_handles.handle(e, last_viewport_size, container);

		if (handled)
			post_redraw();

		return handled;
	}

	return false;
}

void tf_editor_scatterplot::on_set(void* member_ptr) {
	// react to changes of the point alpha parameter and update the styles
	if(member_ptr == &m_alpha || member_ptr == &blur) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	// Update if the visualization mode has changed
	if (member_ptr == &vis_mode) {
		update_content();
	}

	update_member(member_ptr);
	redraw();
}

bool tf_editor_scatterplot::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and point renderer
	success &= fbc.ensure(ctx);
	success &= fbc_plot.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_renderer_plot_points.init(ctx);
	success &= m_renderer_fonts.init(ctx);
	success &= m_renderer_draggables.init(ctx);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if(m_font.init(ctx)) {
		m_labels.set_msdf_font(&m_font);
		m_labels.set_font_size(m_font_size);
	}

	return success;
}

void tf_editor_scatterplot::init_frame(cgv::render::context& ctx) {

	// react to changes in the overlay size
	if(ensure_overlay_layout(ctx)) {
		ivec2 overlay_size = get_overlay_size();
		
		// calculate the new domain size
		domain.set_pos(ivec2(13) + label_space); // 13 pixel padding (inner space from border) + 20 pixel space for labels
		domain.set_size(overlay_size - 2 * ivec2(13) - label_space); // scale size to fit in leftover inner space

		// update the offline frame buffer to the new size
		fbc.set_size(overlay_size);
		fbc.ensure(ctx);

		// udpate the offline plot frame buffer to the domain size
		fbc_plot.set_size(overlay_size);
		fbc_plot.ensure(ctx);

		// set resolutions of the canvases
		content_canvas.set_resolution(ctx, overlay_size);
		viewport_canvas.set_resolution(ctx, get_viewport_size());

		create_grid();
		create_labels();

		has_damage = true;
		m_reset_plot = true;
	}
}

void tf_editor_scatterplot::create_gui() {

	create_overlay_gui();

	tf_editor_basic::create_gui_basic();
	add_member_control(this, "Blur", blur, "value_slider", "min=0;max=20;step=0.0001;ticks=true");
	add_member_control(this, "Radius", radius, "value_slider", "min=0;max=10;step=0.0001;ticks=true");

	tf_editor_basic::create_gui_coloring();
	tf_editor_basic::create_gui_tm();
}

void tf_editor_scatterplot::resynchronize() {
	// Clear and readd points
	m_points.clear();
	for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
		add_draggables(i);
	}

	set_point_handles();
	// Then redraw strips etc
	redraw();
}

void tf_editor_scatterplot::primitive_added() {
	add_draggables(m_shared_data_ptr->primitives.size() - 1);
	// Add a corresponding draggable point for every centroid
	set_point_handles();
	m_shared_data_ptr->set_synchronized(false);

	redraw();
}

void tf_editor_scatterplot::init_styles(cgv::render::context& ctx) {

	// configure style for rendering the plot framebuffer texture
	m_style_grid.use_texture = true;
	m_style_grid.apply_gamma = false;
	m_style_grid.feather_width = 0.0f;

	// configure style for the plot points
	m_style_plot_points.use_blending = true;
	m_style_plot_points.use_fill_color = false;
	m_style_plot_points.apply_gamma = false;
	m_style_plot_points.position_is_center = true;
	m_style_plot_points.fill_color = rgba(rgb(0.0f), m_alpha);
	m_style_plot_points.feather_width = blur;

	// Style for the grid
	m_rectangle_style.border_color = rgba(0.4f, 0.4f, 0.4f, 1.0f);
	m_rectangle_style.use_blending = false;
	m_rectangle_style.apply_gamma = false;
	m_rectangle_style.use_fill_color = false;
	m_rectangle_style.ring_width = 0.5f;

	// Shapes
	m_style_shapes.use_blending = true;
	m_style_shapes.use_fill_color = true;
	m_style_shapes.apply_gamma = false;
	m_style_shapes.border_width = 1.0f;

	// configure style for the plot labels
	cgv::glutil::shape2d_style text_style;
	text_style.fill_color = rgba(rgb(0.0f), 1.0f);
	text_style.border_color.alpha() = 0.0f;
	text_style.border_width = 0.333f;
	text_style.use_blending = true;
	text_style.apply_gamma = false;

	// draggables style
	m_style_draggables.position_is_center = true;
	m_style_draggables.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables.border_width = 1.5f;
	m_style_draggables.use_blending = true;
	m_style_draggables_interacted.position_is_center = true;
	m_style_draggables_interacted.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables_interacted.border_width = 1.5f;
	m_style_draggables_interacted.use_blending = true;
	
	auto& font_prog = m_renderer_fonts.ref_prog();
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

void tf_editor_scatterplot::update_content() {

	if (!m_data_set_ptr || m_data_set_ptr->voxel_data.empty())
		return;

	const auto& data = m_data_set_ptr->voxel_data;

	// reset previous total count and point geometry
	m_total_count = 0;
	m_geometry_plot_points.clear();

	// setup plot origin and sizes
	const auto org = static_cast<vec2>(domain.pos());
	const auto size = domain.size();

	// Construct to add the points
	const auto add_point = [&](const vec4& v, vec2& pos, const float& x, const float& y) {
		// Apply size and offset
		pos.set(x, y);
		pos *= size;
		pos += org;

		rgb color_rgb(0.0f);
		if (vis_mode == VM_GTF) {
			color_rgb = tf_editor_shared_functions::get_color(v, m_shared_data_ptr->primitives);
		}
		rgba col(color_rgb, use_tone_mapping ? 1.0f : m_alpha);

		m_geometry_plot_points.add(pos, col);
	};

	// for each given sample of 4 protein densities, do:
	for (size_t i = 0; i < data.size(); ++i) {
		const auto& v = data[i];

		// calculate the average to allow filtering with the given threshold
		auto avg = v[0] + v[1] + v[2] + v[3];
		avg *= 0.25f;

		if (avg > m_threshold) {
			// Draw the points for each protein
			// First column
			auto offset = 0.0f;

			// Myosin - Actin, Myosin - Obscurin, Myosin - Salimus
			for (int i = 1; i < 4; i++) {
				vec2 pos(v[0], v[i]);

				add_point(v, pos, pos.x() * 0.33f, (pos.y() / 3.0f) + offset);
				offset += 0.33f;
			}
			// Second col
			// Salimus - Actin, Salimus - Obscurin
			offset = 0.0f;
			for (int i = 1; i < 3; i++) {
				vec2 pos(v[3], v[i]);

				add_point(v, pos, (pos.x() * 0.33f) + 0.33f, (pos.y() / 3.0f) + offset);
				offset += 0.33f;
			}
			// Obscurin - Actin
			vec2 pos(v[2], v[1]);

			add_point(v, pos, (pos.x() * 0.33f) + 0.66f, (pos.y() / 3.0f));
			offset += 0.33f;
		}
	}

	redraw();
}

void tf_editor_scatterplot::create_labels() {
	
	m_labels.clear();

	std::vector<std::string> texts = { "0", "1", "2", "3" };

	if(m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3) {
		texts = m_data_set_ptr->stain_names;
	}

	if(m_font.is_initialized()) {
		const auto x = domain.pos().x() + 100;
		const auto y = domain.pos().y() - label_space / 2;

		m_labels.add_text(texts[0], ivec2(domain.box.get_center().x() * 0.35f, y), cgv::render::TA_NONE);
		m_labels.add_text(texts[1], ivec2(domain.box.get_center().x(), y), cgv::render::TA_NONE);
		m_labels.add_text(texts[2], ivec2(domain.box.get_center().x() * 1.60f, y), cgv::render::TA_NONE);

		m_labels.add_text(texts[3], ivec2(domain.box.get_center().x() * 0.35f, y), cgv::render::TA_NONE);
		m_labels.add_text(texts[2], ivec2(domain.box.get_center().x(), y), cgv::render::TA_NONE);
		m_labels.add_text(texts[1], ivec2(domain.box.get_center().x() * 1.60f, y), cgv::render::TA_NONE);
	}
}

void tf_editor_scatterplot::create_grid() {
	m_rectangles_draw.clear();
	m_rectangles_calc.clear();

	const auto size_x = domain.size().x();
	const auto size_y = domain.size().y();

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.05f), vec2(size_x * 0.38f, size_y * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.05f), vec2(size_x * 0.33f, size_y * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.38f), vec2(size_x * 0.38f, size_y * 0.71f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.38f), vec2(size_x * 0.33f, size_y * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.71f), vec2(size_x * 0.38f, size_y * 1.05f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.05f, size_y * 0.71f), vec2(size_x * 0.33f, size_y * 0.34f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.38f, size_y * 0.05f), vec2(size_x * 0.71f, size_y * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.38f, size_y * 0.05f), vec2(size_x * 0.33f, size_y * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.38f, size_y * 0.38f), vec2(size_x * 0.71f, size_y * 0.71f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.38f, size_y * 0.38f), vec2(size_x * 0.33f, size_y * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.71f, size_y * 0.05f), vec2(size_x * 1.05f, size_y * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(size_x * 0.71f, size_y * 0.05f), vec2(size_x * 0.33f, size_y * 0.33f)));
}

void tf_editor_scatterplot::create_primitive_shapes() {
	m_ellipses.clear();
	m_boxes.clear();

	for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
		std::vector<tf_editor_shared_data_types::ellipse> ellipses;
		std::vector<tf_editor_shared_data_types::rectangle> boxes;

		for (int j = 0; j < m_points.at(i).size(); j++) {
			// Get the width for the point's protein stains
			const auto width_stain_x = m_shared_data_ptr->primitives.at(i).centr_widths[m_points[i][j].m_stain_first];
			const auto width_stain_y = m_shared_data_ptr->primitives.at(i).centr_widths[m_points[i][j].m_stain_second];
			// Multiply with the rectangle size
			const auto width_x = width_stain_x * m_points.at(i).at(j).parent_rectangle->size_x();
			const auto width_y = width_stain_y * m_points.at(i).at(j).parent_rectangle->size_y();
			// Store
			const auto position_start = vec2(m_points.at(i).at(j).pos.x() - width_x / 2, m_points.at(i).at(j).pos.y() - width_y / 2);

			m_shared_data_ptr->primitives.at(i).type == shared_data::TYPE_BOX ?
				boxes.push_back(tf_editor_shared_data_types::rectangle(position_start, vec2(width_x, width_y))) :
				ellipses.push_back(tf_editor_shared_data_types::ellipse(position_start, vec2(width_x, width_y)));
		}

		m_shared_data_ptr->primitives.at(i).type == shared_data::TYPE_BOX ? m_boxes.push_back(boxes) : m_ellipses.push_back(ellipses);
	}
}

void tf_editor_scatterplot::add_draggables(int primitive_index) {
	std::vector<tf_editor_shared_data_types::point_scatterplot> points;
	const auto org = static_cast<vec2>(domain.pos());
	const auto size = domain.size();

	// Add the new draggables to the scatter plot
	const auto& centroid_positions = m_shared_data_ptr->primitives.at(primitive_index).centr_pos;
	auto pos = m_rectangles_calc.at(0).point_in_rect(vec2(centroid_positions[0], centroid_positions[3]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 0, 3, &m_rectangles_calc.at(0)));

	pos = m_rectangles_calc.at(1).point_in_rect(vec2(centroid_positions[0], centroid_positions[2]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 0, 2, &m_rectangles_calc.at(1)));

	pos = m_rectangles_calc.at(2).point_in_rect(vec2(centroid_positions[0], centroid_positions[1]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 0, 1, &m_rectangles_calc.at(2)));

	pos = m_rectangles_calc.at(3).point_in_rect(vec2(centroid_positions[1], centroid_positions[3]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 1, 3, &m_rectangles_calc.at(3)));

	pos = m_rectangles_calc.at(4).point_in_rect(vec2(centroid_positions[1], centroid_positions[2]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 1, 2, &m_rectangles_calc.at(4)));

	pos = m_rectangles_calc.at(5).point_in_rect(vec2(centroid_positions[2], centroid_positions[3]));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(pos, 2, 3, &m_rectangles_calc.at(5)));

	m_points.push_back(points);
}

void tf_editor_scatterplot::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// first draw the plot (the method will check internally if it needs an update)
	bool done = draw_scatterplot(ctx);

	// enable the offline frame buffer, so all things are drawn into its attached textures
	fbc.enable(ctx);
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// draw the plot content from its own framebuffer texture
	fbc_plot.enable_attachment(ctx, "color", 0);

	auto& rectangle_prog = content_canvas.enable_shader(ctx, use_tone_mapping ? "plot_tone_mapping" : "rectangle");

	// Aply tone mapping using the tone mapping shader
	if (use_tone_mapping) {
		rectangle_prog.set_uniform(ctx, "normalization_factor", 1.0f / static_cast<float>(std::max(tm_normalization_count, 1u)));
		rectangle_prog.set_uniform(ctx, "alpha", tm_alpha);
		rectangle_prog.set_uniform(ctx, "gamma", tm_gamma);
		rectangle_prog.set_uniform(ctx, "use_color", vis_mode == VM_GTF);
	}

	m_style_grid.use_blending = use_tone_mapping;
	m_style_grid.apply(ctx, rectangle_prog);

	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");

	// ...and axis labels
	// this is pretty much the same as for the generic renderer
	auto& font_prog = m_renderer_fonts.ref_prog();
	font_prog.enable(ctx);
	content_canvas.set_view(ctx, font_prog);
	font_prog.disable(ctx);
	// draw the first label only
	m_renderer_fonts.render(ctx, get_overlay_size(), m_labels, 0, 3);

	// save the current view matrix
	content_canvas.push_modelview_matrix();
	// Rotate the view, so the second label is drawn sideways.
	// Objects are rotated around the origin, so we first need to move the text to the origin.
	// Transformations are applied in reverse order:
	//  3 - move text to origin
	//  2 - rotate 90 degrees
	//  1 - move text back to its position
	//content_canvas.mul_modelview_matrix(ctx, cgv::math::translate2h(labels.ref_texts()[1].position));	// 1
	content_canvas.mul_modelview_matrix(ctx, cgv::math::rotate2h(90.0f));							// 2
	content_canvas.mul_modelview_matrix(ctx, cgv::math::translate2h(vec2(0.0f, -40.0f)));	// 3

	// now render the second label
	font_prog.enable(ctx);
	content_canvas.set_view(ctx, font_prog);
	font_prog.disable(ctx);
	m_renderer_fonts.render(ctx, get_overlay_size(), m_labels, 3, 6);

	// restore the previous view matrix
	content_canvas.pop_modelview_matrix(ctx);

	// Shapes are next (if peak mode is enabled)
	if (!is_peak_mode) {
		create_primitive_shapes();
		draw_primitive_shapes(ctx);
	}

	// Now the draggables
	draw_draggables(ctx);

	// Create and draw the scatterplot grids
	auto& rect_prog = content_canvas.enable_shader(ctx, "rectangle");
	m_rectangle_style.apply(ctx, rect_prog);
	for (const auto rectangle : m_rectangles_draw) {
		content_canvas.draw_shape(ctx, rectangle.start, rectangle.end, m_rectangle_style.border_color);
	}
	content_canvas.disable_current_shader(ctx);

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = !done;
}

bool tf_editor_scatterplot::draw_scatterplot(cgv::render::context& ctx) {

	// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	if(use_tone_mapping) {
		glBlendFunc(GL_ONE, GL_ONE);
	}

	// make sure to reset the color buffer if we update the content from scratch
	if (m_total_count == 0 || m_reset_plot) {
		if(use_tone_mapping)
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		else
			glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		m_reset_plot = false;
	}

	// the amount of points that will be drawn in each step
	auto count = 500000;

	// make sure to not draw more points than available
	if (m_total_count + count > m_geometry_plot_points.get_render_count()) 
		count = m_geometry_plot_points.get_render_count() - m_total_count;

	if (count > 0) {
		auto& point_prog = m_renderer_plot_points.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_style_plot_points.fill_color = rgba(rgb(0.0f), m_alpha);
		m_style_plot_points.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(radius));
		point_prog.disable(ctx);
		m_renderer_plot_points.render(ctx, PT_POINTS, m_geometry_plot_points, m_total_count, count);
	}

	// accumulate the total amount of so-far drawn points
	m_total_count += count;

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc_plot.disable(ctx);

	// reset the blend function
	if (use_tone_mapping) {
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = m_total_count < m_geometry_plot_points.get_render_count();
	if (run) {
		post_redraw();
	}
	else {
		//std::cout << "done" << std::endl;
	}
	return !run;
}

void tf_editor_scatterplot::draw_draggables(cgv::render::context& ctx) {
	for (int i = 0; i < m_shared_data_ptr->primitives.size(); ++i) {
		// Clear for each draggable because colors etc might change
		m_geometry_draggables.clear();
		m_geometry_draggables_interacted.clear();

		const auto color = m_shared_data_ptr->primitives.at(i).color;
		// Apply color to the draggables, always do full opacity
		m_style_draggables.fill_color = rgba{ color.R(), color.G(), color.B(), 1.0f };
		m_style_draggables_interacted.fill_color = rgba{ color.R(), color.G(), color.B(), 1.0f };

		for (int j = 0; j < m_points[i].size(); j++) {
			// Add the points based on if they have been interacted with
			std::find(m_interacted_points.begin(), m_interacted_points.end(), &m_points[i][j]) != m_interacted_points.end() ?
				m_geometry_draggables_interacted.add(m_points[i][j].get_render_position()) :
				m_geometry_draggables.add(m_points[i][j].get_render_position());
		}

		// Draw 
		shader_program& point_prog = m_renderer_draggables.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_style_draggables.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(12.0f));
		point_prog.disable(ctx);
		m_renderer_draggables.render(ctx, PT_POINTS, m_geometry_draggables);

		point_prog.enable(ctx);
		m_style_draggables_interacted.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(16.0f));
		point_prog.disable(ctx);
		m_renderer_draggables.render(ctx, PT_POINTS, m_geometry_draggables_interacted);
	}
}

void tf_editor_scatterplot::draw_primitive_shapes(cgv::render::context& ctx) {
	auto index_ellipses = 0;
	auto index_boxes = 0;

	for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
		const auto& type = m_shared_data_ptr->primitives.at(i).type;

		m_style_shapes.border_color = rgba{ m_shared_data_ptr->primitives.at(i).color, 1.0f };
		m_style_shapes.fill_color = m_shared_data_ptr->primitives.at(i).color;
		m_style_shapes.ring_width = vis_mode == VM_SHAPES ? 0.0f : 3.0f;

		auto& prog = type == shared_data::TYPE_BOX ? content_canvas.enable_shader(ctx, "rectangle") : content_canvas.enable_shader(ctx, "gauss_ellipse");
		auto& index = type == shared_data::TYPE_BOX ? index_boxes : index_ellipses;

		// For each primitive, we have either six boxes or six ellipses
		for (int j = 0; j < 6; j++) {
			// Prevent shapes overlapping rectangles
			glEnable(GL_SCISSOR_TEST);
			glScissor(m_rectangles_calc.at(j).start.x(), m_rectangles_calc.at(j).start.y(), m_rectangles_calc.at(j).size_x(), m_rectangles_calc.at(j).size_y());

			m_style_shapes.apply(ctx, prog);
			type == shared_data::TYPE_BOX ?
				content_canvas.draw_shape(ctx, m_boxes.at(index).at(j).start, m_boxes.at(index).at(j).end, rgba(0, 1, 1, 1)) :
				content_canvas.draw_shape(ctx, m_ellipses.at(index).at(j).pos, m_ellipses.at(index).at(j).size, rgba(0, 1, 1, 1));

			glDisable(GL_SCISSOR_TEST);
		}

		index++;
		content_canvas.disable_current_shader(ctx);
	}
}

void tf_editor_scatterplot::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();
	m_interacted_points.clear();

	if (m_is_point_clicked) {
		m_is_point_clicked = false;
	}

	const auto org = static_cast<vec2>(domain.pos());
	const auto size = domain.size();

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating draggable in the scatter plot has to be updated
			if (&m_points[i][j] == m_point_handles.get_dragged()) {
				m_interacted_points.push_back(&m_points[i][j]);
				const auto& found_point = m_points[i][j];

				m_interacted_point_id.first = i;
				m_interacted_point_id.second = j;
				// Update all other points with values belonging to this certain point
				if (found_point.m_stain_first == 0 && found_point.m_stain_second == 3) {
					m_points[i][1].pos = vec2(found_point.pos.x(), m_points[i][1].pos.y());
					m_points[i][2].pos = vec2(found_point.pos.x(), m_points[i][2].pos.y());

					m_points[i][3].pos = vec2(m_points[i][3].pos.x(), found_point.pos.y());
					m_points[i][5].pos = vec2(m_points[i][5].pos.x(), found_point.pos.y());
				}
				else if (found_point.m_stain_first == 0 && found_point.m_stain_second == 2) {
					m_points[i][0].pos = vec2(found_point.pos.x(), m_points[i][0].pos.y());
					m_points[i][2].pos = vec2(found_point.pos.x(), m_points[i][2].pos.y());

					m_points[i][4].pos = vec2(m_points[i][4].pos.x(), found_point.pos.y());
					m_points[i][5].pos = vec2(found_point.pos.y() + found_point.parent_rectangle->size_x(), m_points[i][5].pos.y());
				}
				else if (found_point.m_stain_first == 0 && found_point.m_stain_second == 1) {
					m_points[i][0].pos = vec2(found_point.pos.x(), m_points[i][0].pos.y());
					m_points[i][1].pos = vec2(found_point.pos.x(), m_points[i][1].pos.y());

					m_points[i][3].pos = vec2(found_point.pos.y() - found_point.parent_rectangle->size_x(), m_points[i][3].pos.y());
					m_points[i][4].pos = vec2(found_point.pos.y() - found_point.parent_rectangle->size_x(), m_points[i][4].pos.y());
				}
				else if (found_point.m_stain_first == 1 && found_point.m_stain_second == 3) {
					m_points[i][0].pos = vec2(m_points[i][0].pos.x(), found_point.pos.y());
					m_points[i][5].pos = vec2(m_points[i][5].pos.x(), found_point.pos.y());

					m_points[i][2].pos = vec2(m_points[i][2].pos.x(), found_point.pos.x() + found_point.parent_rectangle->size_y());
					m_points[i][4].pos = vec2(found_point.pos.x(), m_points[i][4].pos.y());
				}
				else if (found_point.m_stain_first == 1 && found_point.m_stain_second == 2) {
					m_points[i][1].pos = vec2(m_points[i][1].pos.x(), found_point.pos.y());
					m_points[i][5].pos = vec2(found_point.pos.y() + found_point.parent_rectangle->size_x(), m_points[i][5].pos.y());

					m_points[i][2].pos = vec2(m_points[i][2].pos.x(), found_point.pos.x() + found_point.parent_rectangle->size_y());
					m_points[i][3].pos = vec2(found_point.pos.x(), m_points[i][3].pos.y());
				}
				else if (found_point.m_stain_first == 2 && found_point.m_stain_second == 3) {
					m_points[i][0].pos = vec2(m_points[i][0].pos.x(), found_point.pos.y());
					m_points[i][3].pos = vec2(m_points[i][3].pos.x(), found_point.pos.y());

					m_points[i][1].pos = vec2(m_points[i][1].pos.x(), found_point.pos.x() - found_point.parent_rectangle->size_y());
					m_points[i][4].pos = vec2(m_points[i][4].pos.x(), found_point.pos.x() - found_point.parent_rectangle->size_y());
				}
				// Remap the gui values
				const auto gui_value_first = m_points[i][j].get_relative_position(m_points[i][j].pos.x(), true);
				m_shared_data_ptr->primitives.at(i).centr_pos[m_points[i][j].m_stain_first] = gui_value_first;
				update_member(&m_shared_data_ptr->primitives.at(i).centr_pos[m_points[i][j].m_stain_first]);

				const auto gui_value_second = m_points[i][j].get_relative_position(m_points[i][j].pos.y(), false);
				m_shared_data_ptr->primitives.at(i).centr_pos[m_points[i][j].m_stain_second] = gui_value_second;
				update_member(&m_shared_data_ptr->primitives.at(i).centr_pos[m_points[i][j].m_stain_second]);

				m_is_point_dragged = true;

				m_shared_data_ptr->set_synchronized(false);
			}
		}
	}

	has_damage = true;
	post_redraw();
}

void tf_editor_scatterplot::set_point_handles() {
	m_point_handles.clear();
	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			m_point_handles.add(&m_points[i][j]);
		}
	}
}

void tf_editor_scatterplot::find_clicked_draggable(int x, int y) {
	const auto input_vec = vec2{ static_cast<float>(x), static_cast<float>(y) };
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
		m_clicked_draggable_id = found_index;
		has_damage = true;
		post_redraw();
	}
}

void tf_editor_scatterplot::scroll_centroid_width(int x, int y, bool negative_change, bool ctrl_pressed) {
	auto found = false;
	int found_index;
	// Search through the rectangles
	for (int i = 0; i < m_rectangles_calc.size(); i++) {
		if (m_rectangles_calc.at(i).is_inside(x, y)) {
			// If the rectangle is found, update the point's width in it depending on the ctrl modifier
			auto& primitives = m_shared_data_ptr->primitives[m_clicked_draggable_id];
			auto& width = primitives.centr_widths[ctrl_pressed ? m_points[m_clicked_draggable_id][i].m_stain_first : m_points[m_clicked_draggable_id][i].m_stain_second];
			// auto width = 0.0f;
			width += negative_change ? -0.02f : 0.02f;
			width = cgv::math::clamp(width, 0.0f, 1.0f);

			found = true;
			found_index = i;
			break;
		}
	}
	// If we found something, we have to set the corresponding point ids and redraw
	if (found) {
		m_is_point_dragged = true;

		m_shared_data_ptr->set_synchronized(false);
		redraw();
	}
}

/** END - MFLEURY **/