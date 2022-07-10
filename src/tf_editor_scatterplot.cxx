#include "tf_editor_scatterplot.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>
#include <cgv_glutil/color_map.h>

tf_editor_scatterplot::tf_editor_scatterplot() {
	
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

	fbc_plot.add_attachment("color", "flt32[R,G,B,A]");
	fbc_plot.set_size(get_overlay_size());

	// register a rectangle shader for the content canvas, to draw a frame around the plot
	content_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	content_canvas.register_shader("ellipse", cgv::glutil::canvas::shaders_2d::ellipse);

	// register a rectangle shader for the viewport canvas, so that we can draw our content frame buffer to the main frame buffer
	viewport_canvas.register_shader("rectangle", cgv::glutil::canvas::shaders_2d::rectangle);

	// initialize the point renderer with a shader program capable of drawing 2d circles
	m_point_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);

	m_line_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_draggables_renderer = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::circle);

	// callbacks for the moving of centroids
	m_point_handles.set_drag_callback(std::bind(&tf_editor_scatterplot::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_scatterplot::end_drag, this));
}

void tf_editor_scatterplot::clear(cgv::render::context& ctx) {

	// destruct all previously initialized members
	content_canvas.destruct(ctx);
	viewport_canvas.destruct(ctx);
	fbc.clear(ctx);
	fbc_plot.clear(ctx);

	m_point_renderer.destruct(ctx);
	m_point_geometry_data.destruct(ctx);
	m_point_geometry.destruct(ctx);
	m_point_geometry_interacted.destruct(ctx);

	m_line_renderer.destruct(ctx);
	m_draggables_renderer.destruct(ctx);

	font.destruct(ctx);
	font_renderer.destruct(ctx);
}

bool tf_editor_scatterplot::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool tf_editor_scatterplot::handle_event(cgv::gui::event& e) {
	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;

		ivec2 mpos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));
		// Search for points if LMB is pressed
		if (me.get_button() == cgv::gui::MB_RIGHT_BUTTON) {
			find_clicked_centroid(mpos.x(), mpos.y());
		}

		// Set width if a scroll is done
		else if (me.get_action() == cgv::gui::MA_WHEEL && m_is_point_clicked) {
			const auto modifiers = e.get_modifiers();
			const auto negative_change = me.get_dy() > 0 ? true : false;
			const auto shift_pressed = modifiers & cgv::gui::EM_CTRL ? true : false;

			scroll_centroid_width(mpos.x(), mpos.y(), negative_change, shift_pressed);
		}

		bool handled = false;
		handled |= m_point_handles.handle(e, last_viewport_size, container);

		if (handled)
			post_redraw();

		return handled;
	}

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	return false;
}

void tf_editor_scatterplot::on_set(void* member_ptr) {

	// react to changes of the point alpha parameter and update the styles
	if(member_ptr == &alpha || member_ptr == &blur) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}

	if(member_ptr == &x_idx) {
		x_idx = cgv::math::clamp(x_idx, 0, 3);
		if(m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3 && labels.size() > 1)
			labels.set_text(0, m_data_set_ptr->stain_names[x_idx]);
	} else if(member_ptr == &y_idx) {
		y_idx = cgv::math::clamp(y_idx, 0, 3);
		if(m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3 && labels.size() > 1)
			labels.set_text(1, m_data_set_ptr->stain_names[y_idx]);
	}

	// look for updated centroid data
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); ++i) {
		auto value = 0.0f;

		for (int protein_id = 0; protein_id < 4; protein_id++) {
			if (member_ptr == &m_shared_data_ptr->centroids.at(i).centroids[protein_id] ||
				member_ptr == &m_shared_data_ptr->centroids.at(i).color ||
				member_ptr == &m_shared_data_ptr->centroids.at(i).widths[protein_id]) {

				// Move the according points if their position was changed
				if (member_ptr == &m_shared_data_ptr->centroids.at(i).centroids[protein_id]) {
					const auto org = static_cast<vec2>(domain.pos());
					const auto size = domain.size();
					value = m_shared_data_ptr->centroids.at(i).centroids[protein_id];

					switch (protein_id) {
					case 0:
						m_points[i][0].pos.set(((value * size.x()) / 3) + org.x(), m_points[i][0].pos.y());
						m_points[i][1].pos.set(((value * size.x()) / 3) + org.x(), m_points[i][1].pos.y());
						m_points[i][2].pos.set(((value * size.x()) / 3) + org.x(), m_points[i][2].pos.y());
						break;
					case 1:
						m_points[i][2].pos.set(m_points[i][2].pos.x(), ((value * size.y()) / 3) + org.y() + size.y() * 0.66f);
						m_points[i][3].pos.set(((value * size.x()) / 3) + org.x() + size.x() * 0.33f, m_points[i][3].pos.y());
						m_points[i][4].pos.set(((value * size.x()) / 3) + org.x() + size.x() * 0.33f, m_points[i][4].pos.y());
						break;
					case 2:
						m_points[i][1].pos.set(m_points[i][1].pos.x(), ((value * size.y()) / 3) + size.y() * 0.33f + org.y());
						m_points[i][4].pos.set(m_points[i][4].pos.x(), ((value * size.y()) / 3) + size.y() * 0.33f + org.y());
						m_points[i][5].pos.set(((value * size.x()) / 3) + org.x() + size.x() * 0.66f, m_points[i][5].pos.y());
						break;
					case 3:
						m_points[i][0].pos.set(m_points[i][0].pos.x(), ((value * size.y()) / 3) + org.y());
						m_points[i][3].pos.set(m_points[i][3].pos.x(), ((value * size.y()) / 3) + org.y());
						m_points[i][5].pos.set(m_points[i][5].pos.x(), ((value * size.y()) / 3) + org.y());
						break;
					}
				}

				// In all cases, we need to update
				has_damage = true;
				break;
			}
		}
	}

	has_damage = true;
	update_member(member_ptr);
	post_redraw();
}

bool tf_editor_scatterplot::init(cgv::render::context& ctx) {
	
	bool success = true;

	// initialize the offline frame buffer, canvases and point renderer
	success &= fbc.ensure(ctx);
	success &= fbc_plot.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_point_renderer.init(ctx);
	success &= font_renderer.init(ctx);
	success &= m_line_renderer.init(ctx);
	success &= m_draggables_renderer.init(ctx);

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

		create_labels();

		has_damage = true;
		reset_plot = true;
	}
}

void tf_editor_scatterplot::draw(cgv::render::context& ctx) {

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
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");

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
	font_renderer.render(ctx, get_overlay_size(), labels, 0, 3);
	//font_renderer.render(ctx, get_overlay_size(), labels, 1, 1);
	//font_renderer.render(ctx, get_overlay_size(), labels, 2, 1);

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
	font_renderer.render(ctx, get_overlay_size(), labels, 3, 6);
		
	// restore the previous view matrix
	content_canvas.pop_modelview_matrix(ctx);

	create_rectangles();

	// draw the grid rectangle
	auto& rect_prog = content_canvas.enable_shader(ctx, "rectangle");
	m_rectangle_style.apply(ctx, rect_prog);
	for (const auto rectangle : m_rectangles_draw) {
		content_canvas.draw_shape(ctx, rectangle.start, rectangle.end, m_rectangle_style.border_color);
	}
	content_canvas.disable_current_shader(ctx);

	draw_draggables(ctx);

	create_ellipses();
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
		// Strip borders
		m_ellipse_style.border_color = rgba{ m_shared_data_ptr->centroids.at(i).color, 1.0f };
		m_ellipse_style.fill_color = m_shared_data_ptr->centroids.at(i).color;

		auto& ellipse_prog = content_canvas.enable_shader(ctx, "ellipse");
		m_ellipse_style.apply(ctx, ellipse_prog);
		for (const auto ellipse : m_ellipses.at(i)) {
			content_canvas.draw_shape(ctx, ellipse.pos, ellipse.size, rgba(0, 1, 1, 1));
		}
		content_canvas.disable_current_shader(ctx);
	}

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = !done;
}

void tf_editor_scatterplot::create_gui() {

	create_overlay_gui();

	// add a button to trigger a content update by redrawing
	connect_copy(add_button("Update")->click, rebind(this, &tf_editor_scatterplot::update_content));
	// add controls for parameters
	add_member_control(this, "Threshold", threshold, "value_slider", "min=0;max=1;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Alpha", alpha, "value_slider", "min=0;max=1;step=0.0001;log=true;ticks=true");
	add_member_control(this, "Blur", blur, "value_slider", "min=0;max=20;step=0.0001;ticks=true");
	add_member_control(this, "Radius", radius, "value_slider", "min=0;max=10;step=0.0001;ticks=true");

	add_decorator("Stain Indices", "heading", "level=3;font_style=regular");
	add_decorator("", "separator", "h=2");
	//add_member_control(this, "X Index", x_idx, "value", "min=0;max=3;step=1");
	//add_member_control(this, "Y Index", y_idx, "value", "min=0;max=3;step=1");

	// Create new centroids
	auto const add_centroid_button = add_button("Add centroid");
	connect_copy(add_centroid_button->click, rebind(this, &tf_editor_scatterplot::add_centroids));

	// Add GUI controls for the centroid
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
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

	add_gui("", m_ellipse_style);
}

void tf_editor_scatterplot::init_styles(cgv::render::context& ctx) {

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
	m_point_style.use_blending = true;
	m_point_style.use_fill_color = false;
	m_point_style.apply_gamma = false;
	m_point_style.position_is_center = true;
	m_point_style.fill_color = rgba(rgb(0.0f), alpha);
	m_point_style.feather_width = blur;

	m_rectangle_style.border_color = rgba(0.4f, 0.4f, 0.4f, 1.0f);
	m_rectangle_style.use_blending = false;
	m_rectangle_style.apply_gamma = false;
	m_rectangle_style.use_fill_color = false;
	m_rectangle_style.ring_width = 2.0f;

	m_ellipse_style.use_blending = true;
	m_ellipse_style.use_fill_color = true;
	m_ellipse_style.apply_gamma = false;
	m_ellipse_style.ring_width = 0.0f;
	m_ellipse_style.border_width = 5.0f;

	// configure style for the plot labels
	cgv::glutil::shape2d_style text_style;
	text_style.fill_color = rgba(rgb(0.0f), 1.0f);
	text_style.border_color.alpha() = 0.0f;
	text_style.border_width = 0.333f;
	text_style.use_blending = true;
	text_style.apply_gamma = false;

	// draggables style
	m_draggable_style.position_is_center = true;
	m_draggable_style.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style.border_width = 1.5f;
	m_draggable_style.use_blending = true;

	m_draggable_style_interacted.position_is_center = true;
	m_draggable_style_interacted.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_draggable_style_interacted.border_width = 1.5f;
	m_draggable_style_interacted.use_blending = true;
	
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

void tf_editor_scatterplot::create_labels() {
	
	labels.clear();

	std::vector<std::string> texts = { "0", "1", "2", "3" };

	if(m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3) {
		texts = m_data_set_ptr->stain_names;
	}

	if(font.is_initialized()) {
		const auto x = domain.pos().x() + 100;
		const auto y = domain.pos().y() - label_space / 2;

		labels.add_text(texts[0], ivec2(domain.box.get_center().x() * 0.35f, y), cgv::render::TA_NONE);
		labels.add_text(texts[1], ivec2(domain.box.get_center().x(), y), cgv::render::TA_NONE);
		labels.add_text(texts[2], ivec2(domain.box.get_center().x() * 1.60f, y), cgv::render::TA_NONE);

		labels.add_text(texts[3], ivec2(domain.box.get_center().x() * 0.35f, y), cgv::render::TA_NONE);
		labels.add_text(texts[2], ivec2(domain.box.get_center().x(), y), cgv::render::TA_NONE);
		labels.add_text(texts[1], ivec2(domain.box.get_center().x() * 1.60f, y), cgv::render::TA_NONE);
	}
}

void tf_editor_scatterplot::update_content() {

	if(!m_data_set_ptr || m_data_set_ptr->voxel_data.empty())
		return;

	const auto& data = m_data_set_ptr->voxel_data;

	// reset previous total count and point geometry
	total_count = 0;
	m_point_geometry_data.clear();

	// setup plot origin and sizes
	vec2 org = static_cast<vec2>(domain.pos());
	vec2 size = domain.size();

	const auto add_point = [&](vec2 pos, float x, float y) {
		pos.set(x, y);
		pos *= size;
		pos += org;

		// add one point
		m_point_geometry_data.add(pos, rgba(rgb(0.0f), alpha));
	};

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

				add_point(pos, pos.x() * 0.33f, (pos.y() / 3.0f) + offset);
				offset += 0.33f;
			}
			// Second col
			// Salimus - Actin, Salimus - Obscurin
			offset = 0.0f;
			for (int i = 1; i < 3; i++) {
				vec2 pos(v[3], v[i]);

				add_point(pos, (pos.x() * 0.33f) + 0.33f, (pos.y() / 3.0f) + offset);
				offset += 0.33f;
			}
			// Obscurin - Actin
			vec2 pos(v[2], v[1]);

			add_point(pos, (pos.x() * 0.33f) + 0.66f, (pos.y() / 3.0f));
			offset += 0.33f;
		}
	}

	// tell the program that we need to update the content of this overlay
	has_damage = true;
	// request a redraw
	post_redraw();
}

void tf_editor_scatterplot::create_rectangles() {
	m_rectangles_draw.clear();

	const auto sizeX = domain.size().x();
	const auto sizeY = domain.size().y();

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.05f), vec2(sizeX * 0.38f, sizeY * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.05f), vec2(sizeX * 0.33f, sizeY * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.38f), vec2(sizeX * 0.38f, sizeY * 0.71f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.38f), vec2(sizeX * 0.33f, sizeY * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.71f), vec2(sizeX * 0.38f, sizeY * 1.05f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.05f, sizeY * 0.71f), vec2(sizeX * 0.33f, sizeY * 0.34f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.38f, sizeY * 0.05f), vec2(sizeX * 0.71f, sizeY * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.38f, sizeY * 0.05f), vec2(sizeX * 0.33f, sizeY * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.38f, sizeY * 0.38f), vec2(sizeX * 0.71f, sizeY * 0.71f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.38f, sizeY * 0.38f), vec2(sizeX * 0.33f, sizeY * 0.33f)));

	m_rectangles_calc.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.71f, sizeY * 0.05f), vec2(sizeX * 1.05f, sizeY * 0.38f)));
	m_rectangles_draw.push_back(tf_editor_shared_data_types::rectangle(vec2(sizeX * 0.71f, sizeY * 0.05f), vec2(sizeX * 0.33f, sizeY * 0.33f)));
}

void tf_editor_scatterplot::create_ellipses() {
	m_ellipses.clear();

	for (int i = 0; i < m_shared_data_ptr->centroids.size(); i++) {
		// For each centroid, we want to create the lines of the boundaries
		std::vector<tf_editor_shared_data_types::ellipse> ellipses;

		for (int j = 0; j < m_points.at(i).size(); j++) {

			const auto width_stain_x = m_shared_data_ptr->centroids.at(i).widths[m_points[i][j].m_stain_second];
			const auto width_stain_y = m_shared_data_ptr->centroids.at(i).widths[m_points[i][j].m_stain_first];

			const auto width_x = width_stain_x * m_points.at(i).at(j).parent_rectangle->size_x();
			const auto width_y = width_stain_y * m_points.at(i).at(j).parent_rectangle->size_y();

			const auto position = vec2(m_points.at(i).at(j).pos.x() - width_x / 2, m_points.at(i).at(j).pos.y() - width_y / 2);
			ellipses.push_back(tf_editor_shared_data_types::ellipse(position, vec2(width_x, width_y)));
			
			// ellipses.push_back(tf_editor_shared_data_types::ellipse(m_points.at(i).at(j).pos, vec2(width_x, width_y)));
		}

		m_ellipses.push_back(ellipses);
	}
}

void tf_editor_scatterplot::add_centroids() {
	if (m_shared_data_ptr->centroids.size() == 5) {
		return;
	}

	// Create a new centroid and store it
	tf_editor_shared_data_types::centroid centr;
	m_shared_data_ptr->centroids.push_back(centr);
	add_centroid_draggables();
	// Add a corresponding point for every centroid
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

void tf_editor_scatterplot::add_centroid_draggables() {
	std::vector<tf_editor_shared_data_types::point_scatterplot> points;
	const auto org = static_cast<vec2>(domain.pos());
	const auto size = domain.size();

	// Add the new centroid points to the scatter plot
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(0).start, 0, 3, &m_rectangles_calc.at(0)));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(1).start, 0, 2, &m_rectangles_calc.at(1)));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(2).start, 0, 1, &m_rectangles_calc.at(2)));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(3).start, 1, 3, &m_rectangles_calc.at(3)));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(4).start, 1, 2, &m_rectangles_calc.at(4)));
	points.push_back(tf_editor_shared_data_types::point_scatterplot(m_rectangles_calc.at(5).start, 2, 3, &m_rectangles_calc.at(5)));
	m_points.push_back(points);
}

bool tf_editor_scatterplot::draw_scatterplot(cgv::render::context& ctx) {

	// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	// make sure to reset the color buffer if we update the content from scratch
	if (total_count == 0 || reset_plot) {
		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		reset_plot = false;
	}

	// the amount of points that will be drawn in each step
	int count = 50000;

	// make sure to not draw more points than available
	if (total_count + count > m_point_geometry_data.get_render_count())
		count = m_point_geometry_data.get_render_count() - total_count;

	if (count > 0) {
		auto& point_prog = m_point_renderer.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_point_style.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(radius));
		point_prog.disable(ctx);
		m_point_renderer.render(ctx, PT_POINTS, m_point_geometry_data, total_count, count);
	}

	// accumulate the total amount of so-far drawn points
	total_count += count;

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc_plot.disable(ctx);

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	bool run = total_count < m_point_geometry_data.get_render_count();
	if (run) {
		post_redraw();
	} else {
		std::cout << "done" << std::endl;
	}
	return !run;
}

void tf_editor_scatterplot::draw_draggables(cgv::render::context& ctx) {
	for (int i = 0; i < m_shared_data_ptr->centroids.size(); ++i) {
		// Clear for each centroid because colors etc might change
		m_point_geometry.clear();
		m_point_geometry_interacted.clear();

		const auto color = m_shared_data_ptr->centroids.at(i).color;
		// Apply color to the centroids, always do full opacity
		m_draggable_style.fill_color = rgba{ color.R(), color.G(), color.B(), 1.0f };
		m_draggable_style_interacted.fill_color = rgba{ color.R(), color.G(), color.B(), 1.0f };

		for (int j = 0; j < m_points[i].size(); j++) {
			// Add the points based on if they have been interacted with
			std::find(m_interacted_points.begin(), m_interacted_points.end(), &m_points[i][j]) != m_interacted_points.end() ?
				m_point_geometry_interacted.add(m_points[i][j].get_render_position()) :
				m_point_geometry.add(m_points[i][j].get_render_position());
		}

		m_point_geometry.set_out_of_date();

		// Draw 
		shader_program& point_prog = m_draggables_renderer.ref_prog();
		point_prog.enable(ctx);
		content_canvas.set_view(ctx, point_prog);
		m_draggable_style.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(12.0f));
		point_prog.disable(ctx);
		m_draggables_renderer.render(ctx, PT_POINTS, m_point_geometry);

		point_prog.enable(ctx);
		m_draggable_style_interacted.apply(ctx, point_prog);
		point_prog.set_attribute(ctx, "size", vec2(16.0f));
		point_prog.disable(ctx);
		m_draggables_renderer.render(ctx, PT_POINTS, m_point_geometry_interacted);
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
			// Now the relating centroid point in the scatter plot has to be updated
			if (&m_points[i][j] == m_point_handles.get_dragged()) {
				m_interacted_points.push_back(&m_points[i][j]);
				const auto& found_point = m_points[i][j];

				m_interacted_point_id.first = i;
				m_interacted_point_id.second = j;

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

				const auto gui_value_first = m_points[i][j].get_relative_position(m_points[i][j].pos.x(), true);
				m_shared_data_ptr->centroids.at(i).centroids[m_points[i][j].m_stain_first] = gui_value_first;
				update_member(&m_shared_data_ptr->centroids.at(i).centroids[m_points[i][j].m_stain_first]);

				const auto gui_value_second = m_points[i][j].get_relative_position(m_points[i][j].pos.y(), false);
				m_shared_data_ptr->centroids.at(i).centroids[m_points[i][j].m_stain_second] = gui_value_second;
				update_member(&m_shared_data_ptr->centroids.at(i).centroids[m_points[i][j].m_stain_second]);

				m_interacted_id_set = true;
			}
		}
	}

	has_damage = true;
	post_redraw();
}

void tf_editor_scatterplot::find_clicked_centroid(int x, int y) {
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
		m_clicked_centroid_id = found_index;
		has_damage = true;
		post_redraw();
	}
}

void tf_editor_scatterplot::scroll_centroid_width(int x, int y, bool negative_change, bool ctrl_pressed) {
	auto found = false;
	int found_index;
	// Search through all polygons
	for (int i = 0; i < m_rectangles_calc.size(); i++) {
		// If we found a polygon, update the corresponding width
		if (m_rectangles_calc.at(i).is_inside(x, y)) {

			auto& centroids = m_shared_data_ptr->centroids[m_clicked_centroid_id];
			auto& width = centroids.widths[ctrl_pressed ? m_points[m_clicked_centroid_id][i].m_stain_first : m_points[m_clicked_centroid_id][i].m_stain_second];
			// auto width = 0.0f;
			width += negative_change ? 0.02f : -0.02f;
			width = cgv::math::clamp(width, 0.0f, 1.0f);

			found = true;
			found_index = i;
			break;
		}
	}
	// If we found something, we have to set the corresponding point ids and redraw
	if (found) {
		m_interacted_id_set = true;

		has_damage = true;
		post_recreate_gui();
		post_redraw();
	}
}
