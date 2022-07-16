/** BEGIN - MFLEURY **/

#pragma once

/* This class contains data used by all myofibril editors. */
class shared_data {

public:

	enum Type {
		TYPE_GTF = 0,
		TYPE_BOX = 1,
		TYPE_SPHERE = 2
	};

	struct primitive
	{
		Type type{ TYPE_GTF };

		// Default color: blue to see the new centroid better
		rgba color{ 0.0f, 0.0f, 1.0f, 0.5f };

		vec4 centr_pos{ 0.0f, 0.0f, 0.0f, 0.0f };

		vec4 centr_widths{ 0.5f, 0.5f, 0.5f, 0.5f };
	};

public:
	std::vector<primitive> primitives;

	void set_synchronized(bool is_line_editor = true) {
		is_synchronized = true;
		if (is_line_editor) {
			update_scatterplot = true;
		}
	}

	bool is_synchronized = false;
	bool update_scatterplot = false;
};

typedef std::shared_ptr<shared_data> shared_data_ptr;

/** END - MFLEURY **/
