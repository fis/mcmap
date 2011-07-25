#undef tell
#pragma push_macro("RGB")
#include_next <windows.h>
#define tell mctell
#pragma pop_macro("RGB")
