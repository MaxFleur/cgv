/** BEGIN - MFLEURY **/

#pragma once
#include <cgv_glutil/overlay.h>

#include "shared_editor_data.h"
#include "utils_data_types.h"

namespace tf_editor_shared_functions
{
	void draw_draggables(cgv::render::context& ctx, 
						 shared_data_ptr shared_data) {
		for (int i = 0; i < shared_data->centroids.size(); ++i) {
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
}

/** END - MFLEURY **/