#ifndef LVPA_COMPILE_CONFIG
#define LVPA_COMPILE_CONFIG

// choose a namespace name, or comment out this define to disable namespacing (not recommended)
#define LVPA_NAMESPACE lvpa

// if not configuring via CMake or setting these externally,
// use these to enable a set of compression algorithms to use.
//#define LVPA_SUPPORT_ZLIB
//#define LVPA_SUPPORT_LZMA
//#define LVPA_SUPPORT_LZO
//#define LVPA_SUPPORT_LZF
//#define LVPA_SUPPORT_LZHAM


// ------ End of config ------

#if !(defined(LVPA_SUPPORT_ZLIB) || defined(LVPA_SUPPORT_LZMA) || defined(LVPA_SUPPORT_LZO) \
    || defined(LVPA_SUPPORT_LZF) || defined(LVPA_SUPPORT_LZHAM))
#error No compression support enabled; if this is the intent, comment out this warning and go ahead
#endif


#ifdef LVPA_NAMESPACE
#  define LVPA_NAMESPACE_START namespace LVPA_NAMESPACE {
#  define LVPA_NAMESPACE_END }
#  define LVPA_NAMESPACE_IMPL LVPA_NAMESPACE::
   namespace LVPA_NAMESPACE {} // predeclare namespace to make compilers happy
#else
#  define LVPA_NAMESPACE_START
#  define LVPA_NAMESPACE_END
#  define LVPA_NAMESPACE_IMPL
#endif

#define BYTEBUFFER_NO_EXCEPTIONS


#endif
