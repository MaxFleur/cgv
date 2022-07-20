/** BEGIN - MFLEURY **/

#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

#include "sliced_volume_data_set.h"
#include "shared_editor_data.h"
#include "tf_editor_shared_data_types.h"

/* This class provides the basic class used for designing the transfer functions. The lines and 
   scatterplot editor derive from this basic editor. */
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

	// Draw all contents of the editor, exlcuding the plot. This is customized in the
	// deriving editors.
	virtual void draw_content(cgv::render::context& ctx) = 0;

	// Label creation needed if the sliced data is set, customized in the deriving editors.
	virtual void create_labels() = 0;

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

	// whether the plot shall be reset and its framebuffer cleared
	bool m_reset_plot = true;

	// keeps track of the amount of data objects (lines/points) that have been rendered so-far
	int m_total_count = 0;
	// threshold that is applied to protein density samples before plotting
	float m_threshold = 0.3f;
	// alpha value of individual data values in the plot
	float m_alpha = 0.0001f;

	// Tone Mapping parameters
	bool use_tone_mapping = false;
	unsigned tm_normalization_count = 1000;
	float tm_alpha = 1.0f;
	float tm_gamma = 1.0f;

	// Point parameters
	// Has a point been clicked?
	bool m_is_point_clicked = false;
	// Has a point been dragged?
	bool m_is_point_dragged = false;
	// id of the centroid layer whose point was clicked
	int m_clicked_centroid_id;

	// store a pointer to the data set
	sliced_volume_data_set* m_data_set_ptr = nullptr;
	// a pointer to the shared primitive data
	shared_data_ptr m_shared_data_ptr;
};

/** END - MFLEURY **/