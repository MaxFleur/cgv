#include "application_plugin.h"

#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>

namespace cgv {
namespace glutil {

application_plugin::application_plugin(const std::string& name) : group(name) {
	last_blocking_overlay_ptr = nullptr;
	blocking_overlay_ptr = nullptr;
}

bool application_plugin::handle(cgv::gui::event& e) {
	if(e.get_kind() == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&) e;
		cgv::gui::MouseAction ma = me.get_action();

		if(ma == cgv::gui::MA_PRESS || ma == cgv::gui::MA_MOVE || ma == cgv::gui::MA_WHEEL) {
			ivec2 mpos(me.get_x(), me.get_y());

			blocking_overlay_ptr = nullptr;
			for(auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
				auto overlay_ptr = (*it);
				if(overlay_ptr->is_visible() && overlay_ptr->is_hit(mpos)) {
					blocking_overlay_ptr = overlay_ptr;
					break;
				}
			}

			if(ma == cgv::gui::MA_MOVE) {
				if(!last_blocking_overlay_ptr && blocking_overlay_ptr) {
					me.set_action(cgv::gui::MA_ENTER);
				}
				if(last_blocking_overlay_ptr && !blocking_overlay_ptr) {
					me.set_action(cgv::gui::MA_LEAVE);
					last_blocking_overlay_ptr->handle_event(e);
				}
			}
		}

		bool was_handled = false;
		bool was_blocked = false;
		bool result = false;

		if(blocking_overlay_ptr) {
			result = blocking_overlay_ptr->handle_event(e);
			was_handled = true;
			was_blocked = blocking_overlay_ptr->blocks_events();
		}

		if(ma == cgv::gui::MA_RELEASE) {
			blocking_overlay_ptr = nullptr;
		}

		last_blocking_overlay_ptr = blocking_overlay_ptr;

		if(was_handled)
			if(was_blocked)
				return true;
			else
				return result;
		else
			return handle_event(e);
	} else {
		// TODO: make the overlay have a handles keys flag?
		// TODO: have a flag that enables blocking the event from further processing when returning true or false?
		for(auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
			auto overlay_ptr = (*it);
			if(overlay_ptr->is_visible())
				overlay_ptr->handle_event(e);
		}
		return handle_event(e);
	}
}

}
}
