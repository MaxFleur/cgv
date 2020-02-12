/* This file is part of the cgv framework realsense driver
*  for use ensure that a diretory containing realsense2.dll is in the PATH variable else the plugin fails to load
*  e.g. C:\Program Files (x86)\Intel RealSense SDK 2.0\bin\x86
*  also make sure REALSENSE_DIR is set
*  e.g. C:\Program Files (x86)\Intel RealSense SDK 2.0
*/
#pragma once

#include <rgbd_input.h>
#include "lib_begin.h"

namespace rgbd {

	class CGV_API rgbd_realsense : public rgbd_device
	{
	public:
		/// create a detached realsense device object
		rgbd_realsense();
		~rgbd_realsense();
		
		/// attach to the realsense device of the given serial, the expected serial is the same as returned by rgbd_realsense_driver::get_serial
		bool attach(const std::string& serial);
		/// return whether device object is attached to a realsense device
		bool is_attached() const;
		/// detaches the object from the realsense device
		bool detach();
		/// check whether the device supports the given combination of input streams
		bool check_input_stream_configuration(InputStreams is) const;
		void query_stream_formats(InputStreams is, std::vector<stream_format>& stream_formats) const;
		bool start_device(InputStreams is, std::vector<stream_format>& stream_formats);
		/// start the rgbd device with given stream formats 
		bool start_device(const std::vector<stream_format>& stream_formats);
		/// stop the camera
		bool stop_device();
		/// return whether device has been started
		bool is_running() const;
		/// query a frame of the given input stream
		bool get_frame(InputStreams is, frame_type& frame, int timeOut);
		/// map a color frame to the image coordinates of the depth image
		void map_color_to_depth(const frame_type& depth_frame, const frame_type& color_frame,
			frame_type& warped_color_frame) const;
		/// map a depth value together with pixel indices to a 3D point with coordinates in meters; point_ptr needs to provide space for 3 floats
		bool map_depth_to_point(int x, int y, int depth, float* point_ptr) const;
		/// get the camera intrinsics
		bool get_intrinsics(camera_intrinsics& intrinsics) const;

	protected:
		rs2::context* ctx;
		rs2::device* dev;
		rs2::pipeline* pipe;
		rs2::temporal_filter temp_filter;
		rs2::spatial_filter spatial_filter;
		std::string serial;
		/// selected stream formats for color,depth and infrared
		stream_format color_stream, depth_stream, ir_stream;
		/// holds last consistent set of frames
		rs2::frameset frame_cache;
		double last_color_frame_time, last_depth_frame_time, last_ir_frame_time;
		size_t last_color_frame_number, last_depth_frame_number, last_ir_frame_number;
		double depth_scale;
	};

	class CGV_API rgbd_realsense_driver : public rgbd_driver
	{
	public:
		/// construct CLNUI driver
		rgbd_realsense_driver();
		/// destructor
		~rgbd_realsense_driver();
		/// return the number of realsense devices found by driver
		unsigned get_nr_devices();
		/// return the serial of the i-th realsense devices
		std::string get_serial(int i);
		/// create a kinect device
		rgbd_device* create_rgbd_device();
	};

}
#include <cgv/config/lib_end.h>