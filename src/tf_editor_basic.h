/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/msdf_gl_canvas_font_renderer.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

#include "sliced_volume_data_set.h"
#include "shared_editor_data.h"
#include "tf_editor_shared_data_types.h"

/* This class provides the basic class used for designing the transfer functions. It contains all
data types which are used by both editors as well as some functions. */
class tf_editor_basic : public cgv::glutil::overlay {

public:
	tf_editor_basic();

	bool self_reflect(cgv::reflect::reflection_handler& _rh) { return true; }

	void set_data_set(sliced_volume_data_set* data_set_ptr) {
		m_data_set_ptr = data_set_ptr;
		create_labels();
	}

	void set_shared_data(shared_data_ptr data_ptr) {
		m_shared_data_ptr = data_ptr;
	}

protected:

	// Main drawing function
	void draw(cgv::render::context& ctx);

	void clear(cgv::render::context& ctx);

	bool handle_key_input(const char& key, const int& index);

	// Basic GUI for relation visualization
	void create_gui_basic();
	// GUI for tone mapping parameters
	void create_gui_tm();
	// GUI for coloring mode
	void create_gui_coloring();

	// Draw all contents of the editor, exlcuding the plot. This is customized in the
	// deriving editors.
	virtual void draw_content(cgv::render::context& ctx) = 0;

	// Label creation needed if the sliced data is set, customized in the deriving editors.
	virtual void create_labels() = 0;

	virtual void update_content() = 0;

	void handle_mouse_click_end(bool found, bool double_clicked, int i, int j, int& interacted_point_id, bool is_lbe);

	// Called when a mouse drag is ended
	void end_drag() {
		m_currently_dragging = false;

		if (vis_mode == VM_GTF) {
			update_content();
			return;
		}
	}

	// redraw the plot contents
	void redraw() {
		has_damage = true;
		post_redraw();
	}

	template<typename T>
	void set_point_handles(std::vector<std::vector<T>>& points, cgv::glutil::draggables_collection<T*>& point_handles) {
		point_handles.clear();
		for (unsigned i = 0; i < points.size(); ++i) {
			for (int j = 0; j < points[i].size(); j++) {
				point_handles.add(&points[i][j]);
			}
		}
	}

	template <typename T>
	void draw_draggables(cgv::render::context& ctx, const T& points, int interacted_id) {
		for (int i = 0; i < m_shared_data_ptr->primitives.size(); ++i) {
			// Clear for each draggable because colors etc might change
			m_geometry_draggables.clear();
			m_geometry_draggables_interacted.clear();

			const auto color = m_shared_data_ptr->primitives.at(i).color;

			for (int j = 0; j < points[i].size(); j++) {
				float transparency = 1.0f;
				// 12 points means line based editor
				if (points[i].size() == 12) {
					const auto protein_index = j / 3;
					// More transparent points for maximum widths
					if (m_shared_data_ptr->primitives.at(i).centr_widths[protein_index] == 10.0f) {
						transparency = 0.3f;
					}
					// SPLOM editor
				}
				else if (points[i].size() == 6) {
					// Check for maximum widths
					const auto is_width_max = [&](const int& index_1, const int& index_2) {
						if (m_shared_data_ptr->primitives.at(i).centr_widths[index_1] == 10.0f &&
							m_shared_data_ptr->primitives.at(i).centr_widths[index_2] == 10.0f) {
							return 0.3f;
						}
						return 1.0f;
					};
					// Each SPLOM editor draggable encodes two protein indices, based on the current point 
					// in the vector we adress the right indices and check for maximum widths
					switch (j) {
					case 0:
						transparency = is_width_max(0, 1);
						break;
					case 1:
						transparency = is_width_max(0, 2);
						break;
					case 2:
						transparency = is_width_max(0, 3);
						break;
					case 3:
						transparency = is_width_max(3, 1);
						break;
					case 4:
						transparency = is_width_max(3, 2);
						break;
					case 5:
						transparency = is_width_max(2, 1);
						break;
					}
				}

				const auto render_pos = points[i][j].get_render_position();
				rgba col(color.R(), color.G(), color.B(), transparency);
				// Only draw for interacted if a point has been dragged
				i == interacted_id ? m_geometry_draggables_interacted.add(render_pos, col) : m_geometry_draggables.add(render_pos, col);
			}

			// Draw 
			auto& point_prog = m_shared_data_ptr->primitives.at(i).type == shared_data::TYPE_BOX ?
				m_renderer_draggables_rectangle.ref_prog() : m_renderer_draggables_circle.ref_prog();
			auto& renderer = m_shared_data_ptr->primitives.at(i).type == shared_data::TYPE_BOX ? m_renderer_draggables_rectangle : m_renderer_draggables_circle;
			if (i == interacted_id) {
				point_prog.enable(ctx);
				content_canvas.set_view(ctx, point_prog);
				m_style_draggables_interacted.apply(ctx, point_prog);
				point_prog.set_attribute(ctx, "size", vec2(16.0f));
				point_prog.disable(ctx);
				renderer.render(ctx, PT_POINTS, m_geometry_draggables_interacted);
				continue;
			}
			point_prog.enable(ctx);
			content_canvas.set_view(ctx, point_prog);
			m_style_draggables.apply(ctx, point_prog);
			point_prog.set_attribute(ctx, "size", vec2(12.0f));
			point_prog.disable(ctx);
			renderer.render(ctx, PT_POINTS, m_geometry_draggables);
		}
	}

protected:

	// different visualization modes used in both editors
	enum VisualizationMode {
		VM_SHAPES= 0,
		VM_GTF = 1
	} vis_mode;

	// whether we need to redraw the contents of this overlay
	bool has_damage = true;

	// a frame buffer container, storing the offline frame buffer of this overlay content
	cgv::glutil::frame_buffer_container fbc;
	// a frame buffer container, storing the offline frame buffer of the plot data
	cgv::glutil::frame_buffer_container fbc_plot;

	// canvas to draw content into (same size as overlay)
	cgv::glutil::canvas content_canvas;
	// canvas to draw overlay into (same size as viewport/main framebuffer)
	cgv::glutil::canvas viewport_canvas;
	// final style for the overlay when rendering into main framebuffer
	cgv::glutil::shape2d_style overlay_style;

	// rectangle defining the draw area of the actual plot
	cgv::glutil::rect domain;

	// These geometries are used in both editors to drag points representing centroid positions
	// If a centroid position is dragged, its size will increase, so we need two different geometries and styles
	tf_editor_shared_data_types::point_geometry m_geometry_draggables;
	tf_editor_shared_data_types::point_geometry m_geometry_draggables_interacted;

	// Renderer for draggables
	cgv::glutil::generic_renderer m_renderer_draggables_circle;
	cgv::glutil::generic_renderer m_renderer_draggables_rectangle;
	// Style of the draggables, interacted are drawn differently
	cgv::glutil::shape2d_style m_style_draggables;
	cgv::glutil::shape2d_style m_style_draggables_interacted;

	// Text geometry, storing individual labels
	cgv::glutil::msdf_text_geometry m_labels;
	const float m_font_size = 18.0f;
	cgv::glutil::shape2d_style m_style_text;

	// whether the plot shall be reset and its framebuffer cleared
	bool m_reset_plot = true;

	// keeps track of the amount of data objects (lines/points) that have been rendered so-far
	int m_total_count = 0;
	// threshold that is applied to protein density samples before plotting
	float m_threshold = 0.2f;
	// alpha value of individual data values in the plot
	float m_alpha = 0.0001f;

	// Time used to estimate a double click
	double m_click_time;

	// tone mapping parameters
	unsigned tm_normalization_count = 1000;
	float tm_alpha = 1.0f;

	// point parameters
	// Is an interaction going on?
	bool is_interacting = false;
	// Has a point been dragged?
	bool m_is_point_dragged = false;
	// Are we currently dragging something?
	bool m_currently_dragging = false;

	// Whether shapes etc shall be drawn or not
	bool is_peak_mode = false;

	// store a pointer to the data set
	sliced_volume_data_set* m_data_set_ptr = nullptr;
	// a pointer to the shared primitive data
	shared_data_ptr m_shared_data_ptr;
};

/** END - MFLEURY **/