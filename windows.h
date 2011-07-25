#undef tell
#ifdef RGB
#define RGB_WAS_DEFINED
#pragma push_macro("RGB")
#endif
#include_next <windows.h>
#define tell mctell
#ifdef RGB_WAS_DEFINED
#pragma pop_macro("RGB")
#undef RGB_WAS_DEFINED
#else
#undef RGB
#endif
