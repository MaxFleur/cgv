/** BEGIN - MFLEURY **/

#pragma once

/* This class contains the main primitive data which is used by both editors and the viewer. */
class shared_data {

public:

	// Types of the primitive
	enum Type {
		TYPE_GAUSS = 0,
		TYPE_BOX = 1,
		TYPE_SPHERE = 2
	};

	// Main primitive, containing a type, a color and the centroid widths and positions
	struct primitive
	{
		Type type{ TYPE_GAUSS };

		// Default color: blue to see the new centroid better
		rgba color{ 0.0f, 0.0f, 1.0f, 0.5f };

		vec4 centroid{ 0.5f, 0.5f, 0.5f, 0.5f };

		vec4 widths{ 0.5f, 0.5f, 0.5f, 0.5f };

		int last_masked_id = INT_MAX;
	};

public:
	std::vector<primitive> primitives;

	// Set after the editors were synchronized
	void set_synchronized(bool is_line_editor = true) {
		is_synchronized = false;
		if (is_line_editor) {
			scatterplot_updated = false;
		}
	}

	// Check if the primitive visualization is synchronized in all editors
	bool is_synchronized = true;

	bool is_primitive_selected = false;
	int selected_primitive_id = INT_MAX;

	bool scatterplot_updated = true;
};

typedef std::shared_ptr<shared_data> shared_data_ptr;

/** END - MFLEURY **/
