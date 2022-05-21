#include "special_slab_renderer.h"
#include <cgv_gl/gl/gl.h>
#include <cgv_gl/gl/gl_tools.h>

namespace cgv {
	namespace render {
		special_slab_renderer& ref_special_slab_renderer(context& ctx, int ref_count_change)
		{
			static int ref_count = 0;
			static special_slab_renderer r;
			r.manage_singleton(ctx, "special_slab_renderer", ref_count, ref_count_change);
			return r;
		}

		render_style* special_slab_renderer::create_render_style() const
		{
			return new special_slab_render_style();
		}

		special_slab_render_style::special_slab_render_style()
		{
			thickness_scale = 1.0f;
			tex_unit = 0;
			tf_tex_unit = 1;
			tf_source_channel = 0;
			tex_idx_offset = 0;
			tex_idx_stride = 1;
			min_step_count = 0;
			step_size = 0.02f;
			opacity = 1.0f;
			falloff_mix = 1.0f;
			falloff_strength = 1.0f;
			scale = 20.0f;
			mode = COLOR;
			col0 = rgba(0.0f);
			col1 = rgba(0.0f);
			col2 = rgba(0.0f);
			col3 = rgba(0.0f);
			tf_bounds0 = vec2(0.0f, 1.0f);
			tf_bounds1 = vec2(0.0f, 1.0f);
			tf_bounds2 = vec2(0.0f, 1.0f);
			tf_bounds3 = vec2(0.0f, 1.0f);
			enable_clipping = false;
			clip_plane_offset = 0.0f;
			clip_plane_normal = vec3(1.0f, 0.0f, 0.0f);
		}

		special_slab_renderer::special_slab_renderer()
		{
			has_extents = false;
			position_is_center = true;
			has_translations = false;
			has_rotations = false;
			has_texture_indices = false;
		}
		/*void special_slab_renderer::set_attribute_array_manager(const context& ctx, attribute_array_manager* _aam_ptr)
		{
			group_renderer::set_attribute_array_manager(ctx, _aam_ptr);
			if (aam_ptr) {
				if (aam_ptr->has_attribute(ctx, ref_prog().get_attribute_location(ctx, "extent")))
					has_extents = true;
				if (aam_ptr->has_attribute(ctx, ref_prog().get_attribute_location(ctx, "translation")))
					has_translations= true;
				if (aam_ptr->has_attribute(ctx, ref_prog().get_attribute_location(ctx, "rotation")))
					has_rotations= true;
				if (aam_ptr->has_attribute(ctx, ref_prog().get_attribute_location(ctx, "texture_index")))
					has_texture_indices = true;
			}
			else {
				has_extents = false;
				has_translations = false;
				has_rotations = false;
				has_texture_indices = false;
			}
		}*/

		/// call this before setting attribute arrays to manage attribute array in given manager
		void special_slab_renderer::enable_attribute_array_manager(const context& ctx, attribute_array_manager& aam) {

			group_renderer::enable_attribute_array_manager(ctx, aam);
			if(has_attribute(ctx, "extent"))
				has_extents = true;
			if(has_attribute(ctx, "translation"))
				has_translations = true;
			if(has_attribute(ctx, "rotation"))
				has_rotations = true;
			if(has_attribute(ctx, "texture_index"))
				has_texture_indices = true;
		}
		/// call this after last render/draw call to ensure that no other users of renderer change attribute arrays of given manager
		void special_slab_renderer::disable_attribute_array_manager(const context& ctx, attribute_array_manager& aam) {

			group_renderer::disable_attribute_array_manager(ctx, aam);
			has_extents = false;
			has_translations = false;
			has_rotations = false;
			has_texture_indices = false;
		}

		/// set the flag, whether the position is interpreted as the slab center
		void special_slab_renderer::set_position_is_center(bool _position_is_center)
		{
			position_is_center = _position_is_center;
		}

		bool special_slab_renderer::validate_attributes(const context& ctx) const
		{
			// validate set attributes
			const special_slab_render_style& srs = get_style<special_slab_render_style>();
			bool res = group_renderer::validate_attributes(ctx);
			if (!has_extents) {
				ctx.error("slab_renderer::enable() extent attribute not set");
				res = false;
			}
			return res;
		}
		bool special_slab_renderer::init(cgv::render::context& ctx)
		{
			bool res = renderer::init(ctx);
			if (!ref_prog().is_created()) {
				if (!ref_prog().build_program(ctx, "special_slab.glpr", true)) {
					std::cerr << "ERROR in slab_renderer::init() ... could not build program slab.glpr" << std::endl;
					return false;
				}
			}
			return res;
		}
		/// 
		bool special_slab_renderer::enable(context& ctx)
		{
			const special_slab_render_style& srs = get_style<special_slab_render_style>();

			if (!group_renderer::enable(ctx))
				return false;
			ref_prog().set_uniform(ctx, "position_is_center", position_is_center);
			ref_prog().set_uniform(ctx, "has_rotations", has_rotations);
			ref_prog().set_uniform(ctx, "has_translations", has_translations);
			ref_prog().set_uniform(ctx, "has_texture_indices", has_texture_indices);
			ref_prog().set_uniform(ctx, "thickness_scale", srs.thickness_scale);
			ref_prog().set_uniform(ctx, "tex", srs.tex_unit);
			ref_prog().set_uniform(ctx, "tf_tex", srs.tf_tex_unit);
			ref_prog().set_uniform(ctx, "tf_source_channel", srs.tf_source_channel);
			ref_prog().set_uniform(ctx, "tex_idx_offset", srs.tex_idx_offset);
			ref_prog().set_uniform(ctx, "tex_idx_stride", srs.tex_idx_stride);
			ref_prog().set_uniform(ctx, "min_step_count", srs.min_step_count);
			ref_prog().set_uniform(ctx, "step_size", srs.step_size);
			ref_prog().set_uniform(ctx, "opacity_scale", srs.opacity);
			ref_prog().set_uniform(ctx, "falloff_mix", srs.falloff_mix);
			ref_prog().set_uniform(ctx, "falloff_strength", srs.falloff_strength);
			ref_prog().set_uniform(ctx, "scale", srs.scale);
			ref_prog().set_uniform(ctx, "mode", (int)srs.mode);
			ref_prog().set_uniform(ctx, "col0", srs.col0);
			ref_prog().set_uniform(ctx, "col1", srs.col1);
			ref_prog().set_uniform(ctx, "col2", srs.col2);
			ref_prog().set_uniform(ctx, "col3", srs.col3);
			ref_prog().set_uniform(ctx, "tf_bounds0", srs.tf_bounds0);
			ref_prog().set_uniform(ctx, "tf_bounds1", srs.tf_bounds1);
			ref_prog().set_uniform(ctx, "tf_bounds2", srs.tf_bounds2);
			ref_prog().set_uniform(ctx, "tf_bounds3", srs.tf_bounds3);
			ref_prog().set_uniform(ctx, "clip_plane_origin", srs.clip_plane_offset * normalize(srs.clip_plane_normal));
			ref_prog().set_uniform(ctx, "clip_plane_normal", srs.enable_clipping ? normalize(srs.clip_plane_normal) : vec3(0.0f));
			ref_prog().set_uniform(ctx, "viewport_dims", vec2(ctx.get_width(), ctx.get_height()));

			glCullFace(GL_FRONT);
			glEnable(GL_CULL_FACE);
			return true;
		}
		///
		bool special_slab_renderer::disable(context& ctx)
		{
			if (!attributes_persist()) {
				has_extents = false;
				position_is_center = true;
				has_rotations = false;
				has_translations = false;
			}

			glDisable(GL_BLEND);
			glDisable(GL_CULL_FACE);

			return group_renderer::disable(ctx);
		}

		void special_slab_renderer::draw(context& ctx, size_t start, size_t count, bool use_strips, bool use_adjacency, uint32_t strip_restart_index)
		{
			draw_impl(ctx, PT_POINTS, start, count, false, false, -1);
		}

		bool special_slab_render_style_reflect::self_reflect(cgv::reflect::reflection_handler& rh)
		{
			return
				rh.reflect_base(*static_cast<group_render_style*>(this)) &&
				rh.reflect_member("tex_unit", tex_unit) &&
				rh.reflect_member("tf_tex_unit", tf_tex_unit) &&
				rh.reflect_member("tex_idx_offset", tex_idx_offset) &&
				rh.reflect_member("tex_idx_stride", tex_idx_stride) &&
				rh.reflect_member("mode", (int&)mode) &&
				rh.reflect_member("tf_source_channel", tf_source_channel) &&
				rh.reflect_member("thickness_scale", thickness_scale) &&
				rh.reflect_member("step_size", step_size) &&
				rh.reflect_member("min_step_count", min_step_count) &&
				rh.reflect_member("scale", scale) &&
				rh.reflect_member("opacity", opacity) &&
				rh.reflect_member("falloff_mix", falloff_mix) &&
				rh.reflect_member("falloff_strength", falloff_strength) &&
				rh.reflect_member("col0", col0) &&
				rh.reflect_member("col1", col1) &&
				rh.reflect_member("col2", col2) &&
				rh.reflect_member("col3", col3) &&
				rh.reflect_member("tf_bounds0", tf_bounds0);
				rh.reflect_member("tf_bounds1", tf_bounds1) &&
				rh.reflect_member("tf_bounds2", tf_bounds2) &&
				rh.reflect_member("tf_bounds3", tf_bounds3) &&
				rh.reflect_member("enable_clipping", enable_clipping) &&
				rh.reflect_member("clip_plane_offset", clip_plane_offset) &&
				rh.reflect_member("clip_plane_normal", clip_plane_normal);
		}

		cgv::reflect::extern_reflection_traits<special_slab_render_style, special_slab_render_style_reflect> get_reflection_traits(const special_slab_render_style&)
		{
			return cgv::reflect::extern_reflection_traits<special_slab_render_style, special_slab_render_style_reflect>();
		}
	}
}

#include <cgv/gui/provider.h>

namespace cgv {
	namespace gui {

		struct special_slab_render_style_gui_creator : public gui_creator {
			/// attempt to create a gui and return whether this was successful
			bool create(provider* p, const std::string& label,
				void* value_ptr, const std::string& value_type,
				const std::string& gui_type, const std::string& options, bool*) {
				if(value_type != cgv::type::info::type_name<cgv::render::special_slab_render_style>::get_name())
					return false;
				cgv::render::special_slab_render_style* srs_ptr = reinterpret_cast<cgv::render::special_slab_render_style*>(value_ptr);
				cgv::base::base* b = dynamic_cast<cgv::base::base*>(p);

				p->add_member_control(b, "thickness scale", srs_ptr->thickness_scale, "value_slider", "min=0.001;step=0.0001;max=10.0;log=true;ticks=true");
				//p->add_member_control(b, "texture unit", srs_ptr->tex_unit, "value_slider", "min=0;step=1;max=9;log=false;ticks=true");
				//p->add_member_control(b, "transfer function unit", srs_ptr->tf_tex_unit, "value_slider", "min=0;step=1;max=9;log=false;ticks=true");
				p->add_member_control(b, "mode", srs_ptr->mode, "dropdown", "enums='color,transfer function,color mix'");
				p->add_member_control(b, "tf source channel", srs_ptr->tf_source_channel, "value_slider", "min=0;step=1;max=3;log=false;ticks=true");

				p->add_member_control(b, "min step count", srs_ptr->min_step_count, "value_slider", "min=0;step=1;max=64;log=false;ticks=true");
				p->add_member_control(b, "step size", srs_ptr->step_size, "value_slider", "min=0.001;step=0.001;max=0.5;log=true;ticks=true");
				p->add_member_control(b, "opacity scale", srs_ptr->opacity, "value_slider", "min=0.0;step=0.0001;max=1.0;log=true;ticks=true");
				p->add_member_control(b, "falloff mix", srs_ptr->falloff_mix, "value_slider", "min=0.0;step=0.0001;max=1.0;ticks=true");
				p->add_member_control(b, "falloff strength", srs_ptr->falloff_strength, "value_slider", "min=0.0;step=0.0001;max=10.0;log=true;ticks=true");
				p->add_member_control(b, "scale", srs_ptr->scale, "value_slider", "min=0.0;step=0.0001;max=10000.0;log=true;ticks=true");
				
				p->add_member_control(b, "Projectin", srs_ptr->col0);
				p->add_member_control(b, "min", srs_ptr->tf_bounds0[0], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");
				p->add_member_control(b, "max", srs_ptr->tf_bounds0[1], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");

				p->add_member_control(b, "Actin", srs_ptr->col1);
				p->add_member_control(b, "min", srs_ptr->tf_bounds1[0], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");
				p->add_member_control(b, "max", srs_ptr->tf_bounds1[1], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");

				p->add_member_control(b, "Obscurin", srs_ptr->col2);
				p->add_member_control(b, "min", srs_ptr->tf_bounds2[0], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");
				p->add_member_control(b, "max", srs_ptr->tf_bounds2[1], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");

				p->add_member_control(b, "Sallimus", srs_ptr->col3);
				p->add_member_control(b, "min", srs_ptr->tf_bounds3[0], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");
				p->add_member_control(b, "max", srs_ptr->tf_bounds3[1], "value_slider", "min=0.0;step=0.0001;max=1.0;log=false;ticks=false");

				if(p->begin_tree_node("clipping plane", srs_ptr->enable_clipping, false)) {
					p->align("\a");
					p->add_member_control(b, "enable", srs_ptr->enable_clipping, "check");

					//p->add_member_control(b, "offset", srs_ptr->clip_plane_offset, "value_slider", "min=-1;step=0.0001;max=1;ticks=true");
					p->add_member_control(b, "offset", srs_ptr->clip_plane_offset, "value", "w=135;min=-0.5;step=0.0001;max=0.5", " ");
					p->add_member_control(b, "", srs_ptr->clip_plane_offset, "wheel", "w=40;h=20;min=-0.5;step=0.001;max=0.5");

					p->add_member_control(b, "direction", srs_ptr->clip_plane_normal[0], "value", "w=55;min=-1;max=1", " ");
					p->add_member_control(b, "", srs_ptr->clip_plane_normal[1], "value", "w=55;min=-1;max=1", " ");
					p->add_member_control(b, "", srs_ptr->clip_plane_normal[2], "value", "w=55;min=-1;max=1");
					p->add_member_control(b, "", srs_ptr->clip_plane_normal[0], "slider", "w=55;min=-1;step=0.0001;max=1;ticks=true", " ");
					p->add_member_control(b, "", srs_ptr->clip_plane_normal[1], "slider", "w=55;min=-1;step=0.0001;max=1;ticks=true", " ");
					p->add_member_control(b, "", srs_ptr->clip_plane_normal[2], "slider", "w=55;min=-1;step=0.0001;max=1;ticks=true");

					connect_copy(p->add_button("x", "w=35", " ")->click, cgv::signal::rebind(srs_ptr, &cgv::render::special_slab_render_style::clip_plane_normal_x, b));
					connect_copy(p->add_button("y", "w=35", " ")->click, cgv::signal::rebind(srs_ptr, &cgv::render::special_slab_render_style::clip_plane_normal_y, b));
					connect_copy(p->add_button("z", "w=35", " ")->click, cgv::signal::rebind(srs_ptr, &cgv::render::special_slab_render_style::clip_plane_normal_z, b));
					connect_copy(p->add_button("flip", "w=48")->click, cgv::signal::rebind(srs_ptr, &cgv::render::special_slab_render_style::flip_clip_plane, b));

					p->align("\b");
					p->end_tree_node(srs_ptr->enable_clipping);
				}
				
				//p->add_member_control(b, "", srs_ptr->clip_plane_offset, "dial", "w=40;h=40;min=-0.5;max=0.5");
				// TODO: use wheel and add to ThumbWheel::handle
				/*
					bool shift = get_key_state(LeftShiftKey);

					double step_reduce = 1.0;
					if(shift)
						step_reduce = 0.1;
				*/

				//p->add_gui("group_render_style", *static_cast<cgv::render::group_render_style*>(srs_ptr));
				return true;
			}
		};

#include <cgv_gl/gl/lib_begin.h>

		cgv::gui::gui_creator_registration<special_slab_render_style_gui_creator> slab_rs_gc_reg("special_slab_render_style_gui_creator");
	}
}
