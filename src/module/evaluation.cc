#include "isolate/class_handle.h"
#include "isolate/generic/read_option.h"
#include "external_copy_handle.h"
#include "evaluation.h"

using namespace v8;
namespace ivm {

/**
 * ScriptOriginHolder implementation
 */
ScriptOriginHolder::ScriptOriginHolder(MaybeLocal<Object> maybe_options, bool is_module) :
		is_module{is_module} {
	Local<Object> options;
	if (maybe_options.ToLocal(&options)) {
		filename = ReadOption<std::string>(options, "filename", filename);
		column_offset = ReadOption<int32_t>(options, "columnOffset", column_offset);
		line_offset = ReadOption<int32_t>(options, "lineOffset", line_offset);
	}
}

ScriptOriginHolder::operator ScriptOrigin() const {
	return {
		HandleCast<Local<String>>(filename), // resource_name,
		HandleCast<Local<Integer>>(line_offset), // resource_line_offset
		HandleCast<Local<Integer>>(column_offset), // resource_column_offset
		{}, // resource_is_shared_cross_origin
		{}, // script_id
		{}, // source_map_url
		{}, // resource_is_opaque
		{}, // is_wasm
		HandleCast<Local<Boolean>>(is_module)
	};
}

/**
 * CodeCompilerHolder implementation
 */
CodeCompilerHolder::CodeCompilerHolder(Local<String> code_handle, MaybeLocal<Object> maybe_options, bool is_module) :
		script_origin_holder{maybe_options, is_module},
		code_string{std::make_unique<ExternalCopyString>(code_handle)},
		produce_cached_data{ReadOption<bool>(maybe_options, "produceCachedData", {})} {
	// Read `cachedData`
	auto maybe_cached_data = ReadOption<MaybeLocal<Object>>(maybe_options, "cachedData", {});
	Local<Object> cached_data;
	if (maybe_cached_data.ToLocal(&cached_data)) {
		auto copy_handle = ClassHandle::Unwrap<ExternalCopyHandle>(cached_data);
		if (copy_handle != nullptr) {
			ExternalCopyArrayBuffer* copy_ptr = dynamic_cast<ExternalCopyArrayBuffer*>(copy_handle->GetValue().get());
			if (copy_ptr != nullptr) {
				supplied_cached_data = true;
				cached_data_in = copy_ptr->Acquire();
				cached_data_in_size = copy_ptr->Length();
			}
		}
		if (!cached_data_in) {
			throw RuntimeTypeError("`cachedData` must be an ExternalCopy to ArrayBuffer");
		}
	}
}

auto CodeCompilerHolder::GetCachedData() const -> std::unique_ptr<ScriptCompiler::CachedData> {
	if (cached_data_in) {
		return std::make_unique<ScriptCompiler::CachedData>(reinterpret_cast<const uint8_t*>(cached_data_in.get()), cached_data_in_size);
	}
	return {};
}

auto CodeCompilerHolder::GetSource() const -> std::unique_ptr<ScriptCompiler::Source> {
	return std::make_unique<ScriptCompiler::Source>(
		GetSourceString(),
		ScriptOrigin{script_origin_holder},
		GetCachedData().release()
	);
}

auto CodeCompilerHolder::GetSourceString() const -> v8::Local<v8::String> {
	if (code_string_handle.IsEmpty()) {
		code_string_handle = code_string->CopyIntoCheckHeap().As<String>();
	}
	return code_string_handle;
}

void CodeCompilerHolder::ResetSource() {
	cached_data_in.reset();
	code_string.reset();
}

void CodeCompilerHolder::SaveCachedData(ScriptCompiler::CachedData* cached_data) {
	if (cached_data != nullptr) {
		cached_data_out = std::make_shared<ExternalCopyArrayBuffer>((void*)cached_data->data, cached_data->length);
		cached_data->buffer_policy = ScriptCompiler::CachedData::BufferNotOwned;
		delete cached_data;
	}
}

void CodeCompilerHolder::WriteCompileResults(Local<Object> handle) {
	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
	if (DidSupplyCachedData()) {
		Unmaybe(handle->Set(context, v8_symbol("cachedDataRejected"), Boolean::New(isolate, cached_data_rejected)));
	}
	if (cached_data_out) {
		Unmaybe(handle->Set(context, v8_symbol("cachedData"), ClassHandle::NewInstance<ExternalCopyHandle>(std::move(cached_data_out))));
	}
}

} // namespace ivm
