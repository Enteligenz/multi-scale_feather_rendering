import os
import bmesh
import struct
import unicodedata
import mathutils
import math
import numpy as np

from .addon_preferences import get_prefs
from .registry import SceneRegistry
from .xml_node import XMLNode
from .materials import export_material, export_default_feather_spine_bsdf, export_default_feather_barbs_bsdf, export_default_feather_barbules_bsdf
from .feather_gen_generator import generate_feather
from .utils import str_flat_matrix, QuadTree, build_face_quadtree
from .feather_gen_bezier_and_vec3 import Vec3


def get_shape_name_base(obj, inst):
    modifiers = [mod.type for mod in obj.original.modifiers]
    has_nodes = 'NODES' in modifiers

    if has_nodes:
        # Not sure how to ensure shapes with nodes are handled as uniques
        # TODO: We better join them by material
        id = hex(inst.random_id).replace("0x", "").replace('-', 'M').upper()
        return f"{obj.name}_{id}"

    try:
        return f"{obj.data.name}-shape"
    except:
        return f"{obj.original.data.name}-shape"  # We use the original mesh name!


def _shape_name_material(name, mat_id):
    return f"_m_{mat_id}_{name}"

def _export_bmesh_by_material(registry: SceneRegistry, me) -> list[(str, str)]:
    mat_count = len(me.materials)
    shapes = []

    def _export_for_mat(mat_id, abs_filepath):
        from .ply import save_mesh as ply_save

        bm = bmesh.new()
        bm.from_mesh(me)

        # remove faces with other materials
        if mat_count > 1:
            for f in bm.faces:
                # Remove irrelevant faces
                # Special case: Assign invalid material indices to the last material 
                if f.material_index != mat_id and not ((f.material_index < 0 or f.material_index >= mat_count) and mat_id == mat_count-1):
                    bm.faces.remove(f)

        if len(bm.verts) == 0 or len(bm.faces) == 0 or not bm.is_valid:
            bm.free()
            return False

        # Make sure all faces are convex
        bmesh.ops.connect_verts_concave(bm, faces=bm.faces)
        bmesh.ops.triangulate(bm, faces=bm.faces)

        bm.normal_update()

        ply_save(
            filepath=abs_filepath,
            bm=bm,
            use_ascii=False,
            use_normals=True,
            use_uv=True,
            use_color=False
        )

        bm.free()
        return True
    
    if mat_count == 0:
        # special case if the mesh has no slots available
        mat_count = 1
    
    for mat_id in range(0, mat_count):
        shape_name = me.name if mat_count <= 1 else _shape_name_material(me.name, mat_id)
        rel_filepath = os.path.join(get_prefs().mesh_dir_name, shape_name + ".ply")
        abs_filepath = os.path.join(registry.path, rel_filepath)

        if os.path.exists(abs_filepath) and not registry.settings.overwrite_existing_meshes:
            # file is already exported
            pass
        elif _export_for_mat(mat_id, abs_filepath):
            # export successful
            pass
        else:
            # export failed
            continue
        
        shapes.append(rel_filepath.replace('\\', '/')) # Ensure the shape path is not using \ to keep the xml valid
    
    return shapes


# remove non-ascii characters by decomposing accented characters into accent and base character, and only keeping letters afterwards
def remove_accents(text: str):
    return ''.join(
        c for c in unicodedata.normalize('NFKD', text) if ord(c) < 128
    )

def create_paths(registry: SceneRegistry, shape_name: str):
    """Utility function to put together the relative and absolute path of an object."""
    shape_name = remove_accents(shape_name)
    rel_filepath = os.path.join(get_prefs().mesh_dir_name, shape_name + ".ply")
    rel_filepath = rel_filepath.replace('\\', '/') # ensure the shape path is not using \ to keep the xml valid
    abs_filepath = os.path.join(registry.path, rel_filepath)
    return (rel_filepath, abs_filepath)

# code taken from https://github.com/qerrant/BezierBlenderToUE/blob/main/ExportBezierToUE.py (though modified a bit)
def export_curve(registry: SceneRegistry, obj):
    beziers = [] # this will collect all bezier curves for the .ply file
                        
    for subcurve in obj.data.splines:
        if subcurve.type == 'BEZIER':
            beziers.append(subcurve)

    if len(beziers) > 0:
        count_beziers = 0 # how many bezier curves are in this obj (with a bezier being one continuous curve)
        segments = []
    
        for bezier in beziers:
            count_beziers += 1
            count_segments = 0 # how many segments this bezier curve has
            bezier_points = bezier.bezier_points
            num_points = len(bezier_points)
            
            for i in range(num_points - 1):  # iterate over segments
                w0 = bezier_points[i].co
                w1 = bezier_points[i].handle_right
                w2 = bezier_points[i + 1].handle_left
                w3 = bezier_points[i + 1].co
                
                segments.append((w0.x, w0.y, w0.z, w1.x, w1.y, w1.z, w2.x, w2.y, w2.z, w3.x, w3.y, w3.z))
                count_segments += 1

            if bezier.use_cyclic_u:  # if cyclic, connect last to first
                w0 = bezier_points[-1].co
                w1 = bezier_points[-1].handle_right
                w2 = bezier_points[0].handle_left
                w3 = bezier_points[0].co
                
                segments.append((w0.x, w0.y, w0.z, w1.x, w1.y, w1.z, w2.x, w2.y, w2.z, w3.x, w3.y, w3.z))
                count_segments += 1

        rel_filepath, abs_filepath = create_paths(registry, obj.name)
        

        if os.path.exists(abs_filepath) and not registry.settings.overwrite_existing_meshes:
            return rel_filepath

        with open(abs_filepath, "wb") as text_file:
            # write .ply header in ASCII
            header = f"""ply
format binary_little_endian 1.0
element num_beziers {count_beziers}
property float root_normal_x
property float root_normal_y
property float root_normal_z
element num_segments {count_segments}
property float w0x
property float w0y
property float w0z
property float w1x
property float w1y
property float w1z
property float w2x
property float w2y
property float w2z
property float w3x
property float w3y
property float w3z
end_header
"""
            text_file.write(header.encode('utf-8'))  # write header as bytes

            # write dummy normal data
            for bezier in beziers:
                text_file.write(struct.pack("<3f", 0.0, 0.0, 0.0))

            # write segment data in binary format (float32)
            for seg in segments:
                text_file.write(struct.pack("<12f", *seg))  # little-endian 12 float values per segment

        return rel_filepath
    
def calculate_strand_tangents(points):
    """
    Calculate tangent vectors for each point in a hair strand to ensure curve continuity.
    Uses Catmull-Rom spline tangent calculation for smooth interpolation.
    """
    tangents = []

    # for first point, use forward difference
    if len(points) > 1:
        tangent = (points[1] - points[0]).normalized()
        tangents.append(tangent)

    # for middle points, use central difference
    for i in range(1, len(points) - 1):
        tangent = (points[i+1] - points[i-1]).normalized()
        tangents.append(tangent)

    # for last points, use backward difference
    if len(points) > 1:
        tangent = (points[-1] - points[-2]).normalized()
        tangents.append(tangent)

    return tangents

def export_hair_as_curve(registry: SceneRegistry, ps, me, name):
    """
    Converts given particle system (hair) from Blender into a collection of bezier curves.
    These curves are then saved in a binary .ply file.
    """
    # build quadtree for fast face (normals) lookup
    quadtree, face_centers, face_normals = build_face_quadtree(me)

    beziers = [] # this will collect all bezier curves for the .ply file
    num_skipped = 0
    count_beziers = 0 # how many bezier curves are in this obj (with a bezier being one continuous curve)
    
    hair = ps.particles
    is_first = True
    last_count_segments = -1
    for strand in hair:
        points = [key.co for key in strand.hair_keys]
        count_segments = len(points) - 1 # how many segments this bezier curve has

        # Handle hair within particle system having different numbers of segments
        if (not is_first) and (count_segments != last_count_segments):
            print(f"WARNING: Hairs in this particle system do not seem to have a consistent number of segments! \
                  The last hair had {last_count_segments} segments, the current one had {count_segments}.")
            return None
        last_count_segments = count_segments

        if len(points) < 2:
            num_skipped += 1
            continue # skip hair that is too short

        # get surface normal at hair root
        surface_normal = mathutils.Vector((0, 0, 1)) # default fallback
        
        try:
            if hasattr(strand, "num") and strand.num < len(me.polygons):
                surface_normal = face_normals[strand.num]
            elif quadtree is not None:
                root_pos = points[0]
                closest_face_idx, _ = quadtree.find_nearest(
                    (root_pos.x, root_pos.y, root_pos.z),
                    face_centers
                )
                if closest_face_idx is not None:
                    surface_normal = face_normals[closest_face_idx]
        except (AttributeError, IndexError): # keep default normal if we can't determine the surface normal
            pass

        count_beziers += 1
        bezier_segment = fit_single_cubic_bezier(points)

        beziers.append({
            "spline": [bezier_segment],
            "root_normal": surface_normal
        })

    if not beziers:
        return None

    # if os.path.exists(abs_filepath) and not registry.settings.overwrite_existing_meshes:
    #     return rel_filepath

    rel_filepath, abs_filepath = create_paths(registry, name + "_hair")

    with open(abs_filepath, "wb") as text_file:
        # write .ply header in ASCII
        header = f"""ply
format binary_little_endian 1.0
element num_beziers {count_beziers}
property float root_normal_x
property float root_normal_y
property float root_normal_z
element num_segments {1}
property float w0x
property float w0y
property float w0z
property float w1x
property float w1y
property float w1z
property float w2x
property float w2y
property float w2z
property float w3x
property float w3y
property float w3z
end_header
"""
        # changed count_segments in second curly braces to number
        text_file.write(header.encode('utf-8'))  # write header as bytes

        # write data in binary format (float32)
        # write all normal data
        for bezier in beziers:
            normal = bezier["root_normal"]
            text_file.write(struct.pack("<3f", normal.x, normal.y, normal.z))

        # write all segment data
        for bezier in beziers:
            for seg in bezier["spline"]:
                text_file.write(struct.pack("<12f", *seg))  # little-endian 12 float values per segment

        if num_skipped > 0: print(f"Skipped {num_skipped} hairs that were too short (<2 segments).")

        print(f"Exported {count_beziers} Bézier curves with {count_segments} segments each.")

        return rel_filepath

def fit_single_cubic_bezier(points):
    """
    Fits a single cubic Bézier curve to a sequence of points using least-squares optimization.
    
    A cubic Bézier is defined as B(t) = (1-t)³w0 + 3(1-t)²t w1 + 3(1-t)t² w2 + t³ w3
    where w0 and w3 are fixed as the first and last points.
    
    We solve for w1 and w2 by minimizing the squared distance from intermediate points
    to the curve, assuming they are evenly spaced in parameter space.
    
    Returns tuple: (w0x, w0y, w0z, w1x, w1y, w1z, w2x, w2y, w2z, w3x, w3y, w3z)
    """
    if len(points) < 2:
        # Degenerate case
        print(f"WARNING: Hair strand with only {len(points)} point(s)") #
        p = points[0]
        return (p.x, p.y, p.z, p.x, p.y, p.z, p.x, p.y, p.z, p.x, p.y, p.z)
    
    # Endpoints are fixed
    w0 = points[0]
    w3 = points[-1]

    # Debug: check for zero-length strands #
    if (w3 - w0).length < 1e-6: #
        print(f"WARNING: Zero-length hair strand at {w0}") #
    
    if len(points) == 2:
        # Only two points - use simple tangent-based approach
        chord = w3 - w0
        w1 = w0 + chord / 3.0
        w2 = w0 + chord * 2.0 / 3.0
        return (
            w0.x, w0.y, w0.z,
            w1.x, w1.y, w1.z,
            w2.x, w2.y, w2.z,
            w3.x, w3.y, w3.z
        )
    
    # For 3+ points, use least-squares fitting
    n = len(points)
    
    # Assign parameter values to each point (evenly spaced in [0,1])
    # Could also use chord-length parameterization for better results
    t_values = np.linspace(0, 1, n)
    
    # Build the system of equations
    # For each intermediate point i with parameter t_i:
    # point_i = (1-t)³w0 + 3(1-t)²t w1 + 3(1-t)t² w2 + t³ w3
    # Rearranging: point_i - (1-t)³w0 - t³w3 = 3(1-t)²t w1 + 3(1-t)t² w2
    
    A = []  # Coefficient matrix
    b_x, b_y, b_z = [], [], []  # Right-hand side for each dimension
    
    for i in range(1, n - 1):  # Skip first and last points (they're fixed)
        t = t_values[i]
        
        # Bernstein polynomial basis functions
        b0 = (1 - t) ** 3
        b1 = 3 * (1 - t) ** 2 * t
        b2 = 3 * (1 - t) * t ** 2
        b3 = t ** 3
        
        # Coefficients for w1 and w2
        A.append([b1, b2])
        
        # Right-hand side: target_point - contributions from fixed endpoints
        target = points[i]
        rhs_x = target.x - b0 * w0.x - b3 * w3.x
        rhs_y = target.y - b0 * w0.y - b3 * w3.y
        rhs_z = target.z - b0 * w0.z - b3 * w3.z
        
        b_x.append(rhs_x)
        b_y.append(rhs_y)
        b_z.append(rhs_z)
    
    A = np.array(A)
    b_x = np.array(b_x)
    b_y = np.array(b_y)
    b_z = np.array(b_z)
    
    # Solve least-squares: minimize ||A * [w1, w2]^T - b||²
    try:
        # Solve for each dimension separately
        solution_x, _, _, _ = np.linalg.lstsq(A, b_x, rcond=None)
        solution_y, _, _, _ = np.linalg.lstsq(A, b_y, rcond=None)
        solution_z, _, _, _ = np.linalg.lstsq(A, b_z, rcond=None)
        
        w1 = mathutils.Vector((solution_x[0], solution_y[0], solution_z[0]))
        w2 = mathutils.Vector((solution_x[1], solution_y[1], solution_z[1]))

        # Debug: check for NaN or Inf #
        if not all(np.isfinite([w1.x, w1.y, w1.z, w2.x, w2.y, w2.z])): #
            print(f"WARNING: Got NaN/Inf in control points, using fallback") #
            raise ValueError("Invalid control points") #
        
    except np.linalg.LinAlgError:
        # Fallback to simple method if solving fails
        print(f"WARNING: Least-squares failed ({e}), using fallback method") #
        chord = w3 - w0
        w1 = w0 + chord / 3.0
        w2 = w0 + chord * 2.0 / 3.0

    result = (
        w0.x, w0.y, w0.z,
        w1.x, w1.y, w1.z,
        w2.x, w2.y, w2.z,
        w3.x, w3.y, w3.z
    )

    if all(v == 0 for v in result):
        print(f"ERROR: Got all-zero curve! w0={w0}, w3={w3}, points={len(points)}")
    
    return result
    
def create_matrix_from_pos_dir(pos, dir):
    """Create a 4x4 matrix from a position and direction vector."""
    z_axis = mathutils.Vector((dir.x, dir.y, dir.z))

    # generate x and y axes that are perpendicular to z axis
    if abs(z_axis.x) < 0.9:
        ref_vec = mathutils.Vector((1, 0, 0))
    else:
        ref_vec = mathutils.Vector((0, 1, 0))
    x_axis = z_axis.cross(ref_vec).normalized()
    y_axis = z_axis.cross(x_axis).normalized()

    # create rotation matrix from these axes
    rotation_matrix = mathutils.Matrix((
        (x_axis.x, y_axis.x, z_axis.x, 0),
        (x_axis.y, y_axis.y, z_axis.y, 0),
        (x_axis.z, y_axis.z, z_axis.z, 0),
        (0, 0, 0, 1)
    ))

    # create translation matrix
    translation_matrix = mathutils.Matrix.Translation((pos.x, pos.y, pos.z))

    # combine matrices
    transform_matrix = translation_matrix @ rotation_matrix
    return transform_matrix
    
def export_as_feathers(registry: SceneRegistry, obj):
    """
    Converts a given obj that has hair on it from Blender into a collection of bezier curves (that portray feathers).
    A feather generation method will be used to replace all hairs with feathers.
    Only a few feathers (see below parameter) will actually be generated, all others will be instanced from those.
    The generated curves are then saved in a binary .ply file.
    WARNING: Multiple particle systems will be merged.
    """
    num_generated = 10
    # radius parameters
    root_spine = "0.005"
    tip_spine = "0.0001"
    root_barbs = "0.0005"
    tip_barbs = "0.0001"
    root_barbules = "0.0001"
    tip_barbules = "0.00001"
    # returned t: 0.0001 (smaller than Epsilon = 0.0001) error? [error]   your intersection is susceptible to self-intersections

    # check for hair particle system
    hair_systems = [ps for ps in obj.particle_systems if ps.settings.type == 'HAIR']

    # 1. find feather positions and directions
    feathers = [] # this will collect all feather positions and directions
    num_skipped = 0
    for ps in hair_systems:
        hair = ps.particles
        for strand in hair:
            points = [key.co for key in strand.hair_keys]

            if len(points) < 2:
                num_skipped += 1
                continue # skip hair that is too short

            # feather position and direction
            feather_pos = Vec3(points[0].x, points[0].y, points[0].z)
            end_point = Vec3(points[-1].x, points[-1].y, points[-1].z)
            feather_dir = (end_point - feather_pos)
            feather_dir = feather_dir.normalize()
            feathers.append((feather_pos, feather_dir))
    
    if num_skipped > 0: print(f"Skipped {num_skipped} hairs that were too short (<2 segments).")

    # 2. generate a few feathers
    feather_instances = [] # will have length of 3 * num_generated after this loop due to feather components being saved separately
    shape_ids = [] # stores tuples of (spine_id, barbs_id, barbules_id) for reference
    for i in range(num_generated):
        # generate new feather
        rel_path_spine, rel_path_barbs, rel_path_barbules = generate_feather(registry, id_suffix=f"_{i}", use_convergence=True)

        # create shape ids that will be used consistently
        spine_id = registry._make_unique_name(f"feather_spine_{i}")
        barbs_id = registry._make_unique_name(f"feather_barbs_{i}")
        barbules_id = registry._make_unique_name(f"feather_barbules_{i}")
        shape_ids.append((spine_id, barbs_id, barbules_id))

        # create shape nodes with ids
        shape_node_spine = XMLNode("shape", id=spine_id, type="curve", filename=rel_path_spine, rootradius=root_spine, tipradius=tip_spine)
        shape_node_barbs = XMLNode("shape", id=barbs_id, type="curve", filename=rel_path_barbs, rootradius=root_barbs, tipradius=tip_barbs)
        shape_node_barbules = XMLNode("shape", id=barbules_id, type="curve", filename=rel_path_barbules, rootradius=root_barbules, tipradius=tip_barbules, curvesplits=4)

        # 3. insert generated feather into one of the hair positions
        # create instance nodes
        instance_node_spine = XMLNode("instance")
        instance_node_barbs = XMLNode("instance")
        instance_node_barbules = XMLNode("instance")

        # add materials
        instance_node_spine.add_children(export_default_feather_spine_bsdf())
        instance_node_barbs.add_children(export_default_feather_barbs_bsdf())
        instance_node_barbules.add_children(export_default_feather_barbules_bsdf())

        # add shape to instance
        instance_node_spine.add_child(shape_node_spine)
        instance_node_barbs.add_child(shape_node_barbs)
        instance_node_barbules.add_child(shape_node_barbules)

        # create and add transforms based on hair position and direction
        instance_matrix = create_matrix_from_pos_dir(*feathers[i])
        instance_node_spine.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))
        instance_node_barbs.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))
        instance_node_barbules.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))

        # store shape nodes
        feather_instances.extend([instance_node_spine, instance_node_barbs, instance_node_barbules])

    # 4. create instances
    for i in range(num_generated, len(feathers)):
        base_idx = i % num_generated
        spine_id, barbs_id, barbules_id = shape_ids[base_idx]
        
        # create reference nodes using the stored shape ids
        ref_node_spine = XMLNode("ref", id=spine_id)
        ref_node_barbs = XMLNode("ref", id=barbs_id)
        ref_node_barbules = XMLNode("ref", id=barbules_id)

        # create instance nodes
        instance_node_spine = XMLNode("instance")
        instance_node_barbs = XMLNode("instance")
        instance_node_barbules = XMLNode("instance")

        # add materials
        instance_node_spine.add_children(export_default_feather_spine_bsdf())
        instance_node_barbs.add_children(export_default_feather_barbs_bsdf())
        instance_node_barbules.add_children(export_default_feather_barbules_bsdf())

        # add references to instance
        instance_node_spine.add_child(ref_node_spine)
        instance_node_barbs.add_child(ref_node_barbs)
        instance_node_barbules.add_child(ref_node_barbules)

        # create and add transforms based on hair position and direction
        instance_matrix = create_matrix_from_pos_dir(*feathers[i])
        instance_node_spine.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))
        instance_node_barbs.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))
        instance_node_barbules.add("transform").add("matrix", value=str_flat_matrix(instance_matrix))

        # store shape nodes
        feather_instances.extend([instance_node_spine, instance_node_barbs, instance_node_barbules])

    # if not beziers:
    #     return None

    # if os.path.exists(abs_filepath) and not registry.settings.overwrite_existing_meshes:
    #     return rel_filepath

    print(f"len(feather_instances): {len(feather_instances)}")
    print("Finished feather conversion!")

    return feather_instances


def export_shape(registry: SceneRegistry, obj) -> list[XMLNode]:
    # This is not possible currently, as access to `mesh_get_eval_final` (COLLADA) is not available
    # nor is it possible to setup via dependency graph, see https://devtalk.blender.org/t/get-render-dependency-graph/12164
    if obj.type in {'CURVE'}: # treat curves separately from other types since we don't want to use a mesh here
        filepath = export_curve(registry, obj)
        root_radius = obj.data.bevel_depth
        tip_radius = root_radius
        return [ XMLNode("shape", type="curve", filename=filepath, rootradius=root_radius, tipradius=tip_radius) ]
    else:
        all_nodes = []
        try:
            me = obj.to_mesh(preserve_all_data_layers=False, depsgraph=registry.depsgraph)
        except RuntimeError as e:
            registry.error(f"Could not convert to mesh: {str(e)}")
            return []

        if obj.type == 'MESH' and any(ps.settings.type == 'HAIR' for ps in obj.particle_systems):
            if registry.settings.generate_feathers: # Generate feathers
                print("Generating feathers.")
                all_nodes = export_as_feathers(registry, obj)
            else: # Export as curves
                print("Exporting as curves.")
                root_radius = "0.01"
                tip_radius = "0.0001"

                # check for hair particle systems
                hair_systems = [ps for ps in obj.particle_systems if ps.settings.type == 'HAIR']

                filepaths = []
                for ps in hair_systems:
                    filepaths.append(export_hair_as_curve(registry, ps, me, registry._make_unique_name(obj.name)))
                    # filepath = export_hair_as_curve(registry, ps, me, obj.name)
                if registry.settings.export_curves_as_feathers:
                    for p in filepaths:
                        hair_id = registry._make_unique_name(f"feathersobj")
                        all_nodes.extend([ XMLNode("shape", id=hair_id, type="feathers", filename=p, lod=True, thresholdLOD="0.5", numOriginalFeathers="2", planeFindingMode="pca", directionDerivativePos="0") ])
                else:
                    for p in filepaths:
                        hair_id = registry._make_unique_name(f"curveobj")
                        all_nodes.extend([ XMLNode("shape", id=hair_id, type="curve", filename=p, rootradius=root_radius, tipradius=tip_radius) ])

        shapes = _export_bmesh_by_material(registry, me)
        obj.to_mesh_clear()

        all_nodes.extend([ XMLNode("shape", type="mesh", filename=filepath) for filepath in shapes ])

        return all_nodes

