enum TargetOsKind {
	TargetOs_Invalid,

	TargetOs_windows,
	TargetOs_osx,
	TargetOs_linux,
	TargetOs_essence,

	TargetOs_COUNT,
};

enum TargetArchKind {
	TargetArch_Invalid,

	TargetArch_amd64,
	TargetArch_x86,

	TargetArch_COUNT,
};

String target_os_names[TargetOs_COUNT] = {
	str_lit(""),
	str_lit("windows"),
	str_lit("osx"),
	str_lit("linux"),
	str_lit("essence"),
};

String target_arch_names[TargetArch_COUNT] = {
	str_lit(""),
	str_lit("amd64"),
	str_lit("x86"),
};


String const ODIN_VERSION = str_lit("0.9.0");
String cross_compile_target = str_lit("");
String cross_compile_lib_dir = str_lit("");


// This stores the information for the specify architecture of this build
struct BuildContext {
	// Constants
	String ODIN_OS;      // target operating system
	String ODIN_ARCH;    // target architecture
	String ODIN_ENDIAN;  // target endian
	String ODIN_VENDOR;  // compiler vendor
	String ODIN_VERSION; // compiler version
	String ODIN_ROOT;    // Odin ROOT
	bool   ODIN_DEBUG;   // Odin in debug mode

	// In bytes
	i64    word_size; // Size of a pointer, must be >= 4
	i64    max_align; // max alignment, must be >= 1 (and typically >= word_size)

	String command;

	TargetOsKind   target_os;
	TargetArchKind target_arch;

	String out_filepath;
	String resource_filepath;
	bool   has_resource;
	String opt_flags;
	String llc_flags;
	String link_flags;
	bool   is_dll;
	bool   generate_docs;
	i32    optimization_level;
	bool   show_timings;
	bool   keep_temp_files;
	bool   no_bounds_check;
	bool   no_output_files;

	gbAffinity affinity;
	isize      thread_count;
};


gb_global BuildContext build_context = {0};


TargetOsKind get_target_os_from_string(String str) {
	for (isize i = 0; i < TargetOs_COUNT; i++) {
		if (str_eq_ignore_case(target_os_names[i], str)) {
			return cast(TargetOsKind)i;
		}
	}
	return TargetOs_Invalid;
}

TargetArchKind get_target_arch_from_string(String str) {
	for (isize i = 0; i < TargetArch_COUNT; i++) {
		if (str_eq_ignore_case(target_arch_names[i], str)) {
			return cast(TargetArchKind)i;
		}
	}
	return TargetArch_Invalid;
}

bool is_excluded_target_filename(String name) {
	String const ext = str_lit(".odin");
	GB_ASSERT(string_ends_with(name, ext));
	name = substring(name, 0, name.len-ext.len);

	String str1 = {};
	String str2 = {};
	isize n = 0;

	str1 = name;
	n = str1.len;
	for (isize i = str1.len-1; i >= 0 && str1[i] != '_'; i--) {
		n -= 1;
	}
	str1 = substring(str1, n, str1.len);

	str2 = str1;
	n = str2.len;
	for (isize i = str2.len-1; i >= 0 && str2[i] != '_'; i--) {
		n -= 1;
	}
	str2 = substring(str2, n, str2.len);

	if (str1 == name) {
		return false;
	}



	TargetOsKind   os1   = get_target_os_from_string(str1);
	TargetArchKind arch1 = get_target_arch_from_string(str1);
	TargetOsKind   os2   = get_target_os_from_string(str2);
	TargetArchKind arch2 = get_target_arch_from_string(str2);

	if (arch1 != TargetArch_Invalid && os2 != TargetOs_Invalid) {
		return arch1 != build_context.target_arch || os2 != build_context.target_os;
	} else if (arch1 != TargetArch_Invalid && os1 != TargetOs_Invalid) {
		return arch2 != build_context.target_arch || os1 != build_context.target_os;
	} else if (os1 != TargetOs_Invalid) {
		return os1 != build_context.target_os;
	} else if (arch1 != TargetArch_Invalid) {
		return arch1 != build_context.target_arch;
	}

	return false;
}



struct LibraryCollections {
	String name;
	String path;
};

gb_global Array<LibraryCollections> library_collections = {0};

void add_library_collection(String name, String path) {
	// TODO(bill): Check the path is valid and a directory
	LibraryCollections lc = {name, string_trim_whitespace(path)};
	array_add(&library_collections, lc);
}

bool find_library_collection_path(String name, String *path) {
	for_array(i, library_collections) {
		if (library_collections[i].name == name) {
			if (path) *path = library_collections[i].path;
			return true;
		}
	}
	return false;
}


// TODO(bill): OS dependent versions for the BuildContext
// join_path
// is_dir
// is_file
// is_abs_path
// has_subdir

String const WIN32_SEPARATOR_STRING = {cast(u8 *)"\\", 1};
String const NIX_SEPARATOR_STRING   = {cast(u8 *)"/",  1};

#if defined(GB_SYSTEM_WINDOWS)
String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	wchar_t *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<wchar_t>(heap_allocator(), 300);

	len = 0;
	for (;;) {
		len = GetModuleFileNameW(nullptr, &path_buf[0], cast(int)path_buf.count);
		if (len == 0) {
			return make_string(nullptr, 0);
		}
		if (len < path_buf.count) {
			break;
		}
		array_resize(&path_buf, 2*path_buf.count + 300);
	}
	len += 1; // NOTE(bill): It needs an extra 1 for some reason

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, wchar_t, len+1);

	GetModuleFileNameW(nullptr, text, cast(int)len);
	path = string16_to_string(heap_allocator(), make_string16(text, len));

	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;


	array_free(&path_buf);

	return path;
}

#elif defined(GB_SYSTEM_OSX)

#include <mach-o/dyld.h>

String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	u8 *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<char>(heap_allocator(), 300);

	len = 0;
	for (;;) {
		u32 sz = path_buf.count;
		int res = _NSGetExecutablePath(&path_buf[0], &sz);
		if(res == 0) {
			len = sz;
			break;
		} else {
			array_resize(&path_buf, sz + 1);
		}
	}

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, u8, len + 1);
	gb_memmove(text, &path_buf[0], len);

	path = make_string(text, len);
	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;


	// array_free(&path_buf);

	return path;
}
#else

// NOTE: Linux / Unix is unfinished and not tested very well.
#include <sys/stat.h>

String odin_root_dir(void) {
	String path = global_module_path;
	isize len, i;
	gbTempArenaMemory tmp;
	u8 *text;

	if (global_module_path_set) {
		return global_module_path;
	}

	auto path_buf = array_make<char>(heap_allocator(), 300);
	defer (array_free(&path_buf));

	len = 0;
	for (;;) {
		// This is not a 100% reliable system, but for the purposes
		// of this compiler, it should be _good enough_.
		// That said, there's no solid 100% method on Linux to get the program's
		// path without checking this link. Sorry.
		len = readlink("/proc/self/exe", &path_buf[0], path_buf.count);
		if(len == 0) {
			return make_string(nullptr, 0);
		}
		if (len < path_buf.count) {
			break;
		}
		array_resize(&path_buf, 2*path_buf.count + 300);
	}

	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));

	text = gb_alloc_array(string_buffer_allocator, u8, len + 1);

	gb_memmove(text, &path_buf[0], len);

	path = make_string(text, len);
	for (i = path.len-1; i >= 0; i--) {
		u8 c = path[i];
		if (c == '/' || c == '\\') {
			break;
		}
		path.len--;
	}

	global_module_path = path;
	global_module_path_set = true;

	return path;
}
#endif


#if defined(GB_SYSTEM_WINDOWS)
String path_to_fullpath(gbAllocator a, String s) {
	String result = {};
	gb_mutex_lock(&string_buffer_mutex);
	defer (gb_mutex_unlock(&string_buffer_mutex));

	gbTempArenaMemory tmp = gb_temp_arena_memory_begin(&string_buffer_arena);
	defer (gb_temp_arena_memory_end(tmp));
	String16 string16 = string_to_string16(string_buffer_allocator, s);

	DWORD len = GetFullPathNameW(&string16[0], 0, nullptr, nullptr);
	if (len != 0) {
		wchar_t *text = gb_alloc_array(string_buffer_allocator, wchar_t, len+1);
		GetFullPathNameW(&string16[0], len, text, nullptr);
		text[len] = 0;
		result = string16_to_string(a, make_string16(text, len));
		result = string_trim_whitespace(result);
	}

	return result;
}
#elif defined(GB_SYSTEM_OSX) || defined(GB_SYSTEM_UNIX)
String path_to_fullpath(gbAllocator a, String s) {
	char *p;
	gb_mutex_lock(&string_buffer_mutex);
	p = realpath(cast(char *)s.text, 0);
	gb_mutex_unlock(&string_buffer_mutex);
	if(p == nullptr) return String{};
	return make_string_c(p);
}
#else
#error Implement system
#endif


String get_fullpath_relative(gbAllocator a, String base_dir, String path) {
	u8 *str = gb_alloc_array(heap_allocator(), u8, base_dir.len+1+path.len+1);
	defer (gb_free(heap_allocator(), str));

	isize i = 0;
	gb_memmove(str+i, base_dir.text, base_dir.len); i += base_dir.len;
	gb_memmove(str+i, "/", 1);                      i += 1;
	gb_memmove(str+i, path.text,     path.len);     i += path.len;
	str[i] = 0;

	String res = make_string(str, i);
	res = string_trim_whitespace(res);
	return path_to_fullpath(a, res);
}


String get_fullpath_core(gbAllocator a, String path) {
	String module_dir = odin_root_dir();

	String core = str_lit("core/");

	isize str_len = module_dir.len + core.len + path.len;
	u8 *str = gb_alloc_array(heap_allocator(), u8, str_len+1);
	defer (gb_free(heap_allocator(), str));

	isize i = 0;
	gb_memmove(str+i, module_dir.text, module_dir.len); i += module_dir.len;
	gb_memmove(str+i, core.text, core.len);             i += core.len;
	gb_memmove(str+i, path.text, path.len);             i += path.len;
	str[i] = 0;

	String res = make_string(str, i);
	res = string_trim_whitespace(res);
	return path_to_fullpath(a, res);
}



void init_build_context(void) {
	BuildContext *bc = &build_context;

	gb_affinity_init(&bc->affinity);
	if (bc->thread_count == 0) {
		bc->thread_count = gb_max(bc->affinity.thread_count, 1);
	}

	bc->ODIN_VENDOR  = str_lit("odin");
	bc->ODIN_VERSION = ODIN_VERSION;
	bc->ODIN_ROOT    = odin_root_dir();

#if defined(GB_SYSTEM_WINDOWS)
	bc->ODIN_OS   = str_lit("windows");
	bc->target_os = TargetOs_windows;
#elif defined(GB_SYSTEM_OSX)
	bc->ODIN_OS   = str_lit("osx");
	bc->target_os = TargetOs_osx;
#else
	bc->ODIN_OS   = str_lit("linux");
	bc->target_os = TargetOs_linux;
#endif

	if (cross_compile_target.len) {
		bc->ODIN_OS = cross_compile_target;
	}

#if defined(GB_ARCH_64_BIT)
	bc->ODIN_ARCH = str_lit("amd64");
	bc->target_arch = TargetArch_amd64;
#else
	bc->ODIN_ARCH = str_lit("x86");
	bc->target_arch = TargetArch_x86;
#endif

	{
		u16 x = 1;
		bool big = !*cast(u8 *)&x;
		bc->ODIN_ENDIAN = big ? str_lit("big") : str_lit("little");
	}


	// NOTE(zangent): The linker flags to set the build architecture are different
	// across OSs. It doesn't make sense to allocate extra data on the heap
	// here, so I just #defined the linker flags to keep things concise.
	#if defined(GB_SYSTEM_WINDOWS)
		#define LINK_FLAG_X64 "/machine:x64"
		#define LINK_FLAG_X86 "/machine:x86"

	#elif defined(GB_SYSTEM_OSX)
		// NOTE(zangent): MacOS systems are x64 only, so ld doesn't have
		// an architecture option. All compilation done on MacOS must be x64.
		GB_ASSERT(bc->ODIN_ARCH == "amd64");

		#define LINK_FLAG_X64 ""
		#define LINK_FLAG_X86 ""
	#else
		// Linux, but also BSDs and the like.
		// NOTE(zangent): When clang is swapped out with ld as the linker,
		//   the commented flags here should be used. Until then, we'll have
		//   to use alternative build flags made for clang.
		/*
			#define LINK_FLAG_X64 "-m elf_x86_64"
			#define LINK_FLAG_X86 "-m elf_i386"
		*/
		#define LINK_FLAG_X64 "-arch x86-64"
		#define LINK_FLAG_X86 "-arch x86"
	#endif

	gbString llc_flags = gb_string_make_reserve(heap_allocator(), 64);
	gbString link_flags = gb_string_make_reserve(heap_allocator(), 64);
	if (bc->ODIN_DEBUG) {
		llc_flags = gb_string_appendc(llc_flags, "-debug-compile ");
	}

	if (bc->ODIN_ARCH == "amd64") {
		bc->word_size = 8;
		bc->max_align = 16;

		llc_flags = gb_string_appendc(llc_flags, "-march=x86-64 ");

		if (str_eq_ignore_case(cross_compile_target, str_lit("Essence"))) {
			bc->link_flags = str_lit(" ");
		} else {
			bc->link_flags = str_lit(LINK_FLAG_X64 " ");
		}
	} else if (bc->ODIN_ARCH == "x86") {
		bc->word_size = 4;
		bc->max_align = 8;
		llc_flags = gb_string_appendc(llc_flags, "-march=x86 ");
		bc->link_flags = str_lit(LINK_FLAG_X86 " ");
	} else {
		gb_printf_err("This current architecture is not supported");
		gb_exit(1);
	}

	bc->llc_flags = make_string_c(llc_flags);

	bc->optimization_level = gb_clamp(bc->optimization_level, 0, 3);

	gbString opt_flags = gb_string_make_reserve(heap_allocator(), 16);
	if (bc->optimization_level != 0) {
		opt_flags = gb_string_append_fmt(opt_flags, "-O%d", bc->optimization_level);
	}
	bc->opt_flags = make_string_c(opt_flags);


	#undef LINK_FLAG_X64
	#undef LINK_FLAG_X86
}
