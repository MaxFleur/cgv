#pragma once

#include <cgv/render/managed_frame_buffer.h>
#include <cgv_app/overlay.h>
#include <cgv_g2d/canvas.h>
#include <cgv_g2d/shape2d_styles.h>
#include <plot/plot2d.h>

class plot_overlay : public cgv::app::overlay {
protected:
	cgv::render::view* view_ptr = nullptr;

	double check_for_click;

	cgv::render::managed_frame_buffer fbc;

	cgv::g2d::canvas blit_canvas;
	cgv::g2d::shape2d_style blit_style;

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