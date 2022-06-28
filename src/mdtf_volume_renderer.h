#pragma once

#include <cgv_gl/renderer.h>

#include <cgv_gl/gl/lib_begin.h>

namespace cgv { // @<
	namespace render { // @<
		
		class mdtf_volume_renderer;

		//! reference to a singleton volume renderer that is shared among drawables
		/*! the second parameter is used for reference counting. Use +1 in your init method,
			-1 in your clear method and default 0 argument otherwise. If internal reference
			counter decreases to 0, singleton renderer is destructed. */
		extern mdtf_volume_renderer& ref_mdtf_volume_renderer(context& ctx, int ref_count_change = 0);

		/** style of a volume */
		struct mdtf_volume_render_style : public render_style {
			/*@name global volume rendering options*/
			//@{
			/// quality measure for the number of steps used during raymarching
			enum IntegrationQuality {
				IQ_8 = 8,
				IQ_16 = 16,
				IQ_32= 32,
				IQ_64 = 64,
				IQ_128 = 128,
				IQ_256 = 256,
				IQ_512 = 512,
				IQ_1024 = 1024,
			} integration_quality;
			/// the interpolation method used
			enum InterpolationMode {
				IP_NEAREST = 0,
				IP_LINEAR = 1,
				IP_SMOOTH = 2,
				IP_CUBIC = 3
			} interpolation_mode;
			/// whether to use the noise texture to offset ray start positions in order to reduce sampling artifacts
			bool enable_noise_offset;
			/// whether to enable the scale adjustment
			bool enable_scale_adjustment;
			/// the coefficient used to adjust for volume scaling
			float size_scale;
			/// opacity scaling parameter
			float opacity_scale;
			/// a bounding box used to define a subspace of the volume to be visualized
			box3 clip_box;

			mat4 clip_box_transform;
			mat4 volume_transform;
			
			/// whether to enable lighting (gradient texture must be supplied)
			bool enable_lighting;
			/// wether to enable depth testing by reading depth from a texture to allow geometry intersecting the volume (depth texture must be supplied)
			bool enable_depth_test;
			/// construct with default values
			mdtf_volume_render_style();
		};

		/// renderer that supports point splatting
		class mdtf_volume_renderer : public renderer
		{
		protected:
			// TODO: rename or use the one from the renderer base class?
			cgv::render::attribute_array_manager aa_manager;
			/// the 3D texture used for rendering
			texture* volume_texture;
			/// a 2D texture containing random noise used to offset ray start positions in order to reduce ring artifacts
			texture noise_texture;
			/// the 3D texture containing vector gradients used for lighting normal calculation
			texture* gradient_texture;
			/// a 2D texture from a frame buffer depth buffer used to combine volume rendering with opaque geometry
			texture* depth_texture;
			/// overload to allow instantiation of volume_renderer
			render_style* create_render_style() const;
			/// update shader defines based on render style
			void update_defines(shader_define_map& defines);
			/// build rounded cone program
			bool build_shader_program(context& ctx, shader_program& prog, const shader_define_map& defines);

			void init_noise_texture(context& ctx);
		public:
			/// initializes position_is_center to true 
			mdtf_volume_renderer();
			/// construct shader programs and return whether this was successful, call inside of init method of drawable
			bool init(context& ctx);
			/// sets the 3D volume texture containing scalar values (density or other measured quantities)
			bool set_volume_texture(texture* tex);

			void setCentroid(vec4 centroid)
			{
				m_centroid = centroid;
			};

			void setGaussianWidth(float width)
			{
				m_gaussian_width = width;
			};

			bool set_gradient_texture(texture* tex);
			bool set_depth_texture(texture* tex);

			///
			bool enable(context& ctx);
			///
			bool validate_attributes(const context& ctx) const;
			///
			bool disable(context& ctx);
			///
			void draw(context& ctx, size_t start, size_t count,
				bool use_strips = false, bool use_adjacency = false, uint32_t strip_restart_index = -1);

		private:
			vec4 m_centroid;
			float m_gaussian_width;
		};
	}
}

#include <cgv/config/lib_end.h>