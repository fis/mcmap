/* This is a huge hack to get around the fact that libraries include
   windows.h, and windows.h defines some short identifiers that we
   use, and I'm too stubborn to change those identifiers.
  
   I wonder if my pain will ever end. */
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
