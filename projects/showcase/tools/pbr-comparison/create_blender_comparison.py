"""Build independent Blender Cycles reference scenes for the engine PBR matrix.

Usage: blender -b -t 8 --python projects/showcase/tools/pbr-comparison/create_blender_comparison.py -- [group...]
Set SHARED_TEXTURE_DIR to the directory containing the exact engine-side maps.
Does not read or change a user's open Blender file.
"""
import bpy
from mathutils import Vector
from pathlib import Path
import os, sys, tomllib

repo = Path(__file__).resolve().parents[4]
out = Path(os.environ.get('PBR_OUTPUT_DIR', str(repo / 'build/material-comparison/expanded')))
out.mkdir(parents=True, exist_ok=True)
maps = Path(os.environ.get('SHARED_TEXTURE_DIR', str(repo / 'projects/showcase/assets/textures')))
groups = ('basecolor', 'metallic', 'roughness', 'normal-orm', 'emission', 'uv', 'alpha', 'normal-orm-same-mesh', 'normal-orm-gltf-same-mesh', 'specular-ior', 'specular-map', 'specular-map-approx', 'smoothness-source', 'alpha-map')
shared_mesh_groups = ('uv', 'normal-orm-same-mesh', 'normal-orm-gltf-same-mesh')
chosen = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [g for g in groups if g not in shared_mesh_groups]
if not chosen:
    chosen = [g for g in groups if g not in shared_mesh_groups]
assert all(x in groups for x in chosen), chosen
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 128
scene.cycles.use_denoising = False
scene.cycles.seed = 0
scene.cycles.max_bounces = 0
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1
world = bpy.data.worlds.new('Black environment')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0, 0, 0, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
scene.world = world
spheres = []
if any(g in chosen for g in shared_mesh_groups):
    if len(chosen) != 1:
        raise RuntimeError('UV case must be rendered separately to use the engine mesh')
    mesh_path = repo / 'projects/showcase/assets/meshes/pbr-comparison-sphere.mesh'
    with mesh_path.open('rb') as stream:
        source_mesh = tomllib.load(stream)
    positions = [(v[0], -v[2], v[1]) for v in source_mesh['vertices']]
    normals = [(v[3], -v[5], v[4]) for v in source_mesh['vertices']]
    indices = source_mesh['indices']
    faces = [indices[i:i+3] for i in range(0, len(indices), 3)]
    source_uv = source_mesh['texcoords']
    shared_mesh = bpy.data.meshes.new('Engine comparison sphere mesh')
    shared_mesh.from_pydata(positions, [], faces)
    shared_mesh.update()
    uv_layer = shared_mesh.uv_layers.new(name='UVMap')
    for loop in shared_mesh.loops:
        uv_layer.data[loop.index].uv = source_uv[loop.vertex_index]
    for face in shared_mesh.polygons:
        face.use_smooth = True
    shared_mesh.normals_split_custom_set_from_vertices(normals)
for row in range(2):
    for col in range(3):
        if any(g in chosen for g in shared_mesh_groups):
            sphere = bpy.data.objects.new('Sphere', shared_mesh.copy())
            scene.collection.objects.link(sphere)
            sphere.location = ((-2.2, 0, 2.2)[col], 0, (1.1, -1.1)[row])
        else:
            bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=32, radius=0.9,
                                                 location=((-2.2, 0, 2.2)[col], 0, (1.1, -1.1)[row]))
            sphere = bpy.context.object
        sphere.name = f'Sphere_{row*3+col+1}'
        # Keep other test spheres out of indirect, glossy and shadow rays;
        # direct directional light still illuminates each camera-visible sphere.
        sphere.visible_diffuse = False
        sphere.visible_glossy = False
        sphere.visible_transmission = False
        sphere.visible_shadow = False
        for face in sphere.data.polygons:
            face.use_smooth = True
        spheres.append(sphere)
camera_data = bpy.data.cameras.new('Comparison camera')
camera = bpy.data.objects.new('Comparison camera', camera_data)
scene.collection.objects.link(camera)
camera.location = (0, -12, 0)
camera.rotation_euler = (Vector((0, 0, 0)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 12.8
scene.camera = camera
light_data = bpy.data.lights.new('White directional light', 'SUN')
light_data.energy = 1.0
light_data.color = (1, 1, 1)
light_data.angle = 0.0
light = bpy.data.objects.new('White directional light', light_data)
scene.collection.objects.link(light)
light.location = (-4, -7, 10)
light.rotation_euler = (-Vector(light.location)).to_track_quat('-Z', 'Y').to_euler()

def image_node(nodes, filename, colorspace):
    path = maps / filename
    if not path.is_file():
        raise FileNotFoundError(path)
    node = nodes.new('ShaderNodeTexImage')
    node.image = bpy.data.images.load(str(path), check_existing=False)
    node.image.colorspace_settings.name = colorspace
    node.interpolation = 'Linear'
    node.extension = 'REPEAT'
    return node

def build_material(group, i):
    material = bpy.data.materials.new(f'{group}-{i+1}')
    material.use_nodes = True
    tree = material.node_tree
    bsdf = tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = (0.5, 0.5, 0.5, 1)
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = 0.5
    bsdf.inputs['IOR'].default_value = 1.5
    if group == 'basecolor':
        colors = ((.18,.18,.18), (.5,.5,.5), (.8,.8,.8), (.8,.05,.05), (.05,.8,.05))
        if i < 5:
            bsdf.inputs['Base Color'].default_value = (*colors[i], 1)
        else:
            tree.links.new(image_node(tree.nodes, 'pbr-test-color.png', 'sRGB').outputs['Color'], bsdf.inputs['Base Color'])
    elif group == 'metallic':
        if i < 5:
            bsdf.inputs['Metallic'].default_value = (0,.25,.5,.75,1)[i]
        else:
            separate = tree.nodes.new('ShaderNodeSeparateColor')
            tree.links.new(image_node(tree.nodes, 'pbr-test-orm-metal.png', 'Non-Color').outputs['Color'], separate.inputs['Color'])
            tree.links.new(separate.outputs['Blue'], bsdf.inputs['Metallic'])
    elif group in ('specular-map','specular-map-approx'):
        # Exact at normal incidence for grayscale F0 only: F0 = ((IOR - 1)/(IOR + 1))^2.
        # The diffuse, grazing reflectance and direct-light equations still differ.
        if i < 5:
            color = image_node(tree.nodes, f'pbr-test-spec-rgb-{(0,64,128,191,255)[i]}.png', 'Non-Color')
            channel = tree.nodes.new('ShaderNodeSeparateColor')
            scale = tree.nodes.new('ShaderNodeMath')
            scale.operation = 'MULTIPLY'
            scale.inputs[1].default_value = .04
            root = tree.nodes.new('ShaderNodeMath')
            root.operation = 'SQRT'
            numerator = tree.nodes.new('ShaderNodeMath')
            numerator.operation = 'ADD'
            numerator.inputs[0].default_value = 1
            denominator = tree.nodes.new('ShaderNodeMath')
            denominator.operation = 'SUBTRACT'
            denominator.inputs[0].default_value = 1
            ratio = tree.nodes.new('ShaderNodeMath')
            ratio.operation = 'DIVIDE'
            tree.links.new(color.outputs['Color'], channel.inputs['Color'])
            tree.links.new(channel.outputs['Red'], scale.inputs[0])
            tree.links.new(scale.outputs[0], root.inputs[0])
            tree.links.new(root.outputs[0], numerator.inputs[1])
            tree.links.new(root.outputs[0], denominator.inputs[1])
            tree.links.new(numerator.outputs[0], ratio.inputs[0])
            tree.links.new(denominator.outputs[0], ratio.inputs[1])
            tree.links.new(ratio.outputs[0], bsdf.inputs['IOR'])
        else:
            # Blender has no independent direct-specular switch matching the engine.
            # IOR=1 is only a rough comparison; default scene keeps regular IOR=1.5.
            bsdf.inputs['IOR'].default_value = 1.0 if group == 'specular-map-approx' else 1.5
    elif group == 'smoothness-source':
        if i < 3:
            smoothness = (0,.5,1)[i]
            bsdf.inputs['Roughness'].default_value = 1.0 - smoothness
        else:
            filename = (f'pbr-test-spec-alpha-{(64,191)[i-3]}.png' if i < 5
                        else 'pbr-test-base-alpha-128.png')
            image = image_node(tree.nodes, filename, 'Non-Color' if i < 5 else 'sRGB')
            complement = tree.nodes.new('ShaderNodeMath')
            complement.operation = 'SUBTRACT'
            complement.inputs[0].default_value = 1
            tree.links.new(image.outputs['Alpha'], complement.inputs[1])
            tree.links.new(complement.outputs[0], bsdf.inputs['Roughness'])
    elif group == 'alpha-map':
        alpha = image_node(tree.nodes, f'pbr-test-base-alpha-{(64,127,128,129,191,255)[i]}.png', 'sRGB')
        less = tree.nodes.new('ShaderNodeMath')
        less.operation = 'LESS_THAN'
        less.inputs[1].default_value = .5
        invert = tree.nodes.new('ShaderNodeMath')
        invert.operation = 'SUBTRACT'
        invert.inputs[0].default_value = 1
        tree.links.new(alpha.outputs['Alpha'], less.inputs[0])
        tree.links.new(less.outputs[0], invert.inputs[1])
        tree.links.new(invert.outputs[0], bsdf.inputs['Alpha'])
    elif group == 'specular-ior':
        bsdf.inputs['IOR'].default_value = (1.0,1.2,1.5,1.8,2.0,2.5)[i]
    elif group == 'roughness':
        bsdf.inputs['Roughness'].default_value = (0,.045,.2,.5,.8,1)[i]
    elif group in ('normal-orm','normal-orm-same-mesh','normal-orm-gltf-same-mesh'):
        if 1 <= i <= 3:
            normal = tree.nodes.new('ShaderNodeNormalMap')
            strength = (0,.5,1)[i-1]
            source = image_node(tree.nodes, 'pbr-test-normal.png', 'Non-Color').outputs['Color']
            if group == 'normal-orm-gltf-same-mesh':
                # glTF 2.0 normalTexture.scale operates on decoded XY before normalization.
                # Pre-encode (scale*(rgb-.5)+.5) and use a unit-strength Blender node.
                # The native Blender Strength parameter has a different interpolation contract.
                separate = tree.nodes.new('ShaderNodeSeparateColor')
                combine = tree.nodes.new('ShaderNodeCombineColor')
                tree.links.new(source, separate.inputs['Color'])
                for channel in ('Red', 'Green'):
                    scale = tree.nodes.new('ShaderNodeMath')
                    scale.operation = 'MULTIPLY_ADD'
                    scale.inputs[1].default_value = strength
                    scale.inputs[2].default_value = .5 * (1 - strength)
                    tree.links.new(separate.outputs[channel], scale.inputs[0])
                    tree.links.new(scale.outputs[0], combine.inputs[channel])
                tree.links.new(separate.outputs['Blue'], combine.inputs['Blue'])
                source = combine.outputs['Color']
                normal.inputs['Strength'].default_value = 1
            else:
                normal.inputs['Strength'].default_value = strength
            tree.links.new(source, normal.inputs['Color'])
            tree.links.new(normal.outputs['Normal'], bsdf.inputs['Normal'])
        elif i >= 4:
            separate = tree.nodes.new('ShaderNodeSeparateColor')
            tree.links.new(image_node(tree.nodes, ('pbr-test-orm-low.png','pbr-test-orm-high.png')[i-4], 'Non-Color').outputs['Color'], separate.inputs['Color'])
            tree.links.new(separate.outputs['Green'], bsdf.inputs['Roughness'])
    elif group == 'alpha':
        alpha, cutoff = ((.49,.5),(.5,.5),(.51,.5),(0,0),(.5,0),(1,0))[i]
        alpha_value = tree.nodes.new('ShaderNodeValue')
        alpha_value.label = 'Base color alpha'
        alpha_value.outputs[0].default_value = alpha
        less = tree.nodes.new('ShaderNodeMath')
        less.operation = 'LESS_THAN'
        less.inputs[1].default_value = cutoff
        active = tree.nodes.new('ShaderNodeMath')
        active.operation = 'GREATER_THAN'
        active.inputs[0].default_value = cutoff
        active.inputs[1].default_value = 0
        discard = tree.nodes.new('ShaderNodeMath')
        discard.operation = 'MULTIPLY'
        invert = tree.nodes.new('ShaderNodeMath')
        invert.operation = 'SUBTRACT'
        invert.inputs[0].default_value = 1
        tree.links.new(alpha_value.outputs[0], less.inputs[0])
        tree.links.new(less.outputs[0], discard.inputs[0])
        tree.links.new(active.outputs[0], discard.inputs[1])
        tree.links.new(discard.outputs[0], invert.inputs[1])
        tree.links.new(invert.outputs[0], bsdf.inputs['Alpha'])
    elif group == 'uv':
        transform = ((1,1,0,0),(2,2,0,0),(1,1,.25,0),(1,1,0,.25),(.5,.5,0,0),(1,1,.5,.5))[i]
        coord = tree.nodes.new('ShaderNodeUVMap')
        multiply = tree.nodes.new('ShaderNodeVectorMath')
        multiply.operation = 'MULTIPLY'
        multiply.inputs[1].default_value = (transform[0],transform[1],1)
        offset = tree.nodes.new('ShaderNodeVectorMath')
        offset.operation = 'ADD'
        offset.inputs[1].default_value = (transform[2],transform[3],0)
        image = image_node(tree.nodes, 'pbr-test-uv-pattern.png', 'sRGB')
        tree.links.new(coord.outputs['UV'], multiply.inputs[0])
        tree.links.new(multiply.outputs['Vector'], offset.inputs[0])
        # Engine 2D texture origin is top-left; Blender image UV is bottom-left.
        # Flip after applying the shared scale/offset, so offsets retain engine semantics.
        flip = tree.nodes.new('ShaderNodeVectorMath')
        flip.operation = 'MULTIPLY'
        flip.inputs[1].default_value = (1,-1,1)
        lift = tree.nodes.new('ShaderNodeVectorMath')
        lift.operation = 'ADD'
        lift.inputs[1].default_value = (0,1,0)
        tree.links.new(offset.outputs['Vector'], flip.inputs[0])
        tree.links.new(flip.outputs['Vector'], lift.inputs[0])
        tree.links.new((offset if os.environ.get('PBR_UV_RAW') == '1' else lift).outputs['Vector'], image.inputs['Vector'])
        tree.links.new(image.outputs['Color'], bsdf.inputs['Base Color'])
    elif group == 'emission':
        bsdf.inputs['Base Color'].default_value = (0,0,0,1)
        bsdf.inputs['Emission Strength'].default_value = 1.0
        if i < 5:
            colors = ((0,0,0),(.25,.25,.25),(.5,.5,.5),(1,1,1),(1,0,0))
            bsdf.inputs['Emission Color'].default_value = (*colors[i],1)
        else:
            tree.links.new(image_node(tree.nodes, 'pbr-test-color.png', 'sRGB').outputs['Color'], bsdf.inputs['Emission Color'])
    return material

for group in chosen:
    output_name = group + ('-raw-orientation' if group == 'uv' and os.environ.get('PBR_UV_RAW') == '1' else '')
    for i, sphere in enumerate(spheres):
        sphere.data.materials.clear()
        sphere.data.materials.append(build_material(group, i))
    # Save a distinct self-contained scene for each matrix, never touch other projects.
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(out / f'blender-{output_name}.blend'))
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '8'
    scene.render.filepath = str(out / f'blender-{output_name}.png')
    bpy.ops.render.render(write_still=True)
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '32'
    bpy.data.images['Render Result'].save_render(str(out / f'blender-{output_name}-linear.exr'), scene=scene)
    print('COMPARISON_DONE', group, flush=True)
