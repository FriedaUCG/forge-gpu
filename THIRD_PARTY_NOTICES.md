# Third-Party Notices

This demo imports selected reviewed source and assets from `forge-gpu`.

## forge-gpu

Copyright (c) 2025 Rosy Game Studio

License: Zlib

The imported forge-gpu shader sources and lesson-derived demo code are altered
for this combined SDL_GPU native/browser demo. The original notice is retained
here for the source distribution:

```text
This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is
   not required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
```

`third_party/forge_gpu/common/` contains imported forge-gpu shared headers for
arena allocation, glTF/OBJ loading, math, procedural geometry, and the lesson 28
immediate-mode UI. The imported headers carry SPDX Zlib markers and stay under
the forge-gpu provenance above.

`assets/textures/47-texture-atlas-rendering/` contains the lesson 47 material
PNG set plus `atlas.png`/`atlas.json` from forge-gpu and stays under the
forge-gpu zlib provenance above.

## Brick Wall Texture

`assets/textures/04-textures-and-samplers/brick_wall.png` is from Poly Haven's
`brick_wall_10` asset by Dimitrios Savva, released under CC0.

## cJSON

`third_party/cJSON/` is cJSON by Dave Gamble and cJSON contributors, licensed
under the MIT license. The full notice is retained in the imported source.

## Dear ImGui

`third_party/imgui/` is Dear ImGui by Omar Cornut and contributors, licensed
under the MIT license. The full notice is retained in the imported source.

## Space Shuttle Model

`assets/models/space-shuttle/` is the forge-gpu lesson 08 space shuttle model.
The lesson credits Microsoft as author and lists the model from Sketchfab under
Creative Commons Attribution 4.0 (`https://creativecommons.org/licenses/by/4.0/`):
`https://sketchfab.com/3d-models/space-shuttle-0b4ef1a8fdd54b7286a2a374ac5e90d7`.

## CesiumMilkTruck Model

`assets/models/CesiumMilkTruck/` is the CesiumMilkTruck glTF sample asset by
Cesium, under CC BY 4.0 through the Khronos glTF Sample Assets collection.

## CesiumMan Model

`assets/models/CesiumMan/` is the CesiumMan glTF sample asset by Cesium, under
CC BY 4.0 through the Khronos glTF Sample Assets collection.

## BoxTextured Model

`assets/models/BoxTextured/` is the BoxTextured glTF sample asset by the
Khronos Group, under CC BY 4.0 through the Khronos glTF Sample Assets
collection.

## WaterBottle Model And Processed Fixtures

`assets/models/WaterBottle/` and
`assets/processed/39-pipeline-processed-assets/` are imported from the
forge-gpu lesson 39 asset directory. The processed fixtures are derived from
the lesson's WaterBottle and BoxTextured source assets and use the forge-gpu
provenance above.

`assets/processed/41-scene-model-loading/` contains offline-generated
processed fixtures for the source CesiumMilkTruck, Suzanne, and Duck assets.
They were generated from the forge-gpu asset pipeline with meshoptimizer
commit `4affad044571506a5724c9a6f15424f43e86f731` and MikkTSpace commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`, and stay under the corresponding
source model provenance below.

`assets/processed/42-pipeline-texture-compression/ABeautifulGame/` contains
offline-generated processed fixtures for the source ABeautifulGame asset
described below. They were generated from the forge-gpu asset pipeline
with meshoptimizer commit
`4affad044571506a5724c9a6f15424f43e86f731`, MikkTSpace commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`, and Basis Universal commit
`28e76f6e340e688188c94386ff068e437e1222ca`. The imported runtime fixture set
contains `.fscene`, `.fmesh`, `.fmat`, 33 `.ftex` files, and 33 consumed
texture compression sidecars; PNG/KTX2 intermediates and model-level scratch
metadata are excluded.

`assets/processed/43-pipeline-skinned-animations/` contains offline-generated
processed fixtures for the source CesiumMan, BrainStem, and AnimatedCube
assets. They were generated from the forge-gpu asset pipeline with
meshoptimizer commit `4affad044571506a5724c9a6f15424f43e86f731` and
MikkTSpace commit `3e895b49d05ea07e4c2133156cfa94369e19e409`. The imported
runtime fixture set contains `.fscene`, `.fmesh`, `.fmat`, `.fskin`, `.fanim`,
PNG textures, and consumed texture sidecars; offline scratch metadata and tool
outputs are excluded.

`assets/processed/44-pipeline-morph-animations/` contains offline-generated
processed fixtures for the source AnimatedMorphCube and SimpleMorph assets.
They were generated from the forge-gpu asset pipeline with meshoptimizer
commit `4affad044571506a5724c9a6f15424f43e86f731` and MikkTSpace commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`. The imported runtime fixture set
contains `.fscene`, `.fmesh`, `.fmat`, and `.fanim` files; offline scratch
metadata and tool outputs are excluded.

`assets/processed/51-pbr-shading/Shaderball/` contains offline-generated
processed fixtures for the Shaderball model described below. They were
generated from the forge-gpu asset pipeline with meshoptimizer commit
`4affad044571506a5724c9a6f15424f43e86f731` and MikkTSpace commit
`3e895b49d05ea07e4c2133156cfa94369e19e409`. The imported runtime fixture set
contains `.fscene`, `.fmesh`, `.fmat`, and the source attribution file.

`assets/processed/52-pbr-textures/materials/` contains imported runtime PNG
maps and generated `.fmat`/`.meta.json` sidecars for the six material
directories used by forge-gpu lesson 52: `Metal046B`, `Metal048A`,
`Metal061B`, `ChristmasTreeOrnament021`, `Rock026`, and `WoodFloor051`. The
Forge lesson README identifies these materials as ambientCG assets under the
CC0 license. EXR/source pipeline intermediates are not imported.

`assets/processed/format-fixtures/checkerboard.ftex` is the tiny
cross-language `.ftex` format fixture from forge-gpu's pipeline tests. It is
used only by the processed-asset self-test.

## Liberation Mono Font

`assets/fonts/liberation_mono/LiberationMono-Regular.ttf` is used by the lesson
28 Forge UI font atlas. The imported package retains its `License.txt`,
`COPYING`, and `README` files. The package license text states that Liberation
font software is licensed under GNU GPL v2 with the font/document embedding and
physical-product object-code exceptions set out in that license agreement.

## ABeautifulGame Model

`assets/processed/42-pipeline-texture-compression/ABeautifulGame/` is derived
from the ABeautifulGame glTF sample asset credited to Khronos Group and
licensed under CC BY 4.0 through the Khronos glTF Sample Assets collection.

## Duck Model

`assets/models/Duck/` is the Duck glTF sample asset credited to Sony and
listed by forge-gpu under the SCEA Shared Source License 1.0 through the
Khronos glTF Sample Assets collection:
`https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Duck`.

## BrainStem Model

`assets/processed/43-pipeline-skinned-animations/BrainStem/` is derived from
the BrainStem glTF sample asset. The Khronos glTF Sample Assets listing
credits Keith Hunter / Smith Micro Software, Inc. and lists the asset under
the Poser EULA.

## AnimatedCube Model

`assets/processed/43-pipeline-skinned-animations/AnimatedCube/` is derived
from the Animated Cube glTF sample asset by UX3D / Norbert Nopper, under CC0
1.0 through the Khronos glTF Sample Assets collection.

## AnimatedMorphCube And SimpleMorph Models

`assets/processed/44-pipeline-morph-animations/AnimatedMorphCube/` and
`assets/processed/44-pipeline-morph-animations/SimpleMorph/` are derived from
the corresponding forge-gpu lesson assets and use the forge-gpu provenance
above.

## Shaderball Model

`assets/processed/51-pbr-shading/Shaderball/` is derived from "Shader Ball
2016.2" by adalberto.torres.nikel, imported from Sketchfab and licensed under
CC BY 4.0. The source attribution file is retained in the imported fixture
directory.

## Suzanne Model

`assets/models/Suzanne/` is the Suzanne default Blender mesh by the Blender
Foundation, under CC0 1.0.

## Milky Way Cube Map

`assets/skyboxes/milkyway/` contains cube-map faces imported from forge-gpu's
environment mapping lesson. The lesson README credits ESO / S. Brunier's
Milky Way panorama under CC BY 4.0.

## Transmission Order Test Model

`assets/models/TransmissionOrderTest/` is the TransmissionOrderTest glTF
sample asset used by forge-gpu's blending lesson. The asset metadata lists it
as public domain / CC0, with the model by Ed Mackey and the cloth backdrop by
Adobe.

## Normal Tangent Mirror Test Model

`assets/models/NormalTangentMirrorTest/` is the NormalTangentMirrorTest glTF
sample asset used by forge-gpu's normal-map lesson. The asset metadata lists
copyright 2017-2018 Analytical Graphics, Inc., CC BY 4.0, with mesh and
textures by Ed Mackey.

## Searchlight Model

`assets/models/Searchlight/` is "Low-Poly Searchlight" by Jerd, imported from
Sketchfab and licensed under CC BY 4.0. The upstream model source is
`https://sketchfab.com/3d-models/low-poly-searchlight-3872bcf198944fb48b756c861e1bbeda`.

## Lesson 24 Gobo Texture

`assets/textures/24-gobo-spotlight/gobo_window.png` is imported from the
forge-gpu lesson 24 asset directory and uses the forge-gpu provenance above.

## Planar Reflections Boat Model

`assets/models/PlanarReflections/boat/` is "Boat 3d low-poly" by
shevchenkomr29, imported from Sketchfab and licensed under CC BY 4.0. The
upstream model source is
`https://sketchfab.com/3d-models/boat-3d-low-poly-cc4e4619d8994b71b1f9230033cd1947`.

## Planar Reflections Rock-Cliff Model

`assets/models/PlanarReflections/rocks/` is "Low Poly Rock Cliffs" by
navebackwards, imported from Sketchfab and licensed under CC BY 4.0. The
upstream model source is
`https://sketchfab.com/3d-models/low-poly-rock-cliffs-f43822b6eead40a2b977e8d0fa8ec757`.

## Transform Animations Track Model

`assets/models/TransformAnimations/track/` is "Modular Track Roads Free" by
Bedrill, imported from Sketchfab and licensed under CC BY 4.0. The upstream
model source is
`https://sketchfab.com/3d-models/modular-track-roads-free-4987c1660df44855a0ac18e33f5130f8`.

## Citrus Orchard Skybox

`assets/skyboxes/citrus-orchard/` contains cube-map PNG faces imported from
forge-gpu's planar-reflections lesson. The lesson README credits Poly Haven's
"Citrus Orchard Pure Sky" by Jarod Guest (photography) and Poly Haven
(processing), released under CC0 1.0. The imported runtime assets are the
prebuilt PNG cube faces, not the source HDR panorama.
