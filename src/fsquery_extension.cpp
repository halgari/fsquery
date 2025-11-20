#define DUCKDB_EXTENSION_MAIN

#include "fsquery_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

inline void FsqueryScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Fsquery " + name.GetString() + " 🐥");
	});
}

inline void FsqueryOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Fsquery " + name.GetString() + ", my linked OpenSSL version is " +
		                                           OPENSSL_VERSION_TEXT);
	});
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register a scalar function
	auto fsquery_scalar_function = ScalarFunction("fsquery", {LogicalType::VARCHAR}, LogicalType::VARCHAR, FsqueryScalarFun);
	loader.RegisterFunction(fsquery_scalar_function);

	// Register another scalar function
	auto fsquery_openssl_version_scalar_function = ScalarFunction("fsquery_openssl_version", {LogicalType::VARCHAR},
	                                                            LogicalType::VARCHAR, FsqueryOpenSSLVersionScalarFun);
	loader.RegisterFunction(fsquery_openssl_version_scalar_function);
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
