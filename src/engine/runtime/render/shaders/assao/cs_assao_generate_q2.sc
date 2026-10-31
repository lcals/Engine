/*
 * Adapted from bgfx examples/39-assao at
 * 7a70927d2af7ffcc5cef9c9e8f4c821e31fcb099. Fixed quality 2, two smart blur passes.
 * Changes: private uniforms, orthographic reconstruction, safe border reads,
 * and linear visibility for forward indirect lighting. The sampling pattern,
 * depth mip weighting, edge detection and smart filtering remain upstream.
 * License and change details: third_party/notices/bgfx-assao.md.
 */

/*
 * Copyright 2018 Attila Kocsis. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#define ASSAO_QUALITY 2
#define ASSAO_ADAPTIVE_BASE false

#include "cs_assao_generate_q.sh"
