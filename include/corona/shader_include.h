#pragma once

// Lightweight GLSL/HLSL include macro bridge used by Helicon-generated headers.
// Keep this header macro-only to avoid pulling heavy eDSL runtime headers into
// public engine headers (which can trigger ODR/link conflicts).

#ifndef HELICON_STRINGIZE_
#define HELICON_STRINGIZE_(X) #X
#endif

#ifndef HLSL
#define HLSL(path) HELICON_STRINGIZE_(path.hpp)
#endif

#ifndef GLSL
#define GLSL(path) HELICON_STRINGIZE_(path.hpp)
#endif
