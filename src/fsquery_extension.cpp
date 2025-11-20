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

struct FsQueryBindData : public TableFunctionData {
	string path;

	explicit FsQueryBindData(string path_p) : path(std::move(path_p)) {}
};

struct FsQueryGlobalState : public GlobalTableFunctionState {
	vector<string> paths;
	idx_t current_idx = 0;

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

		// Fill in the output columns
		idx_t col_idx = 0;

		// path
		output.data[col_idx++].SetValue(count, Value(current_path));

		// mode
		output.data[col_idx++].SetValue(count, Value::UBIGINT(file_stat.st_mode));

		// size
		output.data[col_idx++].SetValue(count, Value::BIGINT(file_stat.st_size));

		// uid
#ifdef _WIN32
		output.data[col_idx++].SetValue(count, Value::UINTEGER(0)); // Windows doesn't have uid
#else
		output.data[col_idx++].SetValue(count, Value::UINTEGER(file_stat.st_uid));
#endif

		// gid
#ifdef _WIN32
		output.data[col_idx++].SetValue(count, Value::UINTEGER(0)); // Windows doesn't have gid
#else
		output.data[col_idx++].SetValue(count, Value::UINTEGER(file_stat.st_gid));
#endif

		// atime - convert to microseconds since epoch
		output.data[col_idx++].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_atime)));

		// mtime
		output.data[col_idx++].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_mtime)));

		// ctime
		output.data[col_idx++].SetValue(count, Value::TIMESTAMP(Timestamp::FromEpochSeconds(file_stat.st_ctime)));

		// nlink
		output.data[col_idx++].SetValue(count, Value::UBIGINT(file_stat.st_nlink));

		// ino
		output.data[col_idx++].SetValue(count, Value::UBIGINT(file_stat.st_ino));

		// dev
		output.data[col_idx++].SetValue(count, Value::UBIGINT(file_stat.st_dev));

		// is_regular_file
		bool is_regular = S_ISREG(file_stat.st_mode);
		output.data[col_idx++].SetValue(count, Value::BOOLEAN(is_regular));

		// is_directory
		bool is_dir = S_ISDIR(file_stat.st_mode);
		output.data[col_idx++].SetValue(count, Value::BOOLEAN(is_dir));

		// is_symlink
#ifdef _WIN32
		bool is_symlink = false; // Simplified for Windows
#else
		bool is_symlink = S_ISLNK(file_stat.st_mode);
#endif
		output.data[col_idx++].SetValue(count, Value::BOOLEAN(is_symlink));

		count++;
	}

	output.SetCardinality(count);
}

inline void FsqueryScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Fsquery " + name.GetString() + " 🐥");
	});
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register a scalar function
	auto fsquery_scalar_function = ScalarFunction("fsquery", {LogicalType::VARCHAR}, LogicalType::VARCHAR, FsqueryScalarFun);
	loader.RegisterFunction(fsquery_scalar_function);

	// Register the table function
	TableFunction fsquery_table("fsquery_scan", {LogicalType::VARCHAR}, FsQueryFunction, FsQueryBind, FsQueryInit);
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
