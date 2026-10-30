#ifndef ENGINE_MATERIAL_HELPERS_SH
#define ENGINE_MATERIAL_HELPERS_SH

// Threshold/discard adapted from bgfx 16-shadowmaps/fs_shadowmaps_color_texture.sc.
// Copyright 2013-2014 Dario Manesku. BSD-2-Clause; third_party/notices/bgfx.txt.
// Changes: coverage alpha instead of red, configurable cutoff, shared color/shadow behavior.
void apply_alpha_cutoff(float alpha, float cutoff)
{
    if (cutoff > 0.0 && alpha < cutoff)
    {
        discard;
    }
}

#endif
