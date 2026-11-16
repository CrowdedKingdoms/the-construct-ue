import json
import math
import os
import unreal

root = '/Game/Construct/ModScripts'
tools = unreal.AssetToolsHelpers.get_asset_tools()
for suffix, unlit in [('Lit', False), ('Unlit', True)]:
    name = 'M_ConstructScriptOverlay' + suffix
    path = root + '/Materials/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        raise RuntimeError('Refusing to overwrite an existing material: ' + path)
    material = tools.create_asset(name, root + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('two_sided', True)
    if unlit:
        material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -240, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 1))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -240, 160)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 1.0)
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR if unlit else unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError('Material save failed: ' + path)

# A closed capsule surface, imported through Unreal's regular static-mesh importer.
folder = os.path.join(unreal.Paths.project_saved_dir(), 'ModScriptsUI')
os.makedirs(folder, exist_ok=True)
filename = os.path.abspath(os.path.join(folder, 'SM_ConstructScriptOverlayCapsule.obj'))
segments = 32
rings = []
for index in range(1, 9):
    angle = -math.pi / 2 + index * math.pi / 16
    rings.append((25 * math.cos(angle), -25 + 25 * math.sin(angle)))
for index in range(0, 8):
    angle = index * math.pi / 16
    rings.append((25 * math.cos(angle), 25 + 25 * math.sin(angle)))
vertices = [(0, 0, -50)]
for radius, z in rings:
    for segment in range(segments):
        angle = segment * 2 * math.pi / segments
        vertices.append((radius * math.cos(angle), radius * math.sin(angle), z))
top = len(vertices) + 1
vertices.append((0, 0, 50))
faces = []
for segment in range(segments):
    a, b = 2 + segment, 2 + (segment + 1) % segments
    faces.append((1, b, a))
for ring in range(len(rings) - 1):
    for segment in range(segments):
        a = 2 + ring * segments + segment
        b = 2 + ring * segments + (segment + 1) % segments
        c, d = a + segments, b + segments
        faces.extend([(a, b, d), (a, d, c)])
last = 2 + (len(rings) - 1) * segments
for segment in range(segments):
    faces.append((last + segment, last + (segment + 1) % segments, top))
with open(filename, 'w', encoding='utf8') as stream:
    stream.write('o ConstructScriptOverlayCapsule\ns 1\n')
    for vertex in vertices:
        stream.write('v %.9f %.9f %.9f\n' % vertex)
    for face in faces:
        stream.write('f %d %d %d\n' % face)

name = 'SM_ConstructScriptOverlayCapsule'
mesh_path = root + '/Meshes/' + name
if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
    raise RuntimeError('Refusing to overwrite an existing mesh: ' + mesh_path)
task = unreal.AssetImportTask()
task.set_editor_property('filename', filename)
task.set_editor_property('destination_path', root + '/Meshes')
task.set_editor_property('destination_name', name)
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', False)
task.set_editor_property('save', True)
options = unreal.FbxImportUI()
options.set_editor_property('automated_import_should_detect_type', False)
options.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
options.set_editor_property('import_materials', False)
options.set_editor_property('import_textures', False)
options.static_mesh_import_data.set_editor_property('generate_lightmap_u_vs', False)
options.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
task.set_editor_property('options', options)
task.set_editor_property('factory', unreal.FbxFactory())
tools.import_asset_tasks([task])
mesh = unreal.load_asset(mesh_path)
if not isinstance(mesh, unreal.StaticMesh):
    raise RuntimeError('Capsule mesh import failed')
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
with open(os.path.join(folder, 'assets.json'), 'w', encoding='utf8') as stream:
    json.dump({'materials': [root + '/Materials/M_ConstructScriptOverlayLit', root + '/Materials/M_ConstructScriptOverlayUnlit'], 'capsule': mesh_path, 'vertices': len(vertices), 'triangles': len(faces)}, stream, indent=2)
unreal.log('ConstructModScripts: overlay assets generated')
