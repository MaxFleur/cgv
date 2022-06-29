#include "mdtf_volume_renderer.h"

#include <random>

#include <cgv/math/ftransform.h>
#include <cgv_gl/gl/gl.h>
#include <cgv_gl/gl/gl_tools.h>

#define P_000 vec3(-0.5f,-0.5f,-0.5f)
#define P_001 vec3(-0.5f,-0.5f,+0.5f)
#define P_010 vec3(-0.5f,+0.5f,-0.5f)
#define P_011 vec3(-0.5f,+0.5f,+0.5f)
#define P_100 vec3(+0.5f,-0.5f,-0.5f)
#define P_101 vec3(+0.5f,-0.5f,+0.5f)
#define P_110 vec3(+0.5f,+0.5f,-0.5f)
#define P_111 vec3(+0.5f,+0.5f,+0.5f)

namespace cgv {
	namespace render {
		mdtf_volume_renderer& ref_mdtf_volume_renderer(context& ctx, int ref_count_change)
		{
			static int ref_count = 0;
			static mdtf_volume_renderer r;
			r.manage_singleton(ctx, "mdtf_volume_renderer", ref_count, ref_count_change);
			return r;
		}

		render_style* mdtf_volume_renderer::create_render_style() const
		{
			return new mdtf_volume_render_style();
		}

		void mdtf_volume_renderer::init_noise_texture(context& ctx)
		{
			if(noise_texture.is_created())
				noise_texture.destruct(ctx);

			unsigned size = 64;
			std::vector<float> noise_data(size*size);

			std::mt19937 rng(42);
			std::uniform_real_distribution<float> dist(0.0f, 1.0f);

			for(size_t i = 0; i < noise_data.size(); ++i)
				noise_data[i] = dist(rng);

			cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(size, size, TI_FLT32, cgv::data::CF_R), noise_data.data());
			noise_texture.create(ctx, dv, 0);
		}

		mdtf_volume_render_style::mdtf_volume_render_style()
		{
			integration_quality = IQ_128;
			interpolation_mode = IP_LINEAR;
			enable_noise_offset = true;
			opacity_scale = 1.0f;
			enable_scale_adjustment = true;
			size_scale = 50.0f;
			clip_box = box3(vec3(0.0f), vec3(1.0f));
			enable_lighting = false;
			enable_depth_test = false;
		}

		mdtf_volume_renderer::mdtf_volume_renderer() : noise_texture("flt32[R]")
		{
			//shader_defines = shader_define_map();
			volume_texture = nullptr;
			gradient_texture = nullptr;
			depth_texture = nullptr;

			noise_texture.set_min_filter(TF_LINEAR);
			noise_texture.set_mag_filter(TF_LINEAR);
			noise_texture.set_wrap_s(TW_REPEAT);
			noise_texture.set_wrap_t(TW_REPEAT);
		}

		bool mdtf_volume_renderer::validate_attributes(const context& ctx) const
		{
			// validate set attributes
			const mdtf_volume_render_style& vrs = get_style<mdtf_volume_render_style>();
			bool res = renderer::validate_attributes(ctx);
			res = res && (volume_texture != nullptr);
			return res;
		}

		bool mdtf_volume_renderer::init(cgv::render::context& ctx)
		{
			bool res = renderer::init(ctx);

			res = res && aa_manager.init(ctx);
			enable_attribute_array_manager(ctx, aa_manager);

			std::vector<vec3> positions = {
				// front
				P_001, P_101, P_111,
				P_001, P_111, P_011,
				// back
				P_000, P_110, P_100,
				P_000, P_010, P_110,
				// left
				P_000, P_011, P_010,
				P_000, P_001, P_011,
				// right
				P_100, P_110, P_111,
				P_100, P_111, P_101,
				// top
				P_010, P_111, P_110,
				P_010, P_011, P_111,
				// bottom
				P_000, P_100, P_101,
				P_000, P_101, P_001,
			};

			set_position_array(ctx, positions);
			
			if(!noise_texture.is_created())
				init_noise_texture(ctx);

			return res;
		}

		bool mdtf_volume_renderer::set_volume_texture(texture* tex)
		{
			if(!tex || tex->get_nr_dimensions() != 3)
				return false;
			volume_texture = tex;
			return true;
		}

		bool mdtf_volume_renderer::set_gradient_texture(texture* tex)
		{
			if(!tex || tex->get_nr_dimensions() != 3)
				return false;
			gradient_texture = tex;
			return true;
		}

		bool mdtf_volume_renderer::set_depth_texture(texture* tex)
		{
			if(!tex || tex->get_nr_dimensions() != 2)
				return false;
			depth_texture = tex;
			return true;
		}
		void mdtf_volume_renderer::update_defines(shader_define_map& defines)
		{
			const mdtf_volume_render_style& vrs = get_style<mdtf_volume_render_style>();

			shader_code::set_define(defines, "NUM_STEPS", vrs.integration_quality, mdtf_volume_render_style::IQ_128);
			shader_code::set_define(defines, "INTERPOLATION_MODE", vrs.interpolation_mode, mdtf_volume_render_style::IP_LINEAR);
			shader_code::set_define(defines, "ENABLE_NOISE_OFFSET", vrs.enable_noise_offset, true);
			shader_code::set_define(defines, "ENABLE_SCALE_ADJUSTMENT", vrs.enable_scale_adjustment, false);
			shader_code::set_define(defines, "ENABLE_LIGHTING", vrs.enable_lighting, false);
			shader_code::set_define(defines, "ENABLE_DEPTH_TEST", vrs.enable_depth_test, false);
		}
		bool mdtf_volume_renderer::build_shader_program(context& ctx, shader_program& prog, const shader_define_map& defines)
		{
			return prog.build_program(ctx, "mdtf_volume.glpr", true, defines);
		}

		bool mdtf_volume_renderer::enable(context& ctx)
		{
			const mdtf_volume_render_style& vrs = get_style<mdtf_volume_render_style>();

			if (!renderer::enable(ctx))
				return false;

			if(vrs.enable_lighting && !gradient_texture)
				return false;

			if(vrs.enable_depth_test && !depth_texture)
				return false;

			ref_prog().set_uniform(ctx, "viewport_dims", vec2(ctx.get_width(), ctx.get_height()));
			ref_prog().set_uniform(ctx, "opacity_scale", vrs.opacity_scale);
			ref_prog().set_uniform(ctx, "size_scale", vrs.size_scale);
			ref_prog().set_uniform(ctx, "clip_box_min", vrs.clip_box.get_min_pnt());
			ref_prog().set_uniform(ctx, "clip_box_max", vrs.clip_box.get_max_pnt());

			ref_prog().set_uniform(ctx, "clip_box_transform", vrs.clip_box_transform);
			ref_prog().set_uniform(ctx, "clip_box_transform_inverse", inv(vrs.clip_box_transform));
			ref_prog().set_uniform(ctx, "volume_transform", vrs.volume_transform);
			ref_prog().set_uniform(ctx, "volume_transform_inverse", inv(vrs.volume_transform));

			ref_prog().set_uniform(ctx, "combined_transform", inv(vrs.volume_transform) * vrs.clip_box_transform);
			ref_prog().set_uniform(ctx, "combined_transform_inverse", inv(vrs.clip_box_transform) * vrs.volume_transform);

			glDisable(GL_DEPTH_TEST);

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_FRONT);
			
			volume_texture->enable(ctx, 0);
			noise_texture.enable(ctx, 2);
			if(gradient_texture) gradient_texture->enable(ctx, 3);
			if(depth_texture) depth_texture->enable(ctx, 4);
			return true;
		}
		///
		bool mdtf_volume_renderer::disable(context& ctx)
		{
			volume_texture->disable(ctx);
			noise_texture.disable(ctx);
			if(gradient_texture) gradient_texture->disable(ctx);
			if(depth_texture) depth_texture->disable(ctx);

			glDisable(GL_CULL_FACE);
			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);

			if (!attributes_persist()) {}

			return renderer::disable(ctx);
		}
		///
		void mdtf_volume_renderer::draw(context& ctx, size_t start, size_t count, bool use_strips, bool use_adjacency, uint32_t strip_restart_index)
		{
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)36);
		}
	}
}

#undef P_000
#undef P_001
#undef P_010
#undef P_011
#undef P_100
#undef P_101
#undef P_110
#undef P_111

#include <cgv/gui/provider.h>

namespace cgv {
	namespace gui {

		struct mdtf_volume_render_style_gui_creator : public gui_creator {
			/// attempt to create a gui and return whether this was successful
			bool create(provider* p, const std::string& label,
				void* value_ptr, const std::string& value_type,
				const std::string& gui_type, const std::string& options, bool*) {
				if(value_type != cgv::type::info::type_name<cgv::render::mdtf_volume_render_style>::get_name())
					return false;
				cgv::render::mdtf_volume_render_style* vrs_ptr = reinterpret_cast<cgv::render::mdtf_volume_render_style*>(value_ptr);
				cgv::base::base* b = dynamic_cast<cgv::base::base*>(p);

				p->add_member_control(b, "Quality", vrs_ptr->integration_quality, "dropdown", "enums='8=8,16=16,32=32,64=64,128=128,256=256,512=512,1024=1024'");
				p->add_member_control(b, "Interpolation", vrs_ptr->interpolation_mode, "dropdown", "enums=Nearest,Linear,Smooth,Cubic");
				p->add_member_control(b, "Use Noise", vrs_ptr->enable_noise_offset, "check");
				p->add_member_control(b, "Scale Adjustment", vrs_ptr->size_scale, "value_slider", "w=170;min=0.0;step=0.001;max=500.0;log=true;ticks=true", " ");
				p->add_member_control(b, "", vrs_ptr->enable_scale_adjustment, "check", "w=30");
				p->add_member_control(b, "Opacity Scale", vrs_ptr->opacity_scale, "value_slider", "min=0.0;step=0.001;max=1.0;ticks=true");

				p->add_member_control(b, "Lighting", vrs_ptr->enable_lighting, "check");
				p->add_member_control(b, "Depth Test", vrs_ptr->enable_depth_test, "check");

				p->add_member_control(b, "Box Min", vrs_ptr->clip_box.ref_min_pnt()[0], "value", "w=55;min=0;max=1", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_min_pnt()[1], "value", "w=55;min=0;max=1", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_min_pnt()[2], "value", "w=55;min=0;max=1");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_min_pnt()[0], "slider", "w=55;min=0;step=0.0001;max=1;ticks=true", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_min_pnt()[1], "slider", "w=55;min=0;step=0.0001;max=1;ticks=true", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_min_pnt()[2], "slider", "w=55;min=0;step=0.0001;max=1;ticks=true");

				p->add_member_control(b, "Box Max", vrs_ptr->clip_box.ref_max_pnt()[0], "value", "w=55;max=0;max=1", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_max_pnt()[1], "value", "w=55;max=0;max=1", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_max_pnt()[2], "value", "w=55;max=0;max=1");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_max_pnt()[0], "slider", "w=55;max=0;step=0.0001;max=1;ticks=true", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_max_pnt()[1], "slider", "w=55;max=0;step=0.0001;max=1;ticks=true", " ");
				p->add_member_control(b, "", vrs_ptr->clip_box.ref_max_pnt()[2], "slider", "w=55;max=0;step=0.0001;max=1;ticks=true");

				return true;
			}
		};

#include <cgv_gl/gl/lib_begin.h>

		cgv::gui::gui_creator_registration<mdtf_volume_render_style_gui_creator> mdtf_volume_rs_gc_reg("mdtf_volume_render_style_gui_creator");

	}
}
