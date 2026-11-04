# bgfx debugdraw excerpt

Source: https://github.com/bkaradzic/bgfx/tree/b2513038f4b753ebeced40821c193978daa0492f/examples/common/debugdraw

Fixed bgfx API 167 release excerpt, BSD-2-Clause; see LICENSE and original headers.
Only debugdraw, its official embedded shaders, packrect.h and the required
checkAvailTransientBuffers excerpt from bgfx_utils.h are included. No entry,
window or example-common framework is compiled. Sources remain upstream-identical;
the broad bgfx_utils include is replaced with the small upstream helper only in
the build copy, using the adjacent patch.
