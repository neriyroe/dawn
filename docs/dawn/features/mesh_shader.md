# Native mesh shaders

`dawn-mesh-shader` exposes native task/mesh stages through `DawnShaderSourceNative`,
`DawnMeshPipelineState`, and `DrawMeshTasks` on render pass and bundle encoders.
Metal requires a Metal 4 device and macOS/iOS/tvOS 26 or newer.

Native modules carry DXIL, SPIR-V, or MSL. The backend selects its own source.
`mslEntryPoint` defaults to `entryPoint`. Metal uses `workgroupSize` as the exact
object or mesh dispatch size. Metal derives object-to-mesh payload allocation
from the compiled `object_data` type.
Each active MSL buffer binding declares its compiler-assigned `metalIndex`;
`WGPU_LIMIT_U32_UNDEFINED` identifies an optimized-out binding. Indices are local
to each shader stage and do not depend on the pipeline layout's packed indices.
Dynamic buffer offsets apply at dispatch. Native sources are trusted shader code;
they must enforce their own buffer bounds and matching stage interfaces.

Metal accepts at most 256 threads per object or mesh workgroup. Its compiler
validates the payload allocation against the device's payload memory limit.
Direct mesh grids without an object stage obey the GPU family's mesh-grid limit.
Object shaders must also obey that limit when emitting their mesh grids.

Build `dawn_metal_mesh_shader_tests` with `DAWN_BUILD_TESTS=ON` to exercise native
object/mesh dispatch, payloads, dynamic offsets, pipeline switching, and bundles.

