#import "win32.odin"

File :: type struct {
	Handle :: type win32.HANDLE
	handle: Handle
}

open :: proc(name: string) -> (File, bool) {
	using win32
	buf: [300]byte
	copy(buf[:], name as []byte)
	f := File{CreateFileA(^buf[0], FILE_GENERIC_READ, FILE_SHARE_READ, null, OPEN_EXISTING, 0, null)}
	success := f.handle != INVALID_HANDLE_VALUE as File.Handle

	return f, success
}

create :: proc(name: string) -> (File, bool) {
	using win32
	buf: [300]byte
	copy(buf[:], name as []byte)
	f := File{
		handle = CreateFileA(^buf[0], FILE_GENERIC_WRITE, FILE_SHARE_READ, null, CREATE_ALWAYS, 0, null),
	}
	success := f.handle != INVALID_HANDLE_VALUE as File.Handle
	return f, success
}

close :: proc(using f: ^File) {
	win32.CloseHandle(handle)
}

write :: proc(using f: ^File, buf: []byte) -> bool {
	bytes_written: i32
	return win32.WriteFile(handle, buf.data, buf.count as i32, ^bytes_written, null) != 0
}


File_Standard :: type enum {
	INPUT,
	OUTPUT,
	ERROR,
	COUNT,
}

__std_files := [..]File{
	File{handle = win32.GetStdHandle(win32.STD_INPUT_HANDLE)},
	File{handle = win32.GetStdHandle(win32.STD_OUTPUT_HANDLE)},
	File{handle = win32.GetStdHandle(win32.STD_ERROR_HANDLE)},
}

stdin  := ^__std_files[File_Standard.INPUT]
stdout := ^__std_files[File_Standard.OUTPUT]
stderr := ^__std_files[File_Standard.ERROR]



read_entire_file :: proc(name: string) -> (string, bool) {
	buf: [300]byte
	copy(buf[:], name as []byte)

	f, file_ok := open(name)
	if !file_ok {
		return "", false
	}
	defer close(^f)

	length: i64
	file_size_ok := win32.GetFileSizeEx(f.handle as win32.HANDLE, ^length) != 0
	if !file_size_ok {
		return "", false
	}

	data := new_slice(u8, length)
	if data.data == null {
		return "", false
	}

	single_read_length: i32
	total_read: i64

	for total_read < length {
		remaining := length - total_read
		to_read: u32
		MAX :: 1<<32-1
		if remaining <= MAX {
			to_read = remaining as u32
		} else {
			to_read = MAX
		}

		win32.ReadFile(f.handle as win32.HANDLE, ^data[total_read], to_read, ^single_read_length, null)
		if single_read_length <= 0 {
			free(data.data)
			return "", false
		}

		total_read += single_read_length as i64
	}

	return data as string, true
}



heap_alloc :: proc(size: int) -> rawptr {
	return win32.HeapAlloc(win32.GetProcessHeap(), win32.HEAP_ZERO_MEMORY, size)
}
heap_resize :: proc(ptr: rawptr, new_size: int) -> rawptr {
	return win32.HeapReAlloc(win32.GetProcessHeap(), win32.HEAP_ZERO_MEMORY, ptr, new_size)
}
heap_free :: proc(ptr: rawptr) {
	win32.HeapFree(win32.GetProcessHeap(), 0, ptr)
}


exit :: proc(code: int) {
	win32.ExitProcess(code as u32)
}



current_thread_id :: proc() -> int {
	GetCurrentThreadId :: proc() -> u32 #foreign #dll_import
	return GetCurrentThreadId() as int
}
