vertex = "0513abb1-dd3c-48c0-95e4-7a506c1615b1"
fragment = "5c3a334f-6214-4e4e-a964-f5a3eeff0629"
varying = "b8b2d68f-2cc3-44db-b434-ea1c7e12d38a"
surface_fragment = "a5a67936-a0d4-5f0d-ad73-de383a4a0b38"

[[parameters]]
name = "baseColor"
uniform = "u_base_color"
type = "v4"
value = [0.7, 0.3, 0.15, 1.0]
offset = 0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "metallic"
uniform = "u_material"
type = "float"
value = 0.0
offset = 0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "roughness"
uniform = "u_material"
type = "float"
value = 0.4
offset = 1
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "lightingMode"
uniform = "u_material"
type = "float"
value = 0.0
offset = 2
exposed = false
bounded = true
minimum = 0.0
maximum = 2.0

[[parameters]]
name = "alphaCutoff"
uniform = "u_material"
type = "float"
value = 0.0
offset = 3
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "specularWorkflow"
uniform = "u_specular"
type = "float"
value = 0.0
offset = 3
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "smoothnessSource"
uniform = "u_specular_options"
type = "float"
value = 0.0
offset = 0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "specularHighlights"
uniform = "u_specular_options"
type = "float"
value = 1.0
offset = 1
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "smoothness"
uniform = "u_surface_parameters"
type = "float"
value = 0.5
offset = 0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "normalScale"
uniform = "u_surface_parameters"
type = "float"
value = 1.0
offset = 1
bounded = true
minimum = 0.0
maximum = 8.0

[[parameters]]
name = "fogInfluence"
uniform = "u_surface_parameters"
type = "float"
value = 1.0
offset = 2
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "specularColor"
uniform = "u_specular"
type = "v3"
value = [0.04, 0.04, 0.04]
offset = 0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "emissionColor"
uniform = "u_emission"
type = "v3"
value = [0.0, 0.0, 0.0]
offset = 0
bounded = true
minimum = 0.0
maximum = 10000.0

[[parameters]]
name = "uvTransform"
uniform = "u_uv_transform"
type = "v4"
value = [1.0, 1.0, 0.0, 0.0]
offset = 0

[[samplers]]
name = "baseColorTexture"
uniform = "s_base_color"
stage = 0
default_texture = "builtin://textures/white-srgb"

[[samplers]]
name = "normalTexture"
uniform = "s_normal"
stage = 1
default_texture = "builtin://textures/flat-normal"

[[samplers]]
name = "ormTexture"
uniform = "s_orm"
stage = 2
default_texture = "builtin://textures/white-linear"

[[samplers]]
name = "specularTexture"
uniform = "s_specular"
stage = 3
default_texture = "builtin://textures/white-linear"

[[samplers]]
name = "emissionTexture"
uniform = "s_emission"
stage = 4
default_texture = "builtin://textures/white-srgb"
