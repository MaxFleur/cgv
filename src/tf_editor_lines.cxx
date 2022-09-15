/** BEGIN - MFLEURY **/

#include "tf_editor_lines.h"

#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

#include "tf_editor_shared_functions.h"

tf_editor_lines::tf_editor_lines() {
	set_name("TF Editor Lines Overlay");
	// set the size with an aspect ratio that makes lets the editor nicely fit inside
	// aspect ratio is w:h = 1:0.875
	set_overlay_size(ivec2(700, 612));
	// Register an additional arrow shader
	content_canvas.register_shader("arrow", cgv::glutil::canvas::shaders_2d::arrow);
	content_canvas.register_shader("quad", cgv::glutil::canvas::shaders_2d::quad);

	// initialize the renderers
	m_renderer_lines = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::line);
	m_renderer_quads = cgv::glutil::generic_renderer(cgv::glutil::canvas::shaders_2d::quad);
	m_renderer_quads_gauss = cgv::glutil::generic_renderer("gauss_quad2d.glpr");

	// callbacks for the moving of draggables
	m_point_handles.set_drag_callback(std::bind(&tf_editor_lines::set_point_positions, this));
	m_point_handles.set_drag_end_callback(std::bind(&tf_editor_lines::end_drag, this));
}

void tf_editor_lines::clear(cgv::render::context& ctx) {
	m_renderer_lines.destruct(ctx);
	m_geometry_relations.destruct(ctx);
	m_geometry_widgets.destruct(ctx);
	m_geometry_strip_borders.destruct(ctx);

	cgv::glutil::ref_msdf_font(ctx, -1);
	cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx, -1);

	m_renderer_quads.destruct(ctx);
	m_renderer_quads_gauss.destruct(ctx);

	m_renderer_draggables_circle.destruct(ctx);
	m_renderer_draggables_rectangle.destruct(ctx);
	m_geometry_draggables.destruct(ctx);
	m_geometry_draggables_interacted.destruct(ctx);
}

bool tf_editor_lines::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();

	if(et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;

		if (ke.get_action() == cgv::gui::KA_PRESS) {
			if (ke.get_key() == cgv::gui::KEY_Space && is_hit(ivec2(ke.get_x(), ke.get_y()))) {
				is_peak_mode = !is_peak_mode;
				// User might have adjusted many values while peak mode was active, so recreate everything
				if (!is_peak_mode) {
					m_create_all_values = true;
				}

				redraw();
				return true;
			}
			else {
				return tf_editor_basic::handle_key_input(ke.get_key(), m_interacted_primitive_ids[0]);
			}
		}
	} else if(et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;

		const auto mouse_pos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));

		// Reset dragging by clicking the left mouse
		if (me.get_action() == cgv::gui::MA_PRESS && me.get_button() == cgv::gui::MB_LEFT_BUTTON && !m_currently_dragging) {
			// Check for a double click
			auto is_double_clicked = false;
			if ((me.get_time() - m_click_time) < 0.3) {
				is_double_clicked = true;
			}
			m_click_time = me.get_time();
			point_clicked(mouse_pos, is_double_clicked);
		}
		// Set width if a scroll is done
		else if(me.get_action() == cgv::gui::MA_WHEEL && is_interacting) {
			const auto negative_change = me.get_dy() > 0 ? true : false;
			const auto shift_pressed = e.get_modifiers() & cgv::gui::EM_SHIFT ? true : false;

			scroll_centroid_width(mouse_pos.x(), mouse_pos.y(), negative_change, shift_pressed);
		}

		auto handled = false;
		handled |= m_point_handles.handle(e, last_viewport_size, container);

		if(handled)
			post_redraw();

		return handled;
	}

	return false;
}

void tf_editor_lines::on_set(void* member_ptr) {
	// react to changes of the line alpha parameter and update the styles
	if(member_ptr == &m_alpha) {
		if(auto ctx_ptr = get_context())
			init_styles(*ctx_ptr);
	}
	// Update if the vis mode is changed
	if(member_ptr == &vis_mode) {
		update_content();
	}

	update_member(member_ptr);
	redraw();
}

bool tf_editor_lines::init(cgv::render::context& ctx) {

	auto success = true;

	// initialize the offline frame buffer, canvases and line renderer
	success &= fbc.ensure(ctx);
	success &= fbc_plot.ensure(ctx);
	success &= content_canvas.init(ctx);
	success &= viewport_canvas.init(ctx);
	success &= m_renderer_lines.init(ctx);
	success &= m_renderer_draggables_circle.init(ctx);
	success &= m_renderer_draggables_rectangle.init(ctx);
	success &= m_renderer_quads.init(ctx);
	success &= m_renderer_quads_gauss.init(ctx);

	auto& font = cgv::glutil::ref_msdf_font(ctx, 1);
	cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx, 1);

	// when successful, initialize the styles used for the individual shapes
	if(success)
		init_styles(ctx);

	// setup the font type and size to use for the label geometry
	if(font.is_initialized()) {
		m_labels.set_msdf_font(&font);
		m_labels.set_font_size(m_font_size);
	}

	return success;
}

void tf_editor_lines::init_frame(cgv::render::context& ctx) {

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

		create_widget_lines();
		add_widget_lines();

		m_point_handles.set_constraint(domain);

		create_labels();

		update_point_positions();

		// update the quad positions after a resize
		m_create_all_values = true;

		has_damage = true;
		m_reset_plot = true;
	}
}

void tf_editor_lines::create_gui() {

	create_overlay_gui();

	tf_editor_basic::create_gui_basic();
	tf_editor_basic::create_gui_coloring();
	tf_editor_basic::create_gui_tm();
}

// Called if something in the primitives has been updated
void tf_editor_lines::resynchronize() {
	m_interacted_primitive_ids[0] = m_shared_data_ptr->selected_primitive_id;

	// Clear and readd points
	m_points.clear();
	for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
		add_draggables(i);
	}

	m_create_all_values = true;
	// Then redraw strips etc
	redraw();
}

void tf_editor_lines::primitive_added() {
	add_draggables(m_shared_data_ptr->primitives.size() - 1);

	m_interacted_primitive_ids[0] = m_shared_data_ptr->selected_primitive_id;
	// A new primitive was added, so we need to redraw completely
	m_create_all_values = true;

	redraw();
}

void tf_editor_lines::init_styles(cgv::render::context& ctx) {

	// configure style for rendering the plot framebuffer texture
	m_style_plot.use_texture = true;
	m_style_plot.use_blending = true;
	m_style_plot.feather_width = 0.0f;

	m_style_relations.use_blending = true;
	m_style_relations.use_fill_color = false;
	m_style_relations.width = 1.0f;

	m_style_widgets.use_blending = true;
	m_style_widgets.use_fill_color = true;
	m_style_widgets.fill_color = m_gray_widgets;
	m_style_widgets.width = 1.0f;

	m_style_quads.use_blending = true;
	m_style_quads.use_fill_color = false;
	m_style_quads.fill_color = rgba(1.0f, 0.0f, 0.0f, m_alpha);

	m_style_strip_borders.use_blending = true;
	m_style_strip_borders.use_fill_color = false;
	m_style_strip_borders.border_width = 1.5f;

	auto& quad_prog = m_renderer_quads.enable_prog(ctx);
	m_style_quads.apply(ctx, quad_prog);
	quad_prog.disable(ctx);
		
	auto& quad_prog_gauss = m_renderer_quads_gauss.enable_prog(ctx);
	m_style_quads.apply(ctx, quad_prog_gauss);
	quad_prog_gauss.disable(ctx);

	m_style_draggables.position_is_center = true;
	m_style_draggables.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables.border_width = 1.5f;
	m_style_draggables.use_blending = true;
	m_style_draggables.use_fill_color = false;

	m_style_draggables_interacted.position_is_center = true;
	m_style_draggables_interacted.border_color = rgba(0.2f, 0.2f, 0.2f, 1.0f);
	m_style_draggables_interacted.border_width = 1.5f;
	m_style_draggables_interacted.use_blending = true;
	m_style_draggables_interacted.use_fill_color = false;

	m_style_arrows.head_width = 10.0f;
	m_style_arrows.relative_head_length = 1.0f;
	m_style_arrows.head_length_is_relative = true;
	m_style_arrows.stem_width = 0.0f;
	m_style_arrows.feather_width = 1.0f;
	m_style_arrows.fill_color = m_gray_arrows;
	m_style_arrows.border_color = m_gray_arrows;
	m_style_arrows.use_fill_color = true;
	m_style_arrows.use_blending = true;

	cgv::glutil::shape2d_style plot_rect_style;
	plot_rect_style.use_texture = true;
	auto& rectangle_prog = content_canvas.enable_shader(ctx, "rectangle");
	plot_rect_style.apply(ctx, rectangle_prog);
	content_canvas.disable_current_shader(ctx);

	//cgv::glutil::shape2d_style text_style;
	m_style_text.fill_color = rgba(rgb(0.0f), 1.0f);
	m_style_text.border_color.alpha() = 0.0f;
	m_style_text.border_width = 0.333f;
	m_style_text.use_blending = true;

	// configure style for final blending of whole overlay
	overlay_style.fill_color = rgba(1.0f);
	overlay_style.use_texture = true;
	overlay_style.use_blending = false;
	overlay_style.border_color = rgba(rgb(0.5f), 1.0f);
	overlay_style.border_width = 3.0f;
	overlay_style.feather_width = 0.0f;

	// also does not change, so is only set whenever this method is called
	auto& overlay_prog = viewport_canvas.enable_shader(ctx, "rectangle");
	overlay_style.apply(ctx, overlay_prog);
	viewport_canvas.disable_current_shader(ctx);
}

void tf_editor_lines::update_content() {

	if(!m_data_set_ptr || m_data_set_ptr->voxel_data.empty())
		return;

	const auto& data = m_data_set_ptr->voxel_data;
	// Everything needs to be updated, also nothing is dragged the moment the button is pressed
	m_create_all_values = true;
	m_is_point_dragged = false;

	// reset previous total count and line geometry
	m_total_count = 0;
	m_geometry_relations.clear();

	// for each given sample of 4 protein densities, do:
	for(size_t i = 0; i < data.size(); ++i) {
		const auto& v = data[i];

		// calculate the average to allow filtering with the given threshold
		auto avg = (v[0] + v[1] + v[2] + v[3]) * 0.25f;

		if(avg > m_threshold) {
			rgb color_rgb(0.0f);
			if(vis_mode == VM_GTF) {
				color_rgb = tf_editor_shared_functions::get_color(v, m_shared_data_ptr->primitives);
			}
			// Use full alpha for enabled tone mapping
			rgba col(color_rgb, 1.0f);

			// Left to right
			m_geometry_relations.add(m_widget_lines.at(0).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(6).interpolate(v[1]), col);
			// Left to center
			m_geometry_relations.add(m_widget_lines.at(1).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(12).interpolate(v[3]), col);
			// Left to bottom
			m_geometry_relations.add(m_widget_lines.at(2).interpolate(v[0]), col);
			m_geometry_relations.add(m_widget_lines.at(8).interpolate(v[2]), col);
			// Right to bottom
			m_geometry_relations.add(m_widget_lines.at(4).interpolate(v[1]), col);
			m_geometry_relations.add(m_widget_lines.at(10).interpolate(v[2]), col);
			// Right to center
			m_geometry_relations.add(m_widget_lines.at(5).interpolate(v[1]), col);
			m_geometry_relations.add(m_widget_lines.at(13).interpolate(v[3]), col);
			// Bottom to center
			m_geometry_relations.add(m_widget_lines.at(9).interpolate(v[2]), col);
			m_geometry_relations.add(m_widget_lines.at(14).interpolate(v[3]), col);
		}
	}
	// content was updated, so redraw
	redraw();
}

void tf_editor_lines::create_labels() {

	m_labels.clear();

	// Set the font texts
	if(auto ctx_ptr = get_context()) {
		auto& font = cgv::glutil::ref_msdf_font(*ctx_ptr);
		if(font.is_initialized() && m_widget_polygons.size() > 3) {
			vec2 centers[4];
			for(int i = 0; i < 4; i++)
				centers[i] = m_widget_polygons[i].get_center();

			m_labels.add_text("0", ivec2(centers[0]), cgv::render::TA_NONE, 60.0f);
			m_labels.add_text("1", ivec2(centers[1]), cgv::render::TA_NONE, -60.0f);
			m_labels.add_text("2", ivec2(centers[2]), cgv::render::TA_NONE, 0.0);
			m_labels.add_text("3", ivec2(centers[3]), cgv::render::TA_NONE, 0.0f);

			if(m_data_set_ptr && m_data_set_ptr->stain_names.size() > 3) {
				for(int i = 0; i < 4; i++) {
					m_labels.set_text(i, m_data_set_ptr->stain_names[i]);
				}
			}
		}
	}
}

void tf_editor_lines::create_widget_lines() {
	m_widget_lines.clear();
	m_widget_polygons.clear();

	// Sizing constants for the widgets
	const float a = 1.0f; // Distance from origin (center of central triangle) to its corners
	const float b = 1.0f; // Orthogonal distance from center widget line to the outer widget line
	const float c = 2.0f * sin(cgv::math::deg2rad(60.0f)) * b; // distance between two opposing outer widget lines

	// Constant rotation matrices
	const mat2 R = cgv::math::rotate2(120.0f);
	const mat2 R2 = cgv::math::rotate2(240.0f);
	const mat2 R_half = cgv::math::rotate2(60.0f);
	// Help vector
	const vec2 up(0.0f, 1.0f);

	// Final points
	std::vector<vec2> center(3), left(4), bottom(4), right(4);

	// Create center widget points
	center[0] = a * up;
	center[1] = R * center[0];
	center[2] = R2 * center[0];

	// Calculate help directions
	vec2 b_dir = normalize(R_half * up);
	vec2 c_dir = normalize(center[1]);

	// Create left outer widget corners
	left[1] = center[0] + b * b_dir;
	left[2] = center[1] + b * b_dir;
	left[0] = left[1] + c * up;
	left[3] = left[2] + c * c_dir;

	// Rotate corners of left widget to get the other outer widget points
	for(size_t i = 0; i < 4; ++i) {
		bottom[i] = R * left[i];
		right[i] = R2 * left[i];
	}

	// Calculate the bounding box of all the corners
	box2 box;

	for(size_t i = 0; i < 3; ++i)
		box.add_point(center[i]);

	for(size_t i = 0; i < 4; ++i) {
		box.add_point(left[i]);
		box.add_point(bottom[i]);
		box.add_point(right[i]);
	}

	// Offset applied before scaling (in unit coordinates) to move the widget center of gravity to the origin
	vec2 center_offset = -box.get_center();

	// Offset applied after scaling (in pixel coordinates) to move the widgets to the center of the domain
	vec2 offset = domain.box.get_center();

	// Factor to scale from unit to pixel coordinates
	float scale = std::min(domain.size().x() / box.get_extent().x(), domain.size().y() / box.get_extent().y());

	// Apply offsets and scale
	for(size_t i = 0; i < 3; ++i) {
		center[i] = scale * (center[i] + center_offset) + offset;
		center[i] = cgv::math::round(center[i]) + 0.5f;
	}

	for(size_t i = 0; i < 4; ++i) {
		left[i] = scale * (left[i] + center_offset) + offset;
		bottom[i] = scale * (bottom[i] + center_offset) + offset;
		right[i] = scale * (right[i] + center_offset) + offset;

		left[i] = cgv::math::round(left[i]) + 0.5f;
		bottom[i] = cgv::math::round(bottom[i]) + 0.5f;
		right[i] = cgv::math::round(right[i]) + 0.5f;
	}

	// Helper function to add a new line
	const auto add_line = [&](vec2 v_0, vec2 v_1) {
		m_widget_lines.push_back(tf_editor_shared_data_types::line({ v_0, v_1 }));
	};

	// Left widget
	add_line(left[0], left[1]);
	add_line(left[2], left[1]);
	add_line(left[3], left[2]);
	add_line(left[3], left[0]);

	// Right widget
	add_line(right[1], right[0]);
	add_line(right[2], right[1]);
	add_line(right[3], right[2]);
	add_line(right[3], right[0]);

	// Bottom widget
	add_line(bottom[0], bottom[1]);
	add_line(bottom[1], bottom[2]);
	add_line(bottom[2], bottom[3]);
	add_line(bottom[3], bottom[0]);

	// Center widget, order: Left, right, bottom
	add_line(center[1], center[0]);
	add_line(center[0], center[2]);
	add_line(center[1], center[2]);

	// Create a polygon out of each widget
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(left));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(right));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(bottom));
	m_widget_polygons.push_back(tf_editor_shared_data_types::polygon(center));

	// draw smaller boundaries on the relations borders
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets which basically is the 4th line, they don't need boundaries
		if((i + 1) % 4 != 0) {
			const auto direction = normalize(m_widget_lines.at(i).b - m_widget_lines.at(i).a);
			const auto ortho_direction = cgv::math::ortho(direction);

			const auto boundary_left = m_widget_lines.at(i).interpolate(0.0f);
			const auto boundary_right = m_widget_lines.at(i).interpolate(1.0f);

			add_line(boundary_left - 4.0f * ortho_direction, boundary_left + 4.0f * ortho_direction);
		}
	}
}

void tf_editor_lines::create_centroid_boundaries() {
	float boundary_left;
	float boundary_right;

	const auto calculate_values = [&](int i, int protein_index) {
		// Get the relative position of the centroid and its left and right boundary
		const auto relative_position = m_shared_data_ptr->primitives.at(i).centroid[protein_index];
		boundary_left = relative_position - (m_shared_data_ptr->primitives.at(i).widths[protein_index] / 2.0f);
		boundary_right = relative_position + (m_shared_data_ptr->primitives.at(i).widths[protein_index] / 2.0f);
	};

	// The case where all values have to be drawn
	if(m_create_all_values) {
		m_strip_boundary_points.clear();

		for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
			// For each centroid, we want to create the lines of the boundaries
			std::vector<float> centroid_boundary_values;

			// Each widget has three points which will always generate the same values, 
			// so do less calculations by doing only every 4th point
			for(int j = 0; j < m_points.at(i).size(); j += 3) {
				// Get the correct protein
				calculate_values(i, j / 3);

				// Store the values
				centroid_boundary_values.push_back(boundary_left);
				centroid_boundary_values.push_back(boundary_right);
			}

			// Now the strips
			auto boundary_index = 0;
			std::vector<vec2> strip_coordinates;

			// Iterate over widgets, ignore the back widget as usual
			for(int i = 0; i < 15; i += 4) {
				for(int j = 0; j < 3; j++) {
					const auto vec_left = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index));
					const auto vec_right = m_widget_lines.at(i + j).interpolate(centroid_boundary_values.at(boundary_index + 1));

					// Push back two vectors for the corresponding widget line
					strip_coordinates.push_back(vec_left);
					strip_coordinates.push_back(vec_right);
				}
				boundary_index += 2;
			}

			m_strip_boundary_points.push_back(strip_coordinates);
		}
		m_create_all_values = false;
	}
	// If a centroid's position is dragged in the editor, it would make no sense to redraw everything
	// So we redraw only for the positions that were updated
	else if(m_is_point_dragged) {
		// Recalculate values
		calculate_values(m_interacted_primitive_ids[0], m_interacted_primitive_ids[1] / 3);

		// No need to iterate over the first array entry
		for(int i = 1; i < 4; i++) {
			// Get the overall primitive layer and the parent line
			const auto primitive_layer = m_interacted_primitive_ids[0];
			auto& line = m_points[primitive_layer][m_interacted_primitive_ids[i]].m_parent_line;

			const auto id_left = m_interacted_primitive_ids[i] * 2;
			const auto id_right = m_interacted_primitive_ids[i] * 2 + 1;
			// Update the other centroids belonging to the widget as well
			m_strip_boundary_points[primitive_layer][id_left] = line->interpolate(boundary_left);
			m_strip_boundary_points[primitive_layer][id_right] = line->interpolate(boundary_right);
		}
	}
}

void tf_editor_lines::create_quads() {
	// Don't do anything if there are no points yet
	if(m_points.empty()) {
		return;
	}
	create_centroid_boundaries();

	// Only draw quads for the shape mode
	if(vis_mode == VM_SHAPES) {
		m_quad_strips.clear();
		quad_counts.clear();
		auto quad_count = 0;

		const auto texture_distance = [&](int primitive_index, int protein_index, int widget_line_index) {
			// Get position and width of the current primitive protein index
			const auto& primitive = m_shared_data_ptr->primitives.at(primitive_index);
			const auto pos = primitive.centroid[protein_index];
			const auto half_width = primitive.widths[protein_index] / 2.0f;

			const auto lower = pos - half_width;
			const auto upper = pos + half_width;

			auto t0 = (0.0f - lower) / (pos - lower);
			auto t1 = (1.0f - pos) / (upper - pos);
			t0 = cgv::math::clamp(t0, 0.0f, 1.0f);
			t1 = cgv::math::clamp(t1, 0.0f, 1.0f);
			
			return vec2(
				cgv::math::lerp(0.0f, 0.5f, t0),
				cgv::math::lerp(0.5f, 1.0f, t1)
			);
		};

		const auto add_quad = [&](tf_editor_shared_data_types::quad_geometry& geom,
			int protein_index_left, int protein_index_right, int widget_line_index_left, int widget_line_index_right,
			int strip_id_1, int strip_id_2, int strip_id_3, int strip_id_4, int i, rgba color) {

			const auto& widths = m_shared_data_ptr->primitives.at(i).widths;
			// No quads for maximum widths
			if (widths[protein_index_left] != 10.0f && widths[protein_index_right] != 10.0f) {
				vec2 texture_position_01 = texture_distance(i, protein_index_left, widget_line_index_left);
				vec2 texture_position_23 = texture_distance(i, protein_index_right, widget_line_index_right);

				const auto& bp = m_strip_boundary_points[i];

				geom.add(
					bp[strip_id_1], bp[strip_id_2], bp[strip_id_3], bp[strip_id_4],
					color,
					vec4(texture_position_01[0], texture_position_01[1], texture_position_23[0], texture_position_23[1])
				);
				// Quad is created, so increment
				quad_count++;
			}
		};

		// Now quads themselves
		for(int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
			tf_editor_shared_data_types::quad_geometry quad_strips;
			const auto color = m_shared_data_ptr->primitives.at(i).color;
			quad_count = 0;

			add_quad(m_quad_strips, 0, 1, 0, 6, 0, 1, 10, 11, i, color);
			add_quad(m_quad_strips, 0, 3, 12, 1, 18, 19, 2, 3, i, color);
			add_quad(m_quad_strips, 0, 2, 8, 2, 12, 13, 4, 5, i, color);
			add_quad(m_quad_strips, 1, 2, 10, 4, 16, 17, 6, 7, i, color);
			add_quad(m_quad_strips, 1, 3, 13, 5, 20, 21, 8, 9, i, color);
			add_quad(m_quad_strips, 2, 3, 9, 14, 14, 15, 22, 23, i, color);
			// Store the number of created quads
			quad_counts.push_back(quad_count);
		}
	}
}

void tf_editor_lines::create_strip_borders(int index) {
	// if there are no values yet, do not do anything
	if(m_strip_boundary_points.at(index).empty()) {
		return;
	}

	m_geometry_strip_borders.clear();

	const auto add_indices_to_strip_borders = [&](int index, int a, int b, int width_left, int width_right) {
		const auto& widths = m_shared_data_ptr->primitives.at(index).widths;
		// We don't want to draw lines for a maximum value
		if (widths[width_left] != 10.0f && widths[width_right] != 10.0f) {
			m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(a), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
			m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(b), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
			m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(a + 1), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
			m_geometry_strip_borders.add(m_strip_boundary_points.at(index).at(b + 1), rgba{ m_shared_data_ptr->primitives.at(index).color, 1.0f });
		}
	};

	add_indices_to_strip_borders(index, 0, 10, 0, 1);
	add_indices_to_strip_borders(index, 2, 18, 0, 3);
	add_indices_to_strip_borders(index, 4, 12, 0, 2);
	add_indices_to_strip_borders(index, 6, 16, 1, 2);
	add_indices_to_strip_borders(index, 8, 20, 1, 3);
	add_indices_to_strip_borders(index, 14, 22, 2, 3);
}

void tf_editor_lines::add_widget_lines() {
	m_geometry_widgets.clear();

	for(const auto l : m_widget_lines) {
		m_geometry_widgets.add(l.a, m_gray_widgets);
		m_geometry_widgets.add(l.b, m_gray_widgets);
	}
}

void tf_editor_lines::add_draggables(int primitive_index) {
	std::vector<tf_editor_shared_data_types::point_line> points;
	// Add the new draggable points to the widget lines, start with the left side
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets
		if((i + 1) % 4 != 0) {
			int index;
			if(i < 3) {
				index = 0;
			} else if(i >= 4 && i < 7) {
				index = 1;
			} else if(i >= 8 && i < 11) {
				index = 2;
			} else if(i >= 12 && i < 15) {
				index = 3;
			}

			const auto value = m_shared_data_ptr->primitives.at(primitive_index).centroid[index];
			points.push_back(tf_editor_shared_data_types::point_line(vec2(m_widget_lines.at(i).interpolate(value)), &m_widget_lines.at(i)));
		}
	}
	m_points.push_back(points);

	tf_editor_basic::set_point_handles(m_points, m_point_handles);
}

void tf_editor_lines::draw_content(cgv::render::context& ctx) {

	// enable the OpenGL blend functionalities
	glEnable(GL_BLEND);
	// setup a suitable blend function for color and alpha values (following the over-operator)
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// first draw the plot (the method will check internally if it needs an update)
	bool done = draw_plot(ctx);

	// enable the offline framebuffer
	fbc.enable(ctx);
	glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// draw the plot content from its own framebuffer texture
	fbc_plot.enable_attachment(ctx, "color", 0);

	auto& rectangle_prog = content_canvas.enable_shader(ctx, "plot_tone_mapping");
	// Apply tone mapping via the tone mapping shader
	rectangle_prog.set_uniform(ctx, "normalization_factor", 1.0f / static_cast<float>(std::max(tm_normalization_count, 1u)));
	rectangle_prog.set_uniform(ctx, "alpha", tm_alpha);
	rectangle_prog.set_uniform(ctx, "use_color", vis_mode == VM_GTF);
	m_style_plot.apply(ctx, rectangle_prog);

	content_canvas.draw_shape(ctx, ivec2(0), get_overlay_size());
	content_canvas.disable_current_shader(ctx);
	fbc_plot.disable_attachment(ctx, "color");

	// draw widget lines first
	auto& line_prog = m_renderer_lines.ref_prog();
	line_prog.enable(ctx);
	content_canvas.set_view(ctx, line_prog);
	m_style_widgets.apply(ctx, line_prog);
	line_prog.disable(ctx);
	m_renderer_lines.render(ctx, PT_LINES, m_geometry_widgets);

	// then arrows on top
	auto& arrow_prog = content_canvas.enable_shader(ctx, "arrow");
	m_style_arrows.apply(ctx, arrow_prog);
	// draw arrows indicating where the maximum value is
	for(int i = 0; i < 15; i++) {
		// ignore the "back" lines of the widgets, they don't need arrows
		if((i + 1) % 4 != 0) {
			const auto& l = m_widget_lines.at(i);
			vec2 a = l.interpolate(1.0f);
			vec2 b = a + 10.0f*normalize(l.b - l.a);
			content_canvas.draw_shape2(ctx, a, b);
		}
	}
	content_canvas.disable_current_shader(ctx);

	// Do not draw quads and the border lines for peak mode
	if (!is_peak_mode) {
		// Now create the centroid boundaries and strips
		create_quads();

		if (vis_mode == VM_SHAPES) {
			auto quad_index_start = 0;
			for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
				const auto& type = m_shared_data_ptr->primitives.at(i).type;
				auto& quad_renderer = type == shared_data::TYPE_GAUSS ? m_renderer_quads_gauss : m_renderer_quads;

				content_canvas.set_view(ctx, quad_renderer.enable_prog(ctx));
				// Render the number of quads stored for each primitive
				quad_renderer.render(ctx, PT_POINTS, m_quad_strips, quad_index_start, quad_counts.at(i));
				quad_index_start += quad_counts.at(i);
			}
		}

		for (int i = 0; i < m_shared_data_ptr->primitives.size(); i++) {
			// Strip borders
			create_strip_borders(i);

			line_prog.enable(ctx);
			content_canvas.set_view(ctx, line_prog);
			const auto color = m_shared_data_ptr->primitives.at(i).color;
			m_style_strip_borders.border_color = rgba{ color.R(), color.G(), color.B(), 1.0f };
			m_style_strip_borders.apply(ctx, line_prog);
			line_prog.disable(ctx);
			m_renderer_lines.render(ctx, PT_LINES, m_geometry_strip_borders);
		}
	}

	// then labels
	auto& font_renderer = cgv::glutil::ref_msdf_gl_canvas_font_renderer(ctx);
	font_renderer.render(ctx, content_canvas, m_labels, m_style_text);

	// draggables are the last thing to be drawn so they are above everything else
	tf_editor_basic::draw_draggables(ctx, m_points, m_interacted_primitive_ids[0]);

	fbc.disable(ctx);

	// don't forget to disable blending
	glDisable(GL_BLEND);

	has_damage = !done;
}

bool tf_editor_lines::draw_plot(cgv::render::context& ctx) {

	// enable the offline plot frame buffer, so all things are drawn into its attached textures
	fbc_plot.enable(ctx);

	glBlendFunc(GL_ONE, GL_ONE);

	// make sure to reset the color buffer if we update the content from scratch
	if(m_total_count == 0 || m_reset_plot) {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		m_reset_plot = false;
	}

	// the amount of lines that will be drawn in each step
	auto count = 1000000;

	// make sure not to draw more lines than available
	if(m_total_count + count > m_geometry_relations.get_render_count())
		count = m_geometry_relations.get_render_count() - m_total_count;
	// draw the relations
	if(count > 0) {
		auto& line_prog = m_renderer_lines.ref_prog();
		line_prog.enable(ctx);
		content_canvas.set_view(ctx, line_prog);
		m_style_relations.apply(ctx, line_prog);
		line_prog.disable(ctx);
		m_renderer_lines.render(ctx, PT_LINES, m_geometry_relations, m_total_count, count);
	}

	// accumulate the total amount of so-far drawn lines
	m_total_count += count;

	// disable the offline frame buffer so subsequent draw calls render into the main frame buffer
	fbc_plot.disable(ctx);

	// reset the blend function
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// Stop the process if we have drawn all available lines,
	// otherwise request drawing of another frame.
	auto run = m_total_count < m_geometry_relations.get_render_count();
	if(run) {
		post_redraw();
	}
	return !run;
}

void tf_editor_lines::set_point_positions() {
	// Update original value
	m_point_handles.get_dragged()->update_val();
	m_currently_dragging = true;

	for(unsigned i = 0; i < m_points.size(); ++i) {
		for(int j = 0; j < m_points[i].size(); j++) {
			// Now the relating draggables in the widget have to be updated
			if(&m_points[i][j] == m_point_handles.get_dragged()) {
				handle_interacted_primitive_ids(i, j, true);

				m_is_point_dragged = true;
				is_interacting = true;

				int protein_index = j / 3;
				// Remap to correct GUI vals
				const auto GUI_value = (m_points[i][j].get_relative_line_position() - 0.1f) / 0.8f;
				m_shared_data_ptr->primitives.at(i).centroid[protein_index] = GUI_value;
				update_member(&m_shared_data_ptr->primitives.at(i).centroid[protein_index]);

				m_shared_data_ptr->set_synchronized();
			}
		}
	}

	redraw();
}

void tf_editor_lines::update_point_positions() {
	// update the point positions after a resize
	if(m_shared_data_ptr && m_points.size() == m_shared_data_ptr->primitives.size()) {
		for(size_t i = 0; i < m_points.size(); ++i) {
			auto& points = m_points[i];
			auto& centroid = m_shared_data_ptr->primitives[i];

			size_t idx = 0;
			if(points.size() >= 4 * 3) {
				for(int j = 0; j < 15; j++) {
					// ignore the "back" lines of the widgets
					if((j + 1) % 4 != 0) {
						float c = centroid.centroid[j / 4];

						points[idx].pos = m_widget_lines.at(j).interpolate(c);
						++idx;
					}
				}
			}
		}
	}
}

void tf_editor_lines::point_clicked(const vec2& mouse_pos, bool double_clicked) {
	m_is_point_dragged = false;
	auto found = false;

	for (unsigned i = 0; i < m_points.size(); ++i) {
		for (int j = 0; j < m_points[i].size(); j++) {
			// Now the relating draggables in the widget have to be updated
			if (m_points[i][j].is_inside(mouse_pos)) {
				handle_interacted_primitive_ids(i, j);
				found = true;
			}
		}
	}

	tf_editor_basic::handle_mouse_click_end(found, double_clicked, m_interacted_primitive_ids[0], 
											m_interacted_primitive_ids[1] / 3, m_interacted_primitive_ids[0], true);
}

void tf_editor_lines::scroll_centroid_width(int x, int y, bool negative_change, bool shift_pressed) {
	auto found = false;
	int found_index;
	// Search through all polygons
	for(int i = 0; i < m_widget_polygons.size(); i++) {
		// If we found a polygon, update the corresponding width
		if(m_widget_polygons.at(i).is_point_in_polygon(x, y)) {
			// Stronger change for pressed shift
			auto change = shift_pressed ? 0.05f : 0.02f;
			if(negative_change) {
				change *= -1.0f;
			}
			auto& width = m_shared_data_ptr->primitives[m_interacted_primitive_ids[0]].widths[i];
			width += change;
			width = cgv::math::clamp(width, 0.0f, 10.0f);
			
			if (width == 10.0f) {
				m_shared_data_ptr->primitives[m_interacted_primitive_ids[0]].last_masked_id = i;
			}
			found = true;
			found_index = i;
			break;
		}
	}
	// If we found something, we have to set the corresponding point ids and redraw
	if(found) {
		m_is_point_dragged = true;
		tf_editor_shared_functions::set_interacted_centroid_ids(m_interacted_primitive_ids, found_index);

		m_shared_data_ptr->set_synchronized();
		redraw();
	}
}

void tf_editor_lines::handle_interacted_primitive_ids(int i, int j, bool set_corresponding_points) {
	const auto set_points = [&](int index_row, int index_col, int pos_1, int pos_2, bool set_corresponding_points) {
		const auto relative_position = (m_points[index_row][index_col].get_relative_line_position() - 0.1f) / 0.8f;
		if (set_corresponding_points) {
			m_points[index_row][index_col + pos_1].move_along_line(relative_position);
			m_points[index_row][index_col + pos_2].move_along_line(relative_position);
		}
		m_interacted_primitive_ids[2] = index_col + pos_1;
		m_interacted_primitive_ids[3] = index_col + pos_2;
	};
	
	m_interacted_primitive_ids[0] = i;
	m_interacted_primitive_ids[1] = j;

	// Left widget draggable was moved, update center and right
	if (j % 3 == 0) {
		set_points(i, j, 1, 2, set_corresponding_points);
	}
	// Center widget draggable was moved, update left and right
	else if (j % 3 == 1) {
		set_points(i, j, -1, 1, set_corresponding_points);
	}
	// Right widget draggable was moved, update left and center
	else if (j % 3 == 2) {
		set_points(i, j, -1, -2, set_corresponding_points);
	}
}

/** END - MFLEURY **/