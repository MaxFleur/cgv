#pragma once

#include <cgv_glutil/frame_buffer_container.h>
#include <cgv_glutil/overlay.h>
#include <cgv_glutil/2d/canvas.h>
#include <cgv_glutil/2d/shape2d_styles.h>
#include <plot/plot2d.h>

class plot_overlay : public cgv::glutil::overlay {
protected:
	view* view_ptr = nullptr;

	double check_for_click;

	cgv::glutil::frame_buffer_container fbc;

	cgv::glutil::canvas blit_canvas;
	cgv::glutil::shape2d_style blit_style;

	cgv::plot::plot2d plot;
	std::vector<vec2> plot_data;

public:
	plot_overlay();
	std::string get_type_name() const { return "plot_overlay"; }

	void clear(cgv::render::context& ctx);

	bool self_reflect(cgv::reflect::reflection_handler& _rh);
	void stream_help(std::ostream& os) {}

	bool handle_event(cgv::gui::event& e);
	void on_set(void* member_ptr);

	bool init(cgv::render::context& ctx);
	void init_frame(cgv::render::context& ctx);
	void draw(cgv::render::context& ctx);
	
	void create_gui();
	void create_gui(cgv::gui::provider& p);

	cgv::plot::plot2d& ref_plot() { return plot; }
};