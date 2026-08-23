#pragma once

// CUDA 12.x no longer exposes FLT_MAX transitively to every .cu translation
// unit. The upstream CenterPoint preprocessing kernel uses it directly.
#include <cfloat>
