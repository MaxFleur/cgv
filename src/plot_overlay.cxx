#include "plot_overlay.h"

#include <cgv/gui/animate.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>

plot_overlay::plot_overlay() : plot("") {
	
	set_name("Plot Overlay");
	block_events = true;

	check_for_click = -1;

	set_overlay_alignment(AO_START, AO_END);
	set_overlay_stretch(SO_NONE);
	set_overlay_margin(ivec2(0));
	set_overlay_size(ivec2(300));
	
	fbc.add_attachment("color", "flt32[R,G,B]", cgv::render::TF_LINEAR);
	fbc.set_size(2*get_overlay_size());

	blit_canvas.register_shader("rectangle", cgv::g2d::canvas::shaders_2d::rectangle);
}

void plot_overlay::clear(cgv::render::context& ctx) {

	blit_canvas.destruct(ctx);
	fbc.clear(ctx);
	plot.clear(ctx);
}

bool plot_overlay::self_reflect(cgv::reflect::reflection_handler& _rh) {

	return true;
}

bool plot_overlay::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();
	unsigned char modifiers = e.get_modifiers();

	if(et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&) e;
		cgv::gui::MouseAction ma = me.get_action();

		switch(ma) {
		case cgv::gui::MA_PRESS:
			if(me.get_button() == cgv::gui::MB_LEFT_BUTTON) {
				check_for_click = me.get_time();
				return true;
			}
			break;
		case cgv::gui::MA_RELEASE:
			if(check_for_click != -1) {
				double dt = me.get_time() - check_for_click;
				if(dt < 0.2) {
					std::string title = plot.get_domain_config_ptr()->title;
					std::cout << "clicked on plot " << title << std::endl;
				}
			}
			return true;
			break;
		}

		return false;
	} else {
		return false;
	}
}

void plot_overlay::on_set(void* member_ptr) {

	update_member(member_ptr);
	post_redraw();
}

bool plot_overlay::init(cgv::render::context& ctx) {
	
	bool success = true;

	success &= fbc.ensure(ctx);
	success &= blit_canvas.init(ctx);

	if(success) {
		blit_style.fill_color = rgba(1.0f);
		blit_style.use_texture = true;
		blit_style.use_blending = true;
		blit_style.border_color = rgba(rgb(0.5f), 1.0f);
		blit_style.border_width = 3.0f;
		blit_style.feather_width = 0.0f;

		auto& blit_prog = blit_canvas.enable_shader(ctx, "rectangle");
		blit_style.apply(ctx, blit_prog);
		blit_canvas.disable_current_shader(ctx);
	}

	return success;
}

void plot_overlay::init_frame(cgv::render::context& ctx) {

	if(!view_ptr) {
		if(view_ptr = find_view_as_node()) {
			plot.set_view_ptr(view_ptr);
			plot.init(ctx);
		}
	}

	if(ensure_overlay_layout(ctx)) {
		ivec2 container_size = get_overlay_size();
		
		fbc.set_size(2*container_size);
		fbc.ensure(ctx);

		blit_canvas.set_resolution(ctx, get_viewport_size());

		// resize plot to fit overlay container
		auto& axis_cfgs = plot.get_domain_config_ptr()->axis_configs;
		if(axis_cfgs.size() > 0) {
			float w = static_cast<float>(container_size.x());
			float h = static_cast<float>(container_size.y());

			axis_cfgs[0].extent = w / h;
		}
	}

	if(view_ptr) {
		plot.init_frame(ctx);
	}
}

void plot_overlay::draw(cgv::render::context& ctx) {

	if(!show || !view_ptr)
		return;

	glDisable(GL_DEPTH_TEST);

	fbc.enable(ctx);

	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	
	ivec2 size = get_overlay_size();
	float aspect = static_cast<float>(size.x()) / static_cast<float>(size.y());

	const float fov = 20.0f;
	const float eye_distance = 1.0f / tan(cgv::math::deg2rad(fov)) + 0.5f;
	mat4 proj = cgv::math::perspective4(fov, aspect, 0.1f, 10.0f);

	ctx.push_projection_matrix();
	ctx.set_projection_matrix(proj);
	
	ctx.push_modelview_matrix();
	ctx.set_modelview_matrix(cgv::math::look_at4(vec3(0.0f, 0.0f, eye_distance), vec3(0.0f), vec3(0.0f, 1.0f, 0.0f)));

	plot.draw(ctx);

	ctx.pop_modelview_matrix();
	ctx.pop_projection_matrix();
	
	fbc.disable(ctx);

	// draw frame buffer texture to screen
	auto& blit_prog = blit_canvas.enable_shader(ctx, "rectangle");

	fbc.enable_attachment(ctx, "color", 0);
	blit_canvas.draw_shape(ctx, get_overlay_position(), get_overlay_size());
	fbc.disable_attachment(ctx, "color");

	blit_canvas.disable_current_shader(ctx);

	glEnable(GL_DEPTH_TEST);
}

void plot_overlay::create_gui() {

	create_overlay_gui();

	plot.create_gui(this, *this);
}

void plot_overlay::create_gui(cgv::gui::provider& p) {

	p.add_member_control(this, "Show", show, "check");
}
