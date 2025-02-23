#pragma once

#include "focusable.h"
#include <cgv/render/context.h>

#include "lib_begin.h"

namespace cgv {
	namespace nui {
		/// interface for objects that provides a modelview_projection_window_matrix
		class CGV_API transformed : public cgv::render::render_types
		{
		protected:
			dmat4 MVPW;
		public:
			/// init MVPW member to identity matrix
			transformed();
			/// call this inside of cgv::render::drawable::draw(cgv::render::context&)
			void set_modelview_projection_window_matrix(const cgv::render::context& ctx);
			/// return modelview_projection_window_matrix
			const dmat4& get_modelview_projection_window_matrix() const;
		};

	}
}
#include <cgv/config/lib_end.h>