/*
 * Shadow pass fragment shader — depth-only, no color output.
 *
 * The rasterizer writes depth automatically.  This shader exists only
 * because SDL_GPU requires a fragment shader for every graphics pipeline,
 * even depth-only passes.
 *
 * SPDX-License-Identifier: Zlib
 */

void main()
{
    /* No output: depth is written from SV_Position. */
}
