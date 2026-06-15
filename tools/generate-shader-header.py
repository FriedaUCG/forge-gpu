#!/usr/bin/env python3

import argparse
import re
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path


SHADERS = (
    ("lesson02_triangle_vert", "02-first-triangle/triangle.vert.hlsl", "vertex", "main"),
    ("lesson02_triangle_frag", "02-first-triangle/triangle.frag.hlsl", "fragment", "main"),
    ("lesson03_triangle_vert", "03-uniforms-and-motion/triangle.vert.hlsl", "vertex", "main"),
    ("lesson03_triangle_frag", "03-uniforms-and-motion/triangle.frag.hlsl", "fragment", "main"),
    ("lesson04_quad_vert", "04-textures-and-samplers/quad.vert.hlsl", "vertex", "main"),
    ("lesson04_quad_frag", "04-textures-and-samplers/quad.frag.hlsl", "fragment", "main"),
    ("lesson05_quad_vert", "05-mipmaps/quad.vert.hlsl", "vertex", "main"),
    ("lesson05_quad_frag", "05-mipmaps/quad.frag.hlsl", "fragment", "main"),
    ("lesson06_cube_vert", "06-depth-and-3d/cube.vert.hlsl", "vertex", "main"),
    ("lesson06_cube_frag", "06-depth-and-3d/cube.frag.hlsl", "fragment", "main"),
    ("lesson07_scene_vert", "07-camera-and-input/scene.vert.hlsl", "vertex", "main"),
    ("lesson07_scene_frag", "07-camera-and-input/scene.frag.hlsl", "fragment", "main"),
    ("lesson08_mesh_vert", "08-mesh-loading/mesh.vert.hlsl", "vertex", "main"),
    ("lesson08_mesh_frag", "08-mesh-loading/mesh.frag.hlsl", "fragment", "main"),
    ("lesson09_scene_vert", "09-scene-loading/scene.vert.hlsl", "vertex", "main"),
    ("lesson09_scene_frag", "09-scene-loading/scene.frag.hlsl", "fragment", "main"),
    ("lesson10_lighting_vert", "10-basic-lighting/lighting.vert.hlsl", "vertex", "main"),
    ("lesson10_lighting_frag", "10-basic-lighting/lighting.frag.hlsl", "fragment", "main"),
    ("lesson11_plasma_comp", "11-compute-shaders/plasma.comp.hlsl", "compute", "main"),
    ("lesson11_fullscreen_vert", "11-compute-shaders/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson11_fullscreen_frag", "11-compute-shaders/fullscreen.frag.hlsl", "fragment", "main"),
    ("lesson12_grid_vert", "12-shader-grid/grid.vert.hlsl", "vertex", "main"),
    ("lesson12_grid_frag", "12-shader-grid/grid.frag.hlsl", "fragment", "main"),
    ("lesson12_lighting_vert", "12-shader-grid/lighting.vert.hlsl", "vertex", "main"),
    ("lesson12_lighting_frag", "12-shader-grid/lighting.frag.hlsl", "fragment", "main"),
    ("lesson13_grid_vert", "13-instanced-rendering/grid.vert.hlsl", "vertex", "main"),
    ("lesson13_grid_frag", "13-instanced-rendering/grid.frag.hlsl", "fragment", "main"),
    ("lesson13_instanced_vert", "13-instanced-rendering/instanced.vert.hlsl", "vertex", "main"),
    ("lesson13_instanced_frag", "13-instanced-rendering/instanced.frag.hlsl", "fragment", "main"),
    ("lesson14_skybox_vert", "14-environment-mapping/skybox.vert.hlsl", "vertex", "main"),
    ("lesson14_skybox_frag", "14-environment-mapping/skybox.frag.hlsl", "fragment", "main"),
    ("lesson14_shuttle_vert", "14-environment-mapping/shuttle.vert.hlsl", "vertex", "main"),
    ("lesson14_shuttle_frag", "14-environment-mapping/shuttle.frag.hlsl", "fragment", "main"),
    ("lesson15_shadow_vert", "15-cascaded-shadow-maps/shadow.vert.hlsl", "vertex", "main"),
    ("lesson15_shadow_frag", "15-cascaded-shadow-maps/shadow.frag.hlsl", "fragment", "main"),
    ("lesson15_scene_vert", "15-cascaded-shadow-maps/scene.vert.hlsl", "vertex", "main"),
    ("lesson15_scene_frag", "15-cascaded-shadow-maps/scene.frag.hlsl", "fragment", "main"),
    ("lesson15_grid_vert", "15-cascaded-shadow-maps/grid.vert.hlsl", "vertex", "main"),
    ("lesson15_grid_frag", "15-cascaded-shadow-maps/grid.frag.hlsl", "fragment", "main"),
    ("lesson15_debug_quad_vert", "15-cascaded-shadow-maps/debug_quad.vert.hlsl", "vertex", "main"),
    ("lesson15_debug_quad_frag", "15-cascaded-shadow-maps/debug_quad.frag.hlsl", "fragment", "main"),
    ("lesson16_scene_vert", "16-blending/scene.vert.hlsl", "vertex", "main"),
    ("lesson16_scene_frag", "16-blending/scene.frag.hlsl", "fragment", "main"),
    ("lesson16_alpha_test_frag", "16-blending/alpha_test.frag.hlsl", "fragment", "main"),
    ("lesson16_grid_vert", "16-blending/grid.vert.hlsl", "vertex", "main"),
    ("lesson16_grid_frag", "16-blending/grid.frag.hlsl", "fragment", "main"),
    ("lesson17_scene_vert", "17-normal-maps/scene.vert.hlsl", "vertex", "main"),
    ("lesson17_scene_frag", "17-normal-maps/scene.frag.hlsl", "fragment", "main"),
    ("lesson17_grid_vert", "17-normal-maps/grid.vert.hlsl", "vertex", "main"),
    ("lesson17_grid_frag", "17-normal-maps/grid.frag.hlsl", "fragment", "main"),
    ("lesson18_material_vert", "18-blinn-phong-materials/material.vert.hlsl", "vertex", "main"),
    ("lesson18_material_frag", "18-blinn-phong-materials/material.frag.hlsl", "fragment", "main"),
    ("lesson18_grid_vert", "18-blinn-phong-materials/grid.vert.hlsl", "vertex", "main"),
    ("lesson18_grid_frag", "18-blinn-phong-materials/grid.frag.hlsl", "fragment", "main"),
    ("lesson19_debug_vert", "19-debug-lines/debug.vert.hlsl", "vertex", "main"),
    ("lesson19_debug_frag", "19-debug-lines/debug.frag.hlsl", "fragment", "main"),
    ("lesson20_fog_vert", "20-linear-fog/fog.vert.hlsl", "vertex", "main"),
    ("lesson20_fog_frag", "20-linear-fog/fog.frag.hlsl", "fragment", "main"),
    ("lesson20_grid_fog_vert", "20-linear-fog/grid_fog.vert.hlsl", "vertex", "main"),
    ("lesson20_grid_fog_frag", "20-linear-fog/grid_fog.frag.hlsl", "fragment", "main"),
    ("lesson21_shadow_vert", "21-hdr-tone-mapping/shadow.vert.hlsl", "vertex", "main"),
    ("lesson21_shadow_frag", "21-hdr-tone-mapping/shadow.frag.hlsl", "fragment", "main"),
    ("lesson21_scene_vert", "21-hdr-tone-mapping/scene.vert.hlsl", "vertex", "main"),
    ("lesson21_scene_frag", "21-hdr-tone-mapping/scene.frag.hlsl", "fragment", "main"),
    ("lesson21_grid_vert", "21-hdr-tone-mapping/grid.vert.hlsl", "vertex", "main"),
    ("lesson21_grid_frag", "21-hdr-tone-mapping/grid.frag.hlsl", "fragment", "main"),
    ("lesson21_tonemap_vert", "21-hdr-tone-mapping/tonemap.vert.hlsl", "vertex", "main"),
    ("lesson21_tonemap_frag", "21-hdr-tone-mapping/tonemap.frag.hlsl", "fragment", "main"),
    ("lesson22_scene_vert", "22-bloom/scene.vert.hlsl", "vertex", "main"),
    ("lesson22_scene_frag", "22-bloom/scene.frag.hlsl", "fragment", "main"),
    ("lesson22_grid_vert", "22-bloom/grid.vert.hlsl", "vertex", "main"),
    ("lesson22_grid_frag", "22-bloom/grid.frag.hlsl", "fragment", "main"),
    ("lesson22_emissive_frag", "22-bloom/emissive.frag.hlsl", "fragment", "main"),
    ("lesson22_fullscreen_vert", "22-bloom/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson22_bloom_downsample_frag", "22-bloom/bloom_downsample.frag.hlsl", "fragment", "main"),
    ("lesson22_bloom_upsample_frag", "22-bloom/bloom_upsample.frag.hlsl", "fragment", "main"),
    ("lesson22_tonemap_frag", "22-bloom/tonemap.frag.hlsl", "fragment", "main"),
    ("lesson23_scene_vert", "23-point-light-shadows/scene.vert.hlsl", "vertex", "main"),
    ("lesson23_scene_frag", "23-point-light-shadows/scene.frag.hlsl", "fragment", "main"),
    ("lesson23_grid_vert", "23-point-light-shadows/grid.vert.hlsl", "vertex", "main"),
    ("lesson23_grid_frag", "23-point-light-shadows/grid.frag.hlsl", "fragment", "main"),
    ("lesson23_shadow_vert", "23-point-light-shadows/shadow.vert.hlsl", "vertex", "main"),
    ("lesson23_shadow_frag", "23-point-light-shadows/shadow.frag.hlsl", "fragment", "main"),
    ("lesson23_emissive_frag", "23-point-light-shadows/emissive.frag.hlsl", "fragment", "main"),
    ("lesson23_fullscreen_vert", "23-point-light-shadows/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson23_bloom_downsample_frag", "23-point-light-shadows/bloom_downsample.frag.hlsl", "fragment", "main"),
    ("lesson23_bloom_upsample_frag", "23-point-light-shadows/bloom_upsample.frag.hlsl", "fragment", "main"),
    ("lesson23_tonemap_frag", "23-point-light-shadows/tonemap.frag.hlsl", "fragment", "main"),
    ("lesson24_scene_vert", "24-gobo-spotlight/scene.vert.hlsl", "vertex", "main"),
    ("lesson24_scene_frag", "24-gobo-spotlight/scene.frag.hlsl", "fragment", "main"),
    ("lesson24_grid_vert", "24-gobo-spotlight/grid.vert.hlsl", "vertex", "main"),
    ("lesson24_grid_frag", "24-gobo-spotlight/grid.frag.hlsl", "fragment", "main"),
    ("lesson24_shadow_vert", "24-gobo-spotlight/shadow.vert.hlsl", "vertex", "main"),
    ("lesson24_shadow_frag", "24-gobo-spotlight/shadow.frag.hlsl", "fragment", "main"),
    ("lesson24_tonemap_vert", "24-gobo-spotlight/tonemap.vert.hlsl", "vertex", "main"),
    ("lesson24_tonemap_frag", "24-gobo-spotlight/tonemap.frag.hlsl", "fragment", "main"),
    ("lesson24_bloom_downsample_frag", "24-gobo-spotlight/bloom_downsample.frag.hlsl", "fragment", "main"),
    ("lesson24_bloom_upsample_frag", "24-gobo-spotlight/bloom_upsample.frag.hlsl", "fragment", "main"),
    ("lesson25_noise_vert", "25-shader-noise/noise.vert.hlsl", "vertex", "main"),
    ("lesson25_noise_frag", "25-shader-noise/noise.frag.hlsl", "fragment", "main"),
    ("lesson26_sky_vert", "26-procedural-sky/sky.vert.hlsl", "vertex", "main"),
    ("lesson26_sky_frag", "26-procedural-sky/sky.frag.hlsl", "fragment", "main"),
    ("lesson26_transmittance_lut_comp", "26-procedural-sky/transmittance_lut.comp.hlsl", "compute", "main"),
    ("lesson26_multiscatter_lut_comp", "26-procedural-sky/multiscatter_lut.comp.hlsl", "compute", "main"),
    ("lesson26_fullscreen_vert", "26-procedural-sky/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson26_bloom_downsample_frag", "26-procedural-sky/bloom_downsample.frag.hlsl", "fragment", "main"),
    ("lesson26_bloom_upsample_frag", "26-procedural-sky/bloom_upsample.frag.hlsl", "fragment", "main"),
    ("lesson26_tonemap_frag", "26-procedural-sky/tonemap.frag.hlsl", "fragment", "main"),
    ("lesson27_shadow_vert", "27-ssao/shadow.vert.hlsl", "vertex", "main"),
    ("lesson27_shadow_frag", "27-ssao/shadow.frag.hlsl", "fragment", "main"),
    ("lesson27_scene_vert", "27-ssao/scene.vert.hlsl", "vertex", "main"),
    ("lesson27_scene_frag", "27-ssao/scene.frag.hlsl", "fragment", "main"),
    ("lesson27_grid_vert", "27-ssao/grid.vert.hlsl", "vertex", "main"),
    ("lesson27_grid_frag", "27-ssao/grid.frag.hlsl", "fragment", "main"),
    ("lesson27_fullscreen_vert", "27-ssao/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson27_ssao_frag", "27-ssao/ssao.frag.hlsl", "fragment", "main"),
    ("lesson27_blur_frag", "27-ssao/blur.frag.hlsl", "fragment", "main"),
    ("lesson27_composite_frag", "27-ssao/composite.frag.hlsl", "fragment", "main"),
    ("lesson28_ui_vert", "28-ui-rendering/ui.vert.hlsl", "vertex", "main"),
    ("lesson28_ui_frag", "28-ui-rendering/ui.frag.hlsl", "fragment", "main"),
    ("lesson29_shadow_vert", "29-screen-space-reflections/shadow.vert.hlsl", "vertex", "main"),
    ("lesson29_shadow_frag", "29-screen-space-reflections/shadow.frag.hlsl", "fragment", "main"),
    ("lesson29_scene_vert", "29-screen-space-reflections/scene.vert.hlsl", "vertex", "main"),
    ("lesson29_scene_frag", "29-screen-space-reflections/scene.frag.hlsl", "fragment", "main"),
    ("lesson29_grid_vert", "29-screen-space-reflections/grid.vert.hlsl", "vertex", "main"),
    ("lesson29_grid_frag", "29-screen-space-reflections/grid.frag.hlsl", "fragment", "main"),
    ("lesson29_fullscreen_vert", "29-screen-space-reflections/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson29_ssr_frag", "29-screen-space-reflections/ssr.frag.hlsl", "fragment", "main"),
    ("lesson29_composite_frag", "29-screen-space-reflections/composite.frag.hlsl", "fragment", "main"),
    ("lesson30_shadow_vert", "30-planar-reflections/shadow.vert.hlsl", "vertex", "main"),
    ("lesson30_shadow_frag", "30-planar-reflections/shadow.frag.hlsl", "fragment", "main"),
    ("lesson30_scene_vert", "30-planar-reflections/scene.vert.hlsl", "vertex", "main"),
    ("lesson30_scene_frag", "30-planar-reflections/scene.frag.hlsl", "fragment", "main"),
    ("lesson30_skybox_vert", "30-planar-reflections/skybox.vert.hlsl", "vertex", "main"),
    ("lesson30_skybox_frag", "30-planar-reflections/skybox.frag.hlsl", "fragment", "main"),
    ("lesson30_water_vert", "30-planar-reflections/water.vert.hlsl", "vertex", "main"),
    ("lesson30_water_frag", "30-planar-reflections/water.frag.hlsl", "fragment", "main"),
    ("lesson31_shadow_vert", "31-transform-animations/shadow.vert.hlsl", "vertex", "main"),
    ("lesson31_shadow_frag", "31-transform-animations/shadow.frag.hlsl", "fragment", "main"),
    ("lesson31_scene_vert", "31-transform-animations/scene.vert.hlsl", "vertex", "main"),
    ("lesson31_scene_frag", "31-transform-animations/scene.frag.hlsl", "fragment", "main"),
    ("lesson31_skybox_vert", "31-transform-animations/skybox.vert.hlsl", "vertex", "main"),
    ("lesson31_skybox_frag", "31-transform-animations/skybox.frag.hlsl", "fragment", "main"),
    ("lesson32_skin_vert", "32-skinning-animations/skin.vert.hlsl", "vertex", "main"),
    ("lesson32_skin_frag", "32-skinning-animations/skin.frag.hlsl", "fragment", "main"),
    ("lesson32_shadow_skin_vert", "32-skinning-animations/shadow_skin.vert.hlsl", "vertex", "main"),
    ("lesson32_shadow_frag", "32-skinning-animations/shadow.frag.hlsl", "fragment", "main"),
    ("lesson32_grid_vert", "32-skinning-animations/grid.vert.hlsl", "vertex", "main"),
    ("lesson32_grid_frag", "32-skinning-animations/grid.frag.hlsl", "fragment", "main"),
    ("lesson33_pulled_vert", "33-vertex-pulling/pulled.vert.hlsl", "vertex", "main"),
    ("lesson33_pulled_frag", "33-vertex-pulling/pulled.frag.hlsl", "fragment", "main"),
    ("lesson33_shadow_pulled_vert", "33-vertex-pulling/shadow_pulled.vert.hlsl", "vertex", "main"),
    ("lesson33_shadow_frag", "33-vertex-pulling/shadow.frag.hlsl", "fragment", "main"),
    ("lesson33_grid_vert", "33-vertex-pulling/grid.vert.hlsl", "vertex", "main"),
    ("lesson33_grid_frag", "33-vertex-pulling/grid.frag.hlsl", "fragment", "main"),
    ("lesson34_scene_vert", "34-stencil-testing/scene.vert.hlsl", "vertex", "main"),
    ("lesson34_scene_frag", "34-stencil-testing/scene.frag.hlsl", "fragment", "main"),
    ("lesson34_shadow_vert", "34-stencil-testing/shadow.vert.hlsl", "vertex", "main"),
    ("lesson34_shadow_frag", "34-stencil-testing/shadow.frag.hlsl", "fragment", "main"),
    ("lesson34_grid_vert", "34-stencil-testing/grid.vert.hlsl", "vertex", "main"),
    ("lesson34_grid_frag", "34-stencil-testing/grid.frag.hlsl", "fragment", "main"),
    ("lesson34_outline_frag", "34-stencil-testing/outline.frag.hlsl", "fragment", "main"),
    ("lesson35_scene_vert", "35-decals/scene.vert.hlsl", "vertex", "main"),
    ("lesson35_scene_frag", "35-decals/scene.frag.hlsl", "fragment", "main"),
    ("lesson35_shadow_vert", "35-decals/shadow.vert.hlsl", "vertex", "main"),
    ("lesson35_shadow_frag", "35-decals/shadow.frag.hlsl", "fragment", "main"),
    ("lesson35_grid_vert", "35-decals/grid.vert.hlsl", "vertex", "main"),
    ("lesson35_grid_frag", "35-decals/grid.frag.hlsl", "fragment", "main"),
    ("lesson35_decal_vert", "35-decals/decal.vert.hlsl", "vertex", "main"),
    ("lesson35_decal_frag", "35-decals/decal.frag.hlsl", "fragment", "main"),
    ("lesson36_scene_vert", "36-edge-detection/scene.vert.hlsl", "vertex", "main"),
    ("lesson36_scene_frag", "36-edge-detection/scene.frag.hlsl", "fragment", "main"),
    ("lesson36_shadow_vert", "36-edge-detection/shadow.vert.hlsl", "vertex", "main"),
    ("lesson36_shadow_frag", "36-edge-detection/shadow.frag.hlsl", "fragment", "main"),
    ("lesson36_grid_vert", "36-edge-detection/grid.vert.hlsl", "vertex", "main"),
    ("lesson36_grid_frag", "36-edge-detection/grid.frag.hlsl", "fragment", "main"),
    ("lesson36_fullscreen_vert", "36-edge-detection/fullscreen.vert.hlsl", "vertex", "main"),
    ("lesson36_edge_detect_frag", "36-edge-detection/edge_detect.frag.hlsl", "fragment", "main"),
    ("lesson36_xray_mark_vert", "36-edge-detection/xray_mark.vert.hlsl", "vertex", "main"),
    ("lesson36_xray_mark_frag", "36-edge-detection/xray_mark.frag.hlsl", "fragment", "main"),
    ("lesson36_ghost_vert", "36-edge-detection/ghost.vert.hlsl", "vertex", "main"),
    ("lesson36_ghost_frag", "36-edge-detection/ghost.frag.hlsl", "fragment", "main"),
    ("lesson37_scene_vert", "37-3d-picking/scene.vert.hlsl", "vertex", "main"),
    ("lesson37_scene_frag", "37-3d-picking/scene.frag.hlsl", "fragment", "main"),
    ("lesson37_shadow_vert", "37-3d-picking/shadow.vert.hlsl", "vertex", "main"),
    ("lesson37_shadow_frag", "37-3d-picking/shadow.frag.hlsl", "fragment", "main"),
    ("lesson37_grid_vert", "37-3d-picking/grid.vert.hlsl", "vertex", "main"),
    ("lesson37_grid_frag", "37-3d-picking/grid.frag.hlsl", "fragment", "main"),
    ("lesson37_outline_frag", "37-3d-picking/outline.frag.hlsl", "fragment", "main"),
    ("lesson37_id_pass_vert", "37-3d-picking/id_pass.vert.hlsl", "vertex", "main"),
    ("lesson37_id_pass_frag", "37-3d-picking/id_pass.frag.hlsl", "fragment", "main"),
    ("lesson37_crosshair_vert", "37-3d-picking/crosshair.vert.hlsl", "vertex", "main"),
    ("lesson37_crosshair_frag", "37-3d-picking/crosshair.frag.hlsl", "fragment", "main"),
    ("lesson38_frustum_cull_comp", "38-indirect-drawing/frustum_cull.comp.hlsl", "compute", "main"),
    ("lesson38_indirect_box_vert", "38-indirect-drawing/indirect_box.vert.hlsl", "vertex", "main"),
    ("lesson38_indirect_box_frag", "38-indirect-drawing/indirect_box.frag.hlsl", "fragment", "main"),
    ("lesson38_indirect_shadow_vert", "38-indirect-drawing/indirect_shadow.vert.hlsl", "vertex", "main"),
    ("lesson38_indirect_shadow_frag", "38-indirect-drawing/indirect_shadow.frag.hlsl", "fragment", "main"),
    ("lesson38_debug_box_vert", "38-indirect-drawing/debug_box.vert.hlsl", "vertex", "main"),
    ("lesson38_debug_box_frag", "38-indirect-drawing/debug_box.frag.hlsl", "fragment", "main"),
    ("lesson38_frustum_lines_vert", "38-indirect-drawing/frustum_lines.vert.hlsl", "vertex", "main"),
    ("lesson38_frustum_lines_frag", "38-indirect-drawing/frustum_lines.frag.hlsl", "fragment", "main"),
    ("lesson38_grid_vert", "38-indirect-drawing/grid.vert.hlsl", "vertex", "main"),
    ("lesson38_grid_frag", "38-indirect-drawing/grid.frag.hlsl", "fragment", "main"),
    ("lesson38_truck_scene_vert", "38-indirect-drawing/truck_scene.vert.hlsl", "vertex", "main"),
    ("lesson38_truck_scene_frag", "38-indirect-drawing/truck_scene.frag.hlsl", "fragment", "main"),
    ("lesson39_scene_pipeline_vert", "39-pipeline-processed-assets/scene_pipeline.vert.hlsl", "vertex", "main"),
    ("lesson39_scene_pipeline_frag", "39-pipeline-processed-assets/scene_pipeline.frag.hlsl", "fragment", "main"),
    ("lesson39_scene_raw_vert", "39-pipeline-processed-assets/scene_raw.vert.hlsl", "vertex", "main"),
    ("lesson39_scene_raw_frag", "39-pipeline-processed-assets/scene_raw.frag.hlsl", "fragment", "main"),
    ("lesson39_shadow_vert", "39-pipeline-processed-assets/shadow.vert.hlsl", "vertex", "main"),
    ("lesson39_shadow_frag", "39-pipeline-processed-assets/shadow.frag.hlsl", "fragment", "main"),
    ("lesson39_sky_vert", "39-pipeline-processed-assets/sky.vert.hlsl", "vertex", "main"),
    ("lesson39_sky_frag", "39-pipeline-processed-assets/sky.frag.hlsl", "fragment", "main"),
    ("lesson39_grid_vert", "39-pipeline-processed-assets/grid.vert.hlsl", "vertex", "main"),
    ("lesson39_grid_frag", "39-pipeline-processed-assets/grid.frag.hlsl", "fragment", "main"),
    ("lesson46_particle_sim_comp", "46-particle-animations/particle_sim.comp.hlsl", "compute", "main"),
    ("lesson46_particle_vert", "46-particle-animations/particle.vert.hlsl", "vertex", "main"),
    ("lesson46_particle_frag", "46-particle-animations/particle.frag.hlsl", "fragment", "main"),
    ("lesson49_imposter_bake_vert", "49-imposters/imposter_bake.vert.hlsl", "vertex", "main"),
    ("lesson49_imposter_bake_frag", "49-imposters/imposter_bake.frag.hlsl", "fragment", "main"),
    ("lesson49_imposter_vert", "49-imposters/imposter.vert.hlsl", "vertex", "main"),
    ("lesson49_imposter_frag", "49-imposters/imposter.frag.hlsl", "fragment", "main"),
    ("lesson50_terrain_lod_vert", "50-grass-rendering/terrain_lod.vert.hlsl", "vertex", "main"),
    ("lesson50_terrain_lod_shadow_vert", "50-grass-rendering/terrain_lod_shadow.vert.hlsl", "vertex", "main"),
    ("lesson50_grass_vert", "50-grass-rendering/grass.vert.hlsl", "vertex", "main"),
    ("lesson50_grass_frag", "50-grass-rendering/grass.frag.hlsl", "fragment", "main"),
    ("lesson50_grass_shadow_vert", "50-grass-rendering/grass_shadow.vert.hlsl", "vertex", "main"),
    ("lesson51_pbr_frag", "51-pbr-shading/pbr.frag.hlsl", "fragment", "main"),
    ("lesson52_pbr_textures_frag", "52-pbr-textures/pbr_textures.frag.hlsl", "fragment", "main"),
    ("forge_scene_shadow_vert", "shared-scene/shadow.vert.hlsl", "vertex", "main"),
    ("forge_scene_shadow_frag", "shared-scene/shadow.frag.hlsl", "fragment", "main"),
    ("forge_scene_grid_vert", "shared-scene/grid.vert.hlsl", "vertex", "main"),
    ("forge_scene_grid_frag", "shared-scene/grid.frag.hlsl", "fragment", "main"),
    ("forge_scene_terrain_vert", "shared-scene/terrain.vert.hlsl", "vertex", "main"),
    ("forge_scene_terrain_frag", "shared-scene/terrain.frag.hlsl", "fragment", "main"),
    ("forge_scene_terrain_no_variation_frag", "shared-scene/terrain_no_variation.frag.hlsl", "fragment", "main"),
    ("forge_scene_terrain_shadow_vert", "shared-scene/terrain_shadow.vert.hlsl", "vertex", "main"),
    ("forge_scene_tree_colored_vert", "shared-scene/tree_instanced_colored.vert.hlsl", "vertex", "main"),
    ("forge_scene_tree_colored_frag", "shared-scene/tree_instanced_colored.frag.hlsl", "fragment", "main"),
    ("forge_scene_tree_shadow_vert", "shared-scene/tree_instanced_shadow.vert.hlsl", "vertex", "main"),
    ("forge_scene_sky_vert", "shared-scene/sky.vert.hlsl", "vertex", "main"),
    ("forge_scene_sky_frag", "shared-scene/sky.frag.hlsl", "fragment", "main"),
    ("forge_scene_model_vert", "shared-scene/scene_model.vert.hlsl", "vertex", "main"),
    ("forge_scene_model_frag", "shared-scene/scene_model.frag.hlsl", "fragment", "main"),
    ("forge_scene_textured_vert", "shared-scene/scene_textured.vert.hlsl", "vertex", "main"),
    ("forge_scene_textured_frag", "shared-scene/scene_textured.frag.hlsl", "fragment", "main"),
    ("forge_scene_skinned_vert", "shared-scene/scene_skinned.vert.hlsl", "vertex", "main"),
    ("forge_scene_skinned_shadow_vert", "shared-scene/scene_skinned_shadow.vert.hlsl", "vertex", "main"),
    ("forge_scene_morph_vert", "shared-scene/scene_morph.vert.hlsl", "vertex", "main"),
    ("forge_scene_morph_shadow_vert", "shared-scene/scene_morph_shadow.vert.hlsl", "vertex", "main"),
    ("lesson40_scene_vert", "40-scene-renderer/scene.vert.hlsl", "vertex", "main"),
    ("lesson40_scene_frag", "40-scene-renderer/scene.frag.hlsl", "fragment", "main"),
)

WGSL_SHADERCROSS_EXTRA_ARGS = {}
LAYOUT_POLICY_FLAGS = {
    "--resource-layout-sampled-slot",
    "--storage-texture-slot",
}

LAYOUT_SHADERS = (
    "lesson14_skybox_frag",
    "lesson14_shuttle_frag",
    "lesson15_scene_frag",
    "lesson15_grid_frag",
    "lesson15_debug_quad_frag",
    "lesson21_scene_frag",
    "lesson21_grid_frag",
    "lesson23_scene_frag",
    "lesson23_grid_frag",
    "lesson24_scene_frag",
    "lesson24_grid_frag",
    "lesson27_scene_frag",
    "lesson27_grid_frag",
    "lesson27_ssao_frag",
    "lesson29_scene_frag",
    "lesson29_grid_frag",
    "lesson29_ssr_frag",
    "lesson29_composite_frag",
    "lesson30_scene_frag",
    "lesson30_skybox_frag",
    "lesson30_water_frag",
    "lesson31_scene_frag",
    "lesson31_skybox_frag",
    "lesson32_skin_frag",
    "lesson32_grid_frag",
    "lesson33_pulled_frag",
    "lesson33_grid_frag",
    "lesson34_scene_frag",
    "lesson34_grid_frag",
    "lesson35_scene_frag",
    "lesson35_grid_frag",
    "lesson35_decal_frag",
    "lesson36_scene_frag",
    "lesson36_grid_frag",
    "lesson36_edge_detect_frag",
    "lesson37_scene_frag",
    "lesson37_grid_frag",
    "lesson38_indirect_box_frag",
    "lesson38_grid_frag",
    "lesson38_truck_scene_frag",
    "lesson39_scene_pipeline_frag",
    "lesson39_scene_raw_frag",
    "lesson39_grid_frag",
    "lesson46_particle_vert",
    "lesson46_particle_frag",
    "lesson49_imposter_frag",
    "lesson50_terrain_lod_vert",
    "lesson50_terrain_lod_shadow_vert",
    "lesson50_grass_vert",
    "lesson50_grass_frag",
    "lesson50_grass_shadow_vert",
    "lesson51_pbr_frag",
    "lesson52_pbr_textures_frag",
    "lesson40_scene_frag",
    "forge_scene_grid_frag",
    "forge_scene_terrain_vert",
    "forge_scene_terrain_frag",
    "forge_scene_terrain_no_variation_frag",
    "forge_scene_terrain_shadow_vert",
    "forge_scene_tree_colored_frag",
    "forge_scene_model_frag",
    "forge_scene_textured_frag",
    "forge_scene_skinned_vert",
    "forge_scene_skinned_shadow_vert",
    "forge_scene_morph_vert",
    "forge_scene_morph_shadow_vert",
)

COMPUTE_LAYOUT_SHADERS = (
    "lesson11_plasma_comp",
    "lesson26_transmittance_lut_comp",
    "lesson26_multiscatter_lut_comp",
    "lesson38_frustum_cull_comp",
    "lesson46_particle_sim_comp",
)

LAYOUT_SHADERCROSS_EXTRA_ARGS = {
    "lesson11_plasma_comp": [
        "--storage-texture-slot",
        "class=readwrite,slot=0,texture=2d,format=rgba8unorm,access=write,format_authority=explicit",
    ],
    "lesson26_transmittance_lut_comp": [
        "--storage-texture-slot",
        "class=readwrite,slot=0,texture=2d,format=rgba16float,access=write,format_authority=explicit",
    ],
    "lesson26_multiscatter_lut_comp": [
        "--storage-texture-slot",
        "class=readwrite,slot=0,texture=2d,format=rgba16float,access=write,format_authority=explicit",
    ],
    "lesson15_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson15_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson15_debug_quad_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson21_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson21_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson23_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=4,texture=cube,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson23_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=cube,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=cube,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson24_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson24_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson27_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson27_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson27_ssao_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson29_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson29_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson29_ssr_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=filterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=non_filtering",
    ],
    "lesson29_composite_frag": [
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=4,texture=2d,sample=filterable_float,sampler=non_filtering",
    ],
    "lesson30_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson31_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson32_skin_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson32_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson33_pulled_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson33_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson34_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson34_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson35_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson35_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson35_decal_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=non_filtering",
    ],
    "lesson36_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson36_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson36_edge_detect_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "lesson37_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson37_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson38_indirect_box_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson38_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson38_truck_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson39_scene_pipeline_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson39_scene_raw_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson39_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson40_scene_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "forge_scene_terrain_vert": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "forge_scene_terrain_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "forge_scene_terrain_no_variation_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "forge_scene_terrain_shadow_vert": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "forge_scene_tree_colored_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "forge_scene_grid_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "forge_scene_model_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=4,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=5,texture=2d,sample=depth,sampler=comparison",
    ],
    "forge_scene_textured_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "lesson49_imposter_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "lesson50_terrain_lod_vert": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "lesson50_terrain_lod_shadow_vert": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
    ],
    "lesson50_grass_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=unfilterable_float,sampler=non_filtering",
    ],
    "lesson51_pbr_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=4,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=5,texture=2d,sample=depth,sampler=comparison",
    ],
    "lesson52_pbr_textures_frag": [
        "--resource-layout-sampled-slot",
        "slot=0,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=1,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=2,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=3,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=4,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=5,texture=2d,sample=depth,sampler=comparison",
        "--resource-layout-sampled-slot",
        "slot=6,texture=2d,sample=filterable_float,sampler=filtering",
        "--resource-layout-sampled-slot",
        "slot=7,texture=2d,sample=filterable_float,sampler=filtering",
    ],
}


def shader_map():
    return {
        symbol: {
            "source": source,
            "stage": stage,
            "entrypoint": entrypoint,
        }
        for symbol, source, stage, entrypoint in SHADERS
    }


def layout_symbols():
    return set(LAYOUT_SHADERS) | set(COMPUTE_LAYOUT_SHADERS)


def validate_layout_policy():
    shaders = shader_map()
    layouts = layout_symbols()
    missing_layouts = sorted(layouts - set(shaders))
    if missing_layouts:
        raise SystemExit(f"layout shader(s) missing from SHADERS: {', '.join(missing_layouts)}")

    extra_arg_only = sorted(set(LAYOUT_SHADERCROSS_EXTRA_ARGS) - layouts)
    if extra_arg_only:
        raise SystemExit(f"layout policy arg(s) for non-layout shader(s): {', '.join(extra_arg_only)}")

    overlap = sorted(set(LAYOUT_SHADERS) & set(COMPUTE_LAYOUT_SHADERS))
    if overlap:
        raise SystemExit(f"shader(s) listed as both graphics and compute layout: {', '.join(overlap)}")

    for symbol in LAYOUT_SHADERS:
        if shaders[symbol]["stage"] == "compute":
            raise SystemExit(f"{symbol} is a compute shader but is listed in LAYOUT_SHADERS")
    for symbol in COMPUTE_LAYOUT_SHADERS:
        if shaders[symbol]["stage"] != "compute":
            raise SystemExit(f"{symbol} is not a compute shader but is listed in COMPUTE_LAYOUT_SHADERS")

    for symbol, args in LAYOUT_SHADERCROSS_EXTRA_ARGS.items():
        if len(args) % 2 != 0:
            raise SystemExit(f"{symbol} layout policy args must be flag/value pairs")
        for flag, value in zip(args[0::2], args[1::2]):
            if flag not in LAYOUT_POLICY_FLAGS:
                raise SystemExit(f"{symbol} uses unsupported layout policy flag: {flag}")
            if not value or value.startswith("--"):
                raise SystemExit(f"{symbol} layout policy flag {flag} is missing a value")


def print_layout_policy():
    shaders = shader_map()
    layouts = layout_symbols()

    for symbol, _, _, _ in SHADERS:
        if symbol not in layouts:
            continue
        shader = shaders[symbol]
        layout_kind = "compute" if symbol in COMPUTE_LAYOUT_SHADERS else "shader"
        print(f"{symbol}\t{layout_kind}\t{shader['stage']}\t{shader['source']}")
        args = LAYOUT_SHADERCROSS_EXTRA_ARGS.get(symbol, [])
        if args:
            for flag, value in zip(args[0::2], args[1::2]):
                print(f"  {flag} {value}")
        else:
            print("  reflected layout facts only")


def run(command):
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise SystemExit(
            f"command failed with exit code {completed.returncode}: {' '.join(map(str, command))}\n"
            f"{completed.stdout}"
        )


def c_string_literal(text):
    lines = []
    for line in text.splitlines(keepends=True):
        escaped = (
            line.replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\t", "\\t")
            .replace("\r", "\\r")
            .replace("\n", "\\n")
        )
        lines.append(f'    "{escaped}"')
    if not lines:
        return '    ""'
    return "\n".join(lines)


def c_byte_array(data):
    if not data:
        return "    0"

    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk))
    return ",\n".join(lines)


def header_guard_for(filename):
    stem = filename.upper().replace(".", "_").replace("-", "_")
    return f"SDLGPU_{stem}"


def generated_header_name(lesson_key):
    if lesson_key == "shared-scene":
        return "forge_gpu_shared_scene_shaders.h"
    lesson_number = lesson_key.split("-", 1)[0]
    return f"forge_gpu_lesson_{lesson_number}_shaders.h"


def build_header(generated, filename):
    guard = header_guard_for(filename)
    contents = [
        "/*",
        " * Generated from HLSL files under shaders/source with SDL_shadercross.",
        " * WGSL output uses explicit SDL_shadercross layout facts where WebGPU needs them.",
        " * SPIR-V and DXIL are native-only fixtures; browser builds use WGSL.",
        " * Regenerate with generate-shader-header.py after changing HLSL or shader producers.",
        " */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
    ]

    for symbol, source, stage, wgsl, msl, spirv, dxil in generated:
        contents.extend([
            f"/* {source} ({stage}) */",
            f"static const char {symbol}_wgsl[] =",
            c_string_literal(wgsl),
            ";",
            f"static const unsigned int {symbol}_wgsl_size = sizeof({symbol}_wgsl) - 1u;",
            "",
            f"static const char {symbol}_msl[] =",
            c_string_literal(msl),
            ";",
            f"static const unsigned int {symbol}_msl_size = sizeof({symbol}_msl) - 1u;",
            "",
            "#if !defined(__EMSCRIPTEN__)",
            f"static const unsigned char {symbol}_wgsl_spirv[] = {{",
            c_byte_array(spirv),
            "};",
            f"static const unsigned int {symbol}_wgsl_spirv_size = sizeof({symbol}_wgsl_spirv);",
            "",
            f"static const unsigned char {symbol}_wgsl_dxil[] = {{",
            c_byte_array(dxil),
            "};",
            f"static const unsigned int {symbol}_wgsl_dxil_size = sizeof({symbol}_wgsl_dxil);",
            "#endif",
            "",
        ])

    contents.extend([
        f"#endif /* {guard} */",
        "",
    ])
    return "\n".join(contents)


def build_layout_header(layouts):
    contents = [
        "/*",
        " * Generated from HLSL files under shaders/source with SDL_shadercross.",
        " * Resource layout sidecars are consumed by Forge GPU layout-object helpers.",
        " * Regenerate with generate-shader-header.py after changing HLSL or layout policy.",
        " */",
        "#ifndef SDLGPU_FORGE_GPU_SHADER_LAYOUTS_H",
        "#define SDLGPU_FORGE_GPU_SHADER_LAYOUTS_H",
        "",
        "#include <SDL3/SDL_gpu.h>",
        "",
    ]

    layout_constants = []
    for _, _, _, metadata_c, _ in layouts:
        layout_constants.extend(layout_constant_lines(metadata_c))
    if layout_constants:
        contents.extend(layout_constants)
        contents.append("")

    contents.extend([
        "#ifdef __cplusplus",
        "extern \"C\" {",
        "#endif",
        "",
    ])

    for symbol, _, _, _, layout_kind in layouts:
        if layout_kind == "compute":
            contents.append(f"const SDL_GPUComputePipelineResourceLayoutCreateInfo *ForgeGpuComputePipelineLayout_{symbol}(void);")
        else:
            contents.append(f"const SDL_GPUShaderResourceLayoutCreateInfo *ForgeGpuShaderLayout_{symbol}(void);")

    contents.extend([
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
        "#endif /* SDLGPU_FORGE_GPU_SHADER_LAYOUTS_H */",
        "",
    ])
    return "\n".join(contents)


LAYOUT_CONSTANT_RE = re.compile(r"^enum \{ ([A-Za-z_][A-Za-z0-9_]*) = ([0-9]+) \};$")


def layout_constant_lines(metadata_c):
    return [
        line
        for line in metadata_c.strip().splitlines()
        if LAYOUT_CONSTANT_RE.match(line)
    ]


def clean_metadata_c(metadata_c):
    cleaned = "\n".join(
        line for line in metadata_c.strip().splitlines()
        if line != "#include <SDL3/SDL_gpu.h>" and not LAYOUT_CONSTANT_RE.match(line)
    )
    while "\n\n\n" in cleaned:
        cleaned = cleaned.replace("\n\n\n", "\n\n")
    return cleaned


def build_layout_source(layouts):
    contents = [
        "/*",
        " * Generated from HLSL files under shaders/source with SDL_shadercross.",
        " * Regenerate with generate-shader-header.py after changing HLSL or layout policy.",
        " */",
        "#include \"forge_gpu_shader_layouts.h\"",
        "",
    ]

    for symbol, source, stage, metadata_c, layout_kind in layouts:
        createinfo_type = (
            "SDL_GPUComputePipelineResourceLayoutCreateInfo"
            if layout_kind == "compute"
            else "SDL_GPUShaderResourceLayoutCreateInfo"
        )
        accessor_prefix = "ForgeGpuComputePipelineLayout" if layout_kind == "compute" else "ForgeGpuShaderLayout"
        contents.extend([
            f"/* {source} ({stage}) */",
            clean_metadata_c(metadata_c),
            "",
            f"const {createinfo_type} *{accessor_prefix}_{symbol}(void)",
            "{",
            f"    return &{symbol}_resource_layout_createinfo;",
            "}",
            "",
        ])

    return "\n".join(contents)


def check_or_write(path, contents, check):
    if check:
        if not path.exists() or path.read_text(encoding="utf-8") != contents:
            raise SystemExit(f"{path} is out of date; rerun without --check.")
        return

    path.write_text(contents, encoding="utf-8", newline="\n")


DXIL_BYTE_ARRAY_RE = re.compile(
    r"(static const unsigned char [A-Za-z0-9_]+_wgsl_dxil\[\] = \{\n)"
    r".*?"
    r"(\n\};)",
    re.DOTALL,
)


def redact_dxil_byte_arrays(contents):
    return DXIL_BYTE_ARRAY_RE.sub(
        r"\1    /* DXIL bytes intentionally ignored for this non-canonical check. */\2",
        contents,
    )


def check_or_write_header(path, contents, check, allow_dxil_byte_drift):
    if check:
        if not path.exists():
            raise SystemExit(f"{path} is out of date; rerun without --check.")
        existing = path.read_text(encoding="utf-8")
        if allow_dxil_byte_drift:
            existing = redact_dxil_byte_arrays(existing)
            contents = redact_dxil_byte_arrays(contents)
        if existing != contents:
            raise SystemExit(f"{path} is out of date; rerun without --check.")
        return

    path.write_text(contents, encoding="utf-8", newline="\n")


def check_or_write_shader_headers(output_dir, generated, check, allow_dxil_byte_drift):
    grouped = defaultdict(list)
    for shader in generated:
        _, relative_source, *_ = shader
        lesson_key = relative_source.split("/", 1)[0]
        grouped[lesson_key].append(shader)

    expected_paths = set()
    header_sources = {}
    if not check:
        output_dir.mkdir(parents=True, exist_ok=True)

    for lesson_key in sorted(grouped):
        header_name = generated_header_name(lesson_key)
        if header_name in header_sources:
            raise SystemExit(f"generated shader header collision: {header_sources[header_name]} and {lesson_key} both map to {header_name}")
        header_sources[header_name] = lesson_key
        path = output_dir / header_name
        expected_paths.add(path)
        check_or_write_header(path, build_header(grouped[lesson_key], header_name), check, allow_dxil_byte_drift)

    stale_paths = set()
    if output_dir.exists():
        stale_paths = set(output_dir.glob("forge_gpu_*_shaders.h")) - expected_paths
    if check:
        if stale_paths:
            stale_list = ", ".join(str(path) for path in sorted(stale_paths))
            raise SystemExit(f"stale generated shader header(s): {stale_list}")
    else:
        for path in stale_paths:
            path.unlink()


def main():
    parser = argparse.ArgumentParser(description="Generate WGSL/MSL/SPIR-V/DXIL fixtures for the Forge GPU demo.")
    parser.add_argument("--shadercross", help="Path to the shadercross executable.")
    parser.add_argument("--tint", help="Path to the Tint executable used for WGSL output and validation.")
    parser.add_argument("--source-dir", default="shaders/source", help="HLSL source directory relative to the repository root.")
    parser.add_argument("--output-dir", default="build/generated/shaders/generated", help="Generated per-lesson shader header directory.")
    parser.add_argument("--layout-header", default="build/generated/forge_gpu_shader_layouts.h", help="Generated resource layout header path.")
    parser.add_argument("--layout-source", default="build/generated/forge_gpu_shader_layouts.c", help="Generated resource layout C source path.")
    parser.add_argument("--check", action="store_true", help="Fail if output is not up to date.")
    parser.add_argument("--allow-dxil-byte-drift", action="store_true", help="With --check, compile DXIL but ignore DXIL byte-array differences in generated headers. Use only on non-canonical hosts where DXC emits valid but host-specific DXIL containers.")
    parser.add_argument("--list-layout-policy", action="store_true", help="Print the shaders with generated resource layout sidecars and explicit policy args.")
    args = parser.parse_args()

    if args.allow_dxil_byte_drift and not args.check:
        parser.error("--allow-dxil-byte-drift is only meaningful with --check")

    validate_layout_policy()
    if args.list_layout_policy:
        print_layout_policy()
        return
    if not args.shadercross:
        parser.error("--shadercross is required unless --list-layout-policy is used")
    if not args.tint:
        parser.error("--tint is required unless --list-layout-policy is used")

    project_root = Path(__file__).resolve().parent.parent
    source_dir = Path(args.source_dir)
    if not source_dir.is_absolute():
        source_dir = project_root / source_dir

    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = project_root / output_dir

    layout_header = Path(args.layout_header)
    if not layout_header.is_absolute():
        layout_header = project_root / layout_header

    layout_source = Path(args.layout_source)
    if not layout_source.is_absolute():
        layout_source = project_root / layout_source

    generated = []
    generated_layouts = []
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        for symbol, relative_source, stage, entrypoint in SHADERS:
            source = source_dir / relative_source
            wgsl_path = temp_root / f"{symbol}.wgsl"
            validated_wgsl_path = temp_root / f"{symbol}.validated.wgsl"
            msl_path = temp_root / f"{symbol}.msl"
            spirv_path = temp_root / f"{symbol}.spv"
            dxil_path = temp_root / f"{symbol}.dxil"
            metadata_c_path = temp_root / f"{symbol}.layout.c"
            spirv_metadata_c_path = temp_root / f"{symbol}.spirv.layout.c"
            is_shader_layout = symbol in LAYOUT_SHADERS
            is_compute_layout = symbol in COMPUTE_LAYOUT_SHADERS
            is_layout_shader = is_shader_layout or is_compute_layout
            include_dir = source.parent

            wgsl_command = [
                args.shadercross,
                str(source),
                "-s", "HLSL",
                "-t", stage,
                "-d", "WGSL",
                "--tint", args.tint,
                "-e", entrypoint,
                "-c",
                "-I", str(include_dir),
                "-o", str(wgsl_path),
            ] + WGSL_SHADERCROSS_EXTRA_ARGS.get(symbol, [])
            if is_layout_shader:
                wgsl_command += [
                    "--resource-layout-c", str(metadata_c_path),
                    "--resource-layout-symbol-prefix", symbol,
                ] + LAYOUT_SHADERCROSS_EXTRA_ARGS.get(symbol, [])
            run(wgsl_command)
            wgsl = wgsl_path.read_text(encoding="utf-8")
            run([
                args.tint,
                "--input-format=wgsl",
                "--format=wgsl",
                "--validate=true",
                "--output-name",
                str(validated_wgsl_path),
                str(wgsl_path),
            ])
            run([
                args.shadercross,
                str(source),
                "-s", "HLSL",
                "-t", stage,
                "-d", "MSL",
                "--tint", args.tint,
                "-e", entrypoint,
                "-c",
                "-I", str(include_dir),
                "-o", str(msl_path),
            ])
            spirv_command = [
                args.shadercross,
                str(source),
                "-s", "HLSL",
                "-t", stage,
                "-d", "SPIRV",
                "-e", entrypoint,
                "-c",
                "-I", str(include_dir),
                "-o", str(spirv_path),
            ]
            if is_layout_shader:
                spirv_command += [
                    "--resource-layout-c", str(spirv_metadata_c_path),
                    "--resource-layout-symbol-prefix", symbol,
                ] + LAYOUT_SHADERCROSS_EXTRA_ARGS.get(symbol, [])
            run(spirv_command)
            run([
                args.shadercross,
                str(source),
                "-s", "HLSL",
                "-t", stage,
                "-d", "DXIL",
                "-e", entrypoint,
                "-c",
                "-I", str(include_dir),
                "-o", str(dxil_path),
            ])

            generated.append((
                symbol,
                relative_source,
                stage,
                wgsl,
                msl_path.read_text(encoding="utf-8"),
                spirv_path.read_bytes(),
                dxil_path.read_bytes(),
            ))
            if is_layout_shader:
                generated_layouts.append((
                    symbol,
                    relative_source,
                    stage,
                    metadata_c_path.read_text(encoding="utf-8"),
                    "compute" if is_compute_layout else "shader",
                ))

    layout_header_contents = build_layout_header(generated_layouts)
    layout_source_contents = build_layout_source(generated_layouts)
    check_or_write_shader_headers(output_dir, generated, args.check, args.allow_dxil_byte_drift)
    check_or_write(layout_header, layout_header_contents, args.check)
    check_or_write(layout_source, layout_source_contents, args.check)
    if args.check:
        print(f"{output_dir}, {layout_header}, and {layout_source} are up to date.")


if __name__ == "__main__":
    main()
