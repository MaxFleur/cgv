#pragma once

#include <queue>
#include <omp.h>

#include <cgv/render/render_types.h>
#include <cgv/utils/stopwatch.h>

#include "sliced_volume_data_set.h"

struct gridtree : public cgv::render::render_types {
	struct statistics {
		float min[4];
		float max[4];
		float sum[4];
		float sms[4];
		unsigned int cnt = 0;

		statistics() {
			for(size_t i = 0; i < 4; ++i) {
				min[i] = std::numeric_limits<float>::max();
				max[i] = -std::numeric_limits<float>::max();
				sum[i] = 0.0f;
				sms[i] = 0.0f;
			}
		}

		statistics(const statistics& a, const statistics& b) {
			for(size_t i = 0; i < 4; ++i) {
				min[i] = std::min(a.min[i], b.min[i]);
				max[i] = std::max(a.max[i], b.max[i]);
				sum[i] = a.sum[i] + b.sum[i];
				sms[i] = a.sms[i] + b.sms[i];
			}
			cnt = a.cnt + b.cnt;
		}

		void update(const float v[4]) {
			for(size_t i = 0; i < 4; ++i) {
				min[i] = std::min(min[i], v[i]);
				max[i] = std::max(max[i], v[i]);
				if(v[i] < min[i]) min[i] = v[i];
				if(v[i] > max[i]) max[i] = v[i];
				sum[i] += v[i];
				sms[i] += v[i] * v[i];
			}
			++cnt;
		}

		void update(const statistics& s) {
			for(size_t i = 0; i < 4; ++i) {
				min[i] = std::min(min[i], s.min[i]);
				max[i] = std::max(max[i], s.max[i]);
				sum[i] += s.sum[i];
				sms[i] += s.sms[i];
			}
			cnt += s.cnt;
		}

		vec4 get_minimum() const { return vec4(min[0], min[1], min[2], min[3]); }

		vec4 get_maximum() const { return vec4(max[0], max[1], max[2], max[3]); }

		vec4 get_sum() const { return vec4(sum[0], sum[1], sum[2], sum[3]); }

		vec4 get_sum_of_squares() const { return vec4(sms[0], sms[1], sms[2], sms[3]); }

		vec4 get_average() const { return get_sum() / cnt; }

		vec4 get_variance() const {
			vec4 E = get_average();
			vec4 E2 = get_sum_of_squares() / cnt;
			return E2 - E * E;
		}
		/// compute standard deviation of the considered values
		vec4 get_standard_deviation() const {
			vec4 v = get_variance();
			v[0] = sqrt(v[0]);
			v[1] = sqrt(v[1]);
			v[2] = sqrt(v[2]);
			v[3] = sqrt(v[3]);
			return v;
		}
	};

	/// declare type of 3d 16 bit integer vectors
	typedef cgv::math::fvec<int16_t, 3> i16vec3;

	struct node {
		int child_idx = -1;
		uint8_t child_count = 0;
		i16vec3 a;
		i16vec3 b;
		statistics stats;
	};

	typedef std::vector<node> node_vector;
	std::vector<node_vector> levels;

	int get_mid(int a, int b) {
		return a + (b - a) / 2;
	}

	bool split_node(node& n, node_vector& child_nodes) {
		i16vec3 ext = n.b - n.a;

		int count = -1;
		int ai[2];

		if(ext.x() > 1)
			ai[++count] = 0;

		if(ext.y() > 1)
			ai[++count] = 1;

		if(ext.z() > 1)
			++count;

		n.child_idx = child_nodes.size();

		switch(count) {
		case 0:
		{
			// split along one dimension
			n.child_count = 2u;
			int idx = child_nodes.size();
			child_nodes.resize(idx + 2, n);

			int i0 = ai[0];
			int m = get_mid(n.a[i0], n.b[i0]);

			child_nodes[idx + 0].b[i0] = m;
			child_nodes[idx + 1].a[i0] = m;
		}
		break;
		case 1:
		{
			// split along two dimensions
			n.child_count = 4u;
			int idx = child_nodes.size();
			child_nodes.resize(idx + 4, n);

			int i0 = ai[0];
			int i1 = ai[1];
			int m0 = get_mid(n.a[i0], n.b[i0]);
			int m1 = get_mid(n.a[i1], n.b[i1]);

			child_nodes[idx + 0].b[i0] = m0;
			child_nodes[idx + 1].a[i0] = m0;
			child_nodes[idx + 2].b[i0] = m0;
			child_nodes[idx + 3].a[i0] = m0;

			child_nodes[idx + 0].b[i1] = m1;
			child_nodes[idx + 2].a[i1] = m1;
			child_nodes[idx + 1].b[i1] = m1;
			child_nodes[idx + 3].a[i1] = m1;
		}
		break;
		case 2:
		{
			// split along three dimensions
			n.child_count = 8u;
			int idx = child_nodes.size();
			child_nodes.resize(idx + 8, n);

			int mx = get_mid(n.a.x(), n.b.x());
			int my = get_mid(n.a.y(), n.b.y());
			int mz = get_mid(n.a.z(), n.b.z());

			child_nodes[idx + 0].b.x() = mx;
			child_nodes[idx + 1].a.x() = mx;
			child_nodes[idx + 2].b.x() = mx;
			child_nodes[idx + 3].a.x() = mx;
			child_nodes[idx + 4].b.x() = mx;
			child_nodes[idx + 5].a.x() = mx;
			child_nodes[idx + 6].b.x() = mx;
			child_nodes[idx + 7].a.x() = mx;

			child_nodes[idx + 0].b.y() = my;
			child_nodes[idx + 2].a.y() = my;
			child_nodes[idx + 1].b.y() = my;
			child_nodes[idx + 3].a.y() = my;
			child_nodes[idx + 4].b.y() = my;
			child_nodes[idx + 6].a.y() = my;
			child_nodes[idx + 5].b.y() = my;
			child_nodes[idx + 7].a.y() = my;

			child_nodes[idx + 0].b.z() = mz;
			child_nodes[idx + 4].a.z() = mz;
			child_nodes[idx + 1].b.z() = mz;
			child_nodes[idx + 5].a.z() = mz;
			child_nodes[idx + 2].b.z() = mz;
			child_nodes[idx + 6].a.z() = mz;
			child_nodes[idx + 3].b.z() = mz;
			child_nodes[idx + 7].a.z() = mz;
		}
		break;
		default:
			// no split
			n.child_idx = 0;
			break;
		}

		return ext.x() > 2 || ext.y() > 2 || ext.z() > 2;
	}

	void build(const sliced_volume_data_set& dataset) {

		levels.clear();

		int current_level = 0;

		levels.push_back(node_vector());

		node root;
		root.a = ivec3(0);
		root.b = dataset.resolution;
		levels[0].push_back(root);

		bool run = true;
		while(run) {
			levels.push_back(node_vector());
			auto& current_nodes = levels[current_level];
			auto& next_nodes = levels[current_level + 1];

			next_nodes.reserve(4 * current_nodes.size());

			bool needs_split = false;
			for(auto& n : current_nodes)
				needs_split |= split_node(n, next_nodes);

			if(next_nodes.size() == 0 || current_level > 16 || !needs_split)
				run = false;

			++current_level;
		}

		if(levels.back().size() == 0)
			levels.pop_back();

		if(levels.size() == 0)
			return;

		int last_idx = static_cast<int>(levels.size()) - 1;
		for(int i = last_idx; i >= 0; --i) {
			auto& nodes = levels[i];
			if(i == last_idx) {
#pragma omp parallel for
				for(int i = 0; i < nodes.size(); ++i) {
					auto& n = nodes[i];
					i16vec3 a = n.a;
					i16vec3 e = n.b - n.a;

					//if(e.x() * e.y() * e.z() > 1)
					//	std::cout << "Found leaf node with more than 1 voxels!" << std::endl;

					float values[4];
					values[0] = static_cast<float>(dataset.raw_data.get<unsigned char>(0, a.z(), a.y(), a.x())) / 255.0f;
					values[1] = static_cast<float>(dataset.raw_data.get<unsigned char>(1, a.z(), a.y(), a.x())) / 255.0f;
					values[2] = static_cast<float>(dataset.raw_data.get<unsigned char>(2, a.z(), a.y(), a.x())) / 255.0f;
					values[3] = static_cast<float>(dataset.raw_data.get<unsigned char>(3, a.z(), a.y(), a.x())) / 255.0f;

					n.stats.update(values);
				}
			} else {
				auto& child_nodes = levels[i + 1];

#pragma omp parallel for
				for(int i = 0; i < nodes.size(); ++i) {
					auto& n = nodes[i];
					for(size_t j = n.child_idx; j < n.child_idx + n.child_count; ++j)
						n.stats.update(child_nodes[j].stats);
				}
			}
		}
	}

	void print_info() {

		std::cout << "Tree depth: " << levels.size() << std::endl;
		std::cout << "Leaf count: " << levels.back().size() << std::endl;

		std::cout << "Root statistics: " << std::endl;
		const auto& s = levels[0][0].stats;
		for(size_t i = 0; i < 4; ++i) {
			std::cout << i << ": ";
			std::cout << "cnt = " << s.cnt << ", ";
			std::cout << "min = " << s.min[i] << ", ";
			std::cout << "max = " << s.max[i] << ", ";
			std::cout << "avg = " << s.get_average()[i] << ", ";
			std::cout << "var = " << s.get_variance()[i] << std::endl;
		}
		std::cout << std::endl;
	}

	node_vector extract_leafs(float error_threshold) {

		node_vector result;

		if(levels.size() == 0 || levels[0].size() == 0)
			return result;

		typedef std::pair<int, const node*> queue_entry;
		std::queue<queue_entry, std::deque<queue_entry>> q;
		q.push(std::make_pair(0, &levels[0][0]));

		//float norm = 0.25f;
		float norm = 0.5f;
		float threshold = norm * error_threshold;

		int max_level = levels.size() - 1;

		while(!q.empty()) {
			const auto& e = q.front();

			int level = e.first;
			int next_level = level + 1;
			const auto n = e.second;

			//vec4 measure = n->stats.get_variance();
			vec4 measure = n->stats.get_standard_deviation();

			float mean_measure = 0.25f * (measure[0] + measure[1] + measure[2] + measure[3]);

			if(mean_measure >= threshold && next_level <= max_level) {
				const auto& child_nodes = levels[next_level];
				for(size_t i = n->child_idx; i < n->child_idx + n->child_count; ++i)
					q.push(std::make_pair(next_level, &child_nodes[i]));
			} else {
				result.push_back(*n);
			}

			q.pop();
		}

		return result;
	}
};
