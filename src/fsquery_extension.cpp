#define DUCKDB_EXTENSION_MAIN

#include "fsquery_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <duckdb/parser/parsed_data/create_table_function_info.hpp>

#include <sys/stat.h>
#include <queue>

namespace duckdb {

// Column indices for the table function
enum FsQueryColumns : idx_t {
	COLUMN_PATH = 0,
	COLUMN_MODE = 1,
	COLUMN_SIZE = 2,
	COLUMN_UID = 3,
	COLUMN_GID = 4,
	COLUMN_ATIME = 5,
	COLUMN_MTIME = 6,
	COLUMN_CTIME = 7,
	COLUMN_NLINK = 8,
	COLUMN_INO = 9,
	COLUMN_DEV = 10,
	COLUMN_IS_REGULAR_FILE = 11,
	COLUMN_IS_DIRECTORY = 12,
	COLUMN_IS_SYMLINK = 13
};

struct FsQueryBindData : public TableFunctionData {
	string path;

	explicit FsQueryBindData(string path_p) : path(std::move(path_p)) {}
};

struct FsQueryGlobalState : public GlobalTableFunctionState {
	vector<string> paths;
	idx_t current_idx = 0;
	vector<column_t> column_ids;

	FsQueryGlobalState() = default;
};

static void RecursiveListFiles(FileSystem &fs, const string &path, vector<string> &result) {
	// Add the current path
	result.push_back(path);

	// Check if it's a directory
	try {
		if (!fs.DirectoryExists(path)) {
			// It's a file, not a directory
			return;
		}
	} catch (...) {
		// If we can't determine, skip it
		return;
	}

	// List files in the directory
	try {
		fs.ListFiles(path, [&](const string &fname, bool is_dir) {
			// Build full path
			string full_path = fs.JoinPath(path, fname);
			// Recursively process
			RecursiveListFiles(fs, full_path, result);
		});
	} catch (...) {
		// Skip directories we can't read
	}
}

static unique_ptr<FunctionData> FsQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                             vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<FsQueryBindData>(input.inputs[0].ToString());

	// Define the output schema with all stat fields
	names.emplace_back("path");
	return_types.emplace_back(LogicalType::VARCHAR);

	names.emplace_back("mode");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("size");
	return_types.emplace_back(LogicalType::BIGINT);

	names.emplace_back("uid");
	return_types.emplace_back(LogicalType::UINTEGER);

	names.emplace_back("gid");
	return_types.emplace_back(LogicalType::UINTEGER);

	names.emplace_back("atime");
	return_types.emplace_back(LogicalType::TIMESTAMP);

	names.emplace_back("mtime");
	return_types.emplace_back(LogicalType::TIMESTAMP);

	names.emplace_back("ctime");
	return_types.emplace_back(LogicalType::TIMESTAMP);

	names.emplace_back("nlink");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("ino");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("dev");
	return_types.emplace_back(LogicalType::UBIGINT);

	names.emplace_back("is_regular_file");
	return_types.emplace_back(LogicalType::BOOLEAN);

	names.emplace_back("is_directory");
	return_types.emplace_back(LogicalType::BOOLEAN);

	names.emplace_back("is_symlink");
	return_types.emplace_back(LogicalType::BOOLEAN);

	return std::move(result);
}

static unique_ptr<GlobalTableFunctionState> FsQueryInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<FsQueryBindData>();
	auto result = make_uniq<FsQueryGlobalState>();
	auto &fs = FileSystem::GetFileSystem(context);

	// Store which columns are requested for projection pushdown
	result->column_ids = input.column_ids;

	// Recursively walk the directory and collect all paths
	try {
		RecursiveListFiles(fs, bind_data.path, result->paths);
	} catch (const std::exception &e) {
		throw IOException("Failed to iterate directory: %s", e.what());
	}

	return std::move(result);
}

static void FsQueryFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &gstate = data_p.global_state->Cast<FsQueryGlobalState>();
	idx_t count = 0;

	while (gstate.current_idx < gstate.paths.size() && count < STANDARD_VECTOR_SIZE) {
		const auto &current_path = gstate.paths[gstate.current_idx++];

		// Stat the file
		struct stat file_stat;

#ifdef _WIN32
		// Windows uses _stat or _stat64
		if (_stat64(current_path.c_str(), &file_stat) != 0) {
			// Skip files we can't stat
			continue;
		}
#else
		// Unix/Linux/macOS use stat
		if (stat(current_path.c_str(), &file_stat) != 0) {
			// Skip files we can't stat
			continue;
		}
#endif

		// Fill in the output columns - only compute requested columns
		for (idx_t col_idx = 0; col_idx < gstate.column_ids.size(); col_idx++) {
			auto column_id = gstate.column_ids[col_idx];

			switch (column_id) {
			case COLUMN_PATH:
				output.data[col_idx].SetValue(count, Value(current_path));
				break;
			case COLUMN_MODE:
				output.data[col_idx].SetValue(count, Value::UBIGINT(file_stat.st_mode));
				break;
			case COLUMN_SIZE:
				output.data[col_idx].SetValue(count, Value::BIGINT(file_stat.st_size));
				break;
			case COLUMN_UID:
#ifdef _WIN32
				output.data[col_idx].SetValue(count, Value::UINTEGER(0));
#else
				output.data[col_idx].SetValue(count, Value::UINTEGER(file_stat.st_uid));
#endif
				break;
			case COLUMN_GID:
#ifdef _WIN32
				output.data[col_idx].SetValue(count, Value::UINTEGER(0));
#else
				output.data[col_idx].SetValue(count, Value::UINTEGER(file_stat.st_gid));
#endif
				break;
			case COLUMN_ATIME:
				output.data[col_idx].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_atime)));
				break;
			case COLUMN_MTIME:
				output.data[col_idx].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_mtime)));
				break;
			case COLUMN_CTIME:
				output.data[col_idx].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_ctime)));
				break;
			case COLUMN_NLINK:
				output.data[col_idx].SetValue(count, Value::UBIGINT(file_stat.st_nlink));
				break;
			case COLUMN_INO:
				output.data[col_idx].SetValue(count, Value::UBIGINT(file_stat.st_ino));
				break;
			case COLUMN_DEV:
				output.data[col_idx].SetValue(count, Value::UBIGINT(file_stat.st_dev));
				break;
			case COLUMN_IS_REGULAR_FILE: {
				bool is_regular = S_ISREG(file_stat.st_mode);
				output.data[col_idx].SetValue(count, Value::BOOLEAN(is_regular));
				break;
			}
			case COLUMN_IS_DIRECTORY: {
				bool is_dir = S_ISDIR(file_stat.st_mode);
				output.data[col_idx].SetValue(count, Value::BOOLEAN(is_dir));
				break;
			}
			case COLUMN_IS_SYMLINK: {
#ifdef _WIN32
				bool is_symlink = false;
#else
				bool is_symlink = S_ISLNK(file_stat.st_mode);
#endif
				output.data[col_idx].SetValue(count, Value::BOOLEAN(is_symlink));
				break;
			}
			default:
				break;
			}
		}

		count++;
	}

	output.SetCardinality(count);
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register the table function
	TableFunction fsquery_table("fsquery_scan", {LogicalType::VARCHAR}, FsQueryFunction, FsQueryBind, FsQueryInit);
	// Enable projection pushdown
	fsquery_table.projection_pushdown = true;
	loader.RegisterFunction(fsquery_table);
}

void FsqueryExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string FsqueryExtension::Name() {
	return "fsquery";
}

std::string FsqueryExtension::Version() const {
#ifdef EXT_VERSION_FSQUERY
	return EXT_VERSION_FSQUERY;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(fsquery, loader) {
	duckdb::LoadInternal(loader);
}
}
