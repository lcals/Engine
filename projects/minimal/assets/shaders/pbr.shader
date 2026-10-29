vertex = "0513abb1-dd3c-48c0-95e4-7a506c1615b1"
fragment = "5c3a334f-6214-4e4e-a964-f5a3eeff0629"
varying = "b8b2d68f-2cc3-44db-b434-ea1c7e12d38a"

[[parameters]]
name = "baseColor"
uniform = "u_base_color"
type = "v4"
value = [0.7, 0.3, 0.15, 1.0]
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "metallic"
uniform = "u_material"
offset = 0
type = "float"
value = 0.0
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "roughness"
uniform = "u_material"
offset = 1
type = "float"
value = 0.4
bounded = true
minimum = 0.0
maximum = 1.0

[[parameters]]
name = "lightingMode"
uniform = "u_material"
offset = 2
type = "float"
value = 0.0
exposed = false
bounded = true
minimum = 0.0
maximum = 1.0

[[samplers]]
name = "baseColorTexture"
uniform = "s_base_color"
stage = 0
default_texture = "builtin/textures/white-srgb"

[[samplers]]
name = "normalTexture"
uniform = "s_normal"
stage = 1
default_texture = "builtin/textures/flat-normal"

[[samplers]]
name = "ormTexture"
uniform = "s_orm"
stage = 2
default_texture = "builtin/textures/white-linear"
