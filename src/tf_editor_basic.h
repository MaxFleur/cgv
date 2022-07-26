/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/msdf_gl_font_renderer.h>
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

	// Basic GUI for relation visualization
	void create_basic_gui();
	// GUI for tone mapping parameters
	void create_tm_gui();

	// Draw all contents of the editor, exlcuding the plot. This is customized in the
	// deriving editors.
	virtual void draw_content(cgv::render::context& ctx) = 0;

	// Label creation needed if the sliced data is set, customized in the deriving editors.
	virtual void create_labels() = 0;

	virtual void update_content() = 0;

	template <typename T>
	void find_clicked_draggable(std::vector<std::vector<T>> const& points,
								// std::vector<std::vector<T>> const& interacted_points,
								int x, int y, int& clicked_draggable_id, bool& is_point_clicked) {
		const auto input_vec = vec2{ static_cast<float>(x), static_cast<float>(y) };
		auto found = false;
		int found_index;
		// Search all points
		for (int i = 0; i < points.size(); i++) {
			for (int j = 0; j < points.at(i).size(); j++) {
				// If the mouse was clicked inside a point, store all point addresses belonging to the 
				// corresponding layer
				if (points.at(i).at(j).is_inside(input_vec)) {
					/*interacted_points.clear();
					for (int k = 0; k < m_points.at(i).size(); k++) {
						interacted_points.push_back(&m_points.at(i).at(k));
					}*/
					found = true;
					found_index = i;
					break;
				}
			}
		}

		is_point_clicked = found;
		// If we found something, redraw 
		if (found) {
			clicked_draggable_id = found_index;
			redraw();
		}
	}

	// redraw the plot contents
	void redraw() {
		has_damage = true;
		post_redraw();
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
	tf_editor_shared_data_types::point_geometry_draggable m_geometry_draggables;
	tf_editor_shared_data_types::point_geometry_draggable m_geometry_draggables_interacted;
	// Renderer for draggables
	cgv::glutil::generic_renderer m_renderer_draggables;
	// Style of the draggables, interacted are drawn differently
	cgv::glutil::shape2d_style m_style_draggables;
	cgv::glutil::shape2d_style m_style_draggables_interacted;

	// Font storage and renderer
	cgv::glutil::msdf_font m_font;
	cgv::glutil::msdf_gl_font_renderer m_renderer_fonts;
	// Text geometry, storing individual labels
	cgv::glutil::msdf_text_geometry m_labels;
	const float m_font_size = 18.0f;

	// whether the plot shall be reset and its framebuffer cleared
	bool m_reset_plot = true;

	// keeps track of the amount of data objects (lines/points) that have been rendered so-far
	int m_total_count = 0;
	// threshold that is applied to protein density samples before plotting
	float m_threshold = 0.3f;
	// alpha value of individual data values in the plot
	float m_alpha = 0.0001f;

	// tone mapping parameters
	bool use_tone_mapping = false;
	unsigned tm_normalization_count = 1000;
	float tm_alpha = 1.0f;
	float tm_gamma = 1.0f;

	// point parameters
	// Has a point been clicked?
	bool m_is_point_clicked = false;
	// Has a point been dragged?
	bool m_is_point_dragged = false;
	// id of the centroid layer whose point was clicked
	int m_clicked_draggable_id;

	// store a pointer to the data set
	sliced_volume_data_set* m_data_set_ptr = nullptr;
	// a pointer to the shared primitive data
	shared_data_ptr m_shared_data_ptr;
};

/** END - MFLEURY **/