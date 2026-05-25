#if defined(_WIN32)

extern "C" {

// CUDA's Windows import libraries are built with MSVC and expect a few
// support symbols that llvm-mingw doesn't provide under those exact names.
int _fltused = 0;
unsigned long long __security_cookie = 0x2B992DDFA232ULL;

void __cdecl __security_check_cookie(unsigned long long) {
}

void __cdecl __GSHandlerCheck() {
}

}

#endif
