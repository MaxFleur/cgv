#include <cgv/base/register.h>

#include "import.h"

#include <cgv/utils/dir.h>
#include <cgv/utils/file.h>
#include <cgv/utils/tokenizer.h>
#include <cstdlib>



#include <stdlib.h>
#include <map>

using namespace cgv::utils;
using namespace cgv::utils::file;

namespace cgv {
	namespace base {

std::string find_data_file_rec(const std::string& path, const std::string& file_name)
{
	if (cgv::utils::file::exists(path+'/'+file_name))
		return path+'/'+file_name;
	void* h = find_first(path+"*");
	while (h) {
		if (find_directory(h) && find_name(h) != "." && find_name(h) != "..") {
			std::string fn = path+find_name(h)+'/';
			if (exists(fn+file_name))
				return fn+file_name;
			fn = find_data_file_rec(fn, file_name);
			if (!fn.empty())
				return fn;
		}
		h = find_next(h);
	}
	return "";
}

std::string find_data_file_1(const std::string& base_path, const std::string& sub_path, const std::string& file_name, bool recurse)
{
	std::string base_path_prefix = base_path;
	if (!base_path_prefix.empty() && base_path_prefix.back() != '/')
		base_path_prefix += "/";
	std::string dir_name = base_path_prefix+sub_path;
	if (!dir_name.empty()) {
		if (!dir::exists(dir_name))
			return "";
		if (dir_name.back() != '/')
			dir_name += "/";
	}
	if (exists(dir_name+file_name))
		return dir_name+file_name;
	if (!recurse)
		return "";
	return find_data_file_rec(dir_name, file_name);
}

std::string find_data_file(const std::string& file_name, const std::string& strategy, const std::string& sub_directory, const std::string& master_path)
{
	for (unsigned i=0; i<strategy.size(); ++i) {
		switch (strategy[i]) {
		case 'r' :
		case 'R' :
			{
				std::map<std::string, resource_file_info>::const_iterator it = ref_resource_file_map().find(file_name);
				if (it != ref_resource_file_map().end())
					return std::string("res://")+file_name;
				break;
			}
		case 'c' :
		case 'C' :
			{
				std::string fn = find_data_file_1("", sub_directory, file_name, strategy[i] == 'C');
				//std::cout << "   current -> " << fn << std::endl;
				if (!fn.empty())
					return fn;
				break;
			}
		case 'm' :
		case 'M' :
			{
				std::string fn = find_data_file_1(master_path, sub_directory, file_name, strategy[i] == 'M');
				if (!fn.empty())
					return fn;
				break;
			}
		case 'd' :
		case 'D' :
			{
				const std::vector<std::string>& path_list = ref_data_path_list();
				for (unsigned int i=0; i<path_list.size(); ++i) {
					std::string fn = find_data_file_1(path_list[i], sub_directory, file_name, strategy[i] == 'D');
					if (!fn.empty())
						return fn;
				}
				break;
			}
		case 'p' :
		case 'P' :
			{
				const std::vector<std::string>& parent_stack = ref_parent_file_stack();
				if (!parent_stack.empty()) {
					std::string fn = find_data_file_1(parent_stack.back(), sub_directory, file_name, strategy[i] == 'P');
					if (!fn.empty())
						return fn;
				}
				break;
			}
		case 'a' :
		case 'A' :
			{
				const std::vector<std::string>& parent_stack = ref_parent_file_stack();
				for (size_t i=parent_stack.size(); i>0; --i) {
					std::string fn = find_data_file_1(parent_stack[i-1], sub_directory, file_name, strategy[i] == 'A');
					if (!fn.empty())
						return fn;
				}
				break;
			}
		}
	}
	return std::string();
}

void stdout_message(const std::string& text)
{
	std::cout << text << std::endl;
}

void stderr_message(const std::string& text)
{
	std::cerr << text << std::endl;
}

int std_query(const std::string& text, const std::string& answers, int default_answer)
{
	std::vector<std::string> answer_strings;
	std::vector<int> answer_values;

	std::vector<cgv::utils::token> tokens;
	cgv::utils::tokenizer(answers).set_ws(",").bite_all(tokens);
	std::cout << text << "\n  select answer from {" << answers << "} with 0..." << tokens.size() - 1 << ":>";
	std::cout.flush();
	int answer;
	std::cin >> answer;
	return answer;
}

std::string std_ask_dir(const std::string& text, const std::string& path)
{
	std::cout << text << "\n:>";
	std::cout.flush();
	std::string answer;
	std::cin >> answer;
	return answer;
}

std::string find_or_download_data_file(const std::string& file_name, const std::string& find_strategy,
	const std::string& url, const std::string& cache_strategy, const std::string& producer,
	const std::string& sub_directory, const std::string& master_path, user_feedback uf)
{
	// first try to find file 
	std::string file_path = cgv::base::find_data_file(file_name, find_strategy, sub_directory, master_path);
	if (!file_path.empty())
		return file_path;

	// if not found find directory in which to cache download according to \c cache_strategy
	size_t i = 0;
	while (i < cache_strategy.size() && file_path.empty()) {
		switch (cache_strategy[i++]) {
		case 'c':
		case 'C': file_path = "."; break;
		case 'm':
		case 'M': if (cgv::utils::dir::exists(master_path))
			file_path = master_path;
			break;
		case 'd':
		case 'D': {
			const std::vector<std::string>& path_list = cgv::base::ref_data_path_list();
			for (unsigned int i = 0; i < path_list.size(); ++i)
				if (cgv::utils::dir::exists(path_list[i])) {
					file_path = path_list[i];
					break;
				}
			break;
		}
		case 'p':
		case 'P': {
			const std::vector<std::string>& parent_stack = cgv::base::ref_parent_file_stack();
			if (!parent_stack.empty()) {
				if (cgv::utils::dir::exists(parent_stack.back()))
					file_path = parent_stack.back();
			}
			break;
		}
		case 'a':
		case 'A': {
			const std::vector<std::string>& parent_stack = cgv::base::ref_parent_file_stack();
			for (size_t i = parent_stack.size(); i > 0; --i) {
				if (cgv::utils::dir::exists(parent_stack[i])) {
					file_path = parent_stack[i];
					break;
				}
			}
			break;
		}
		}
	}
	if (file_path.empty()) {
		if (uf.query == 0)
			return "";
		if (uf.query(producer + " wants to download <" + file_name + "> but has no path to store it. Do you want to specify a path to store the download?",
			"Yes,No", 0) != 1)
			return "";
		if (uf.ask_dir == 0)
			return "";
		file_path = uf.ask_dir(std::string("specify path to store ") + producer + " downloads", master_path);
		if (file_path.empty())
			return "";
	}
	// extend file path with subdirectory
	if (!sub_directory.empty()) {
		if (file_path.back() != '/' && file_path.back() != '\\')
			file_path += '/';
		file_path += sub_directory;
		if (!cgv::utils::dir::exists(file_path)) {
			if (uf.query == 0)
				return "";
			if (uf.query(producer + " wants to create directory <" + file_path + "> to store downloads. Do you allow this?",
				"Yes,No", 0) != 1)
				return "";
			if (!cgv::utils::dir::mkdir(file_path)) {
				if (uf.message != 0)
					uf.message(std::string("could not create subdirectory <") + sub_directory + "> in cache path");
				return "";
			}
		}
	}
	// append file name
	if (file_path.back() != '/' && file_path.back() != '\\')
		file_path += '/';
	file_path += file_name;
	// try to download file
	std::string cmd = "curl --output \"";
	cmd += file_path + "\" " + url;
	bool retry;
	do {
		retry = false;
		int result = system(cmd.c_str());
		if (result == -1 || !cgv::utils::file::exists(file_path)) {
			if (uf.query == 0)
				return "";
			if (uf.query(std::string("download of <") + file_name + "> failed. Please check internet connectivity! Try again?",
				"Yes,No", 0) != 1)
				return "";
			retry = true;
		}
	} while (retry);
	return file_path;
}

std::string clean_data_path(const std::string& data_path)
{
	if (data_path.size() == 0)
		return data_path;
	unsigned int p = (unsigned int) data_path.size()-1;
	if (data_path[p] == '/' || data_path[p] == '\\')
		return clean_data_path(data_path.substr(0,p));
	return data_path;
}

/// return a reference to the data path list, which is constructed from the environment variable CGV_DATA
std::vector<std::string>& ref_data_path_list()
{
	static std::vector<std::string> data_path_list;
	static bool initialized = false;
	if (!initialized) {
		char* cgv_data = getenv("CGV_DATA");
		if (cgv_data) {
			std::string cgv_data_str(cgv_data);
			std::vector<token> data_path_tokens;
			bite_all(tokenizer(cgv_data_str).set_ws(";"),data_path_tokens);
			for (unsigned int i=0; i<data_path_tokens.size(); ++i) {
				std::string data_path = to_string(data_path_tokens[i]);
				data_path_list.push_back(clean_data_path(data_path));
			}
		}
		initialized = true;
	}
	return data_path_list;
}

/// extract a valid path from the given argument and push it onto the stack of parent paths. This should always be paired with a call to pop_file_parent().
void push_file_parent(const std::string& path_or_file_name)
{
	bool is_path;
	if (cgv::utils::dir::exists(path_or_file_name))
		is_path = true;
	else if (cgv::utils::file::exists(path_or_file_name))
		is_path = false;
	else if (cgv::utils::file::get_extension(path_or_file_name).empty())
		is_path = true;
	else
		is_path = false;

	if (is_path)
		ref_parent_file_stack().push_back(path_or_file_name);
	else
		ref_parent_file_stack().push_back(cgv::utils::file::get_path(path_or_file_name));
}

/// pop the latestly pushed parent path from the parent path stack.
void pop_file_parent()
{
	if (!ref_parent_file_stack().empty())
		ref_parent_file_stack().pop_back();
}

/// return a reference to the data path list, which is constructed from the environment variable CGV_DATA
std::vector<std::string>& ref_parent_file_stack()
{
	static std::vector<std::string> parent_file_stack;
	return parent_file_stack;
}

bool find_and_extend_system_path(std::string& file_name)
{
	std::string system_paths(std::getenv("PATH"));
	std::vector<cgv::utils::token> path_tokens;
	bite_all(tokenizer(system_paths).set_ws(
#ifdef _WIN32
		";"
#else
		":"
#endif
		), path_tokens);

	for (unsigned int i = 0; i<path_tokens.size(); ++i) {
		std::string system_path = to_string(path_tokens[i]);
		if (cgv::utils::file::exists(system_path + "/" + file_name)) {
			file_name = system_path + "/" + file_name;
			return true;
		}
	}
	return false;
}

/// open a file with fopen supporting resource files, that have the prefix "res://"
FILE* open_data_file(const std::string& file_name, const char* mode)
{
	if (file_name.substr(0,6) != "res://")
		return fopen(file_name.c_str(), mode);
	std::map<std::string, resource_file_info>::const_iterator i = ref_resource_file_map().find(file_name.substr(6));
	if (i == ref_resource_file_map().end())
		return 0;
	std::string source_file = i->second.source_file;
	if (source_file.empty())
		source_file = ref_prog_path_prefix() + ref_prog_name();
	else {
		if (cgv::utils::file::exists(ref_prog_path_prefix() + source_file))
			source_file = ref_prog_path_prefix() + source_file;
		else {
			if (!find_and_extend_system_path(source_file)) {
				std::cerr << "ERROR: could not find " << source_file << " to import " << i->second.file_data << std::endl;
				return 0;
			}
		}
	}
	unsigned int off = i->second.file_offset;
	if (off == (unsigned int)-1) {
		off = find_file_offset(source_file, i->second.file_data, i->second.file_length);
		if (off == (unsigned int)-1) {
			std::cerr << "ERROR: could not find offset of " << i->second.file_data << " to import from " << source_file << std::endl;
			return 0;
		}
	}
	FILE* fp = fopen(source_file.c_str(), mode);
	if (!fp) {
		std::cerr << "ERROR: could not open " << source_file << " to  import " << i->second.file_data << std::endl;
		return 0;
	}
	fseek(fp, off, SEEK_SET);
	return fp;
}

/// read ascii file into a string
bool read_data_file(const std::string& file_name, std::string& content, bool ascii)
{
	if (file_name.substr(0, 6) == "str://") {
		std::map<std::string, resource_file_info>::const_iterator it = ref_resource_file_map().find(file_name.substr(6));
		if (it == ref_resource_file_map().end())
			return false;
		content = it->second.file_data;
		return true;
	}
	if (file_name.substr(0, 6) != "res://")
		return file::read(file_name, content, ascii);
	unsigned int n = data_file_size(file_name);
	content.resize(n);
	FILE* fp = open_data_file(file_name, ascii?"ra":"rb");
	if (!fp)
		return false;
	if (!fread(&content[0], 1, n, fp)) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}

/// return the file size of a given file with support for resource files, that have the prefix "res://"
unsigned int data_file_size(const std::string& file_name)
{
	if (file_name.substr(0,6) != "res://")
		return (unsigned int) file::size(file_name.c_str());
	return ref_resource_file_map()[file_name.substr(6)].file_length;
}

/// find the offset of the given data block in the given file
unsigned int find_file_offset(const std::string& file_name, const char* data, unsigned int data_size)
{
	size_t file_size;
	char* file_data = read(file_name, false, &file_size);
	if (!file_data || file_size < data_size)
		return -1;
	unsigned int n = (unsigned int) (file_size-data_size);
	char* fdata = file_data;
	for (unsigned int off = 0; off < n; ++off, ++fdata) {
		bool found = true;
		for (unsigned int i = 0; i < data_size; ++i) {
			if (data[i] != fdata[i]) {
				found = false;
				break;
			}
		}
		if (found) {
			delete [] file_data;
			return off;
		}
	}
	delete [] file_data;
	return -1;
}


	}
}
