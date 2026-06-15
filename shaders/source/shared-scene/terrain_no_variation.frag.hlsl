/*
 * Diagnostic terrain fragment variant.
 *
 * The default terrain shader intentionally keeps the source Forge procedural
 * sin/frac surface variation. This diagnostic variant disables that term so
 * cross-target captures can isolate shader-math variance without changing
 * default rendered content.
 *
 * SPDX-License-Identifier: Zlib
 */

#define FORGE_TERRAIN_DISABLE_VARIATION 1
#include "terrain.frag.hlsl"
