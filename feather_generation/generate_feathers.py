import math
import random
from typing import List, Tuple, Optional

from bezier_and_vec3 import *
from utils import *

# DEFAULTS
SPINE_ROOT_RADIUS = 0.005
BARB_ROOT_RADIUS = 0.0005
BARBULE_ROOT_RADIUS = 0.0001
    
def create_spine_curve(length: float = 10.0, curvature: float = 0.3,
                       w0_offset: float = 0.0) -> BezierCurve:
    """Create the main spine of the feather as a single bezier curve."""
    w0 = Vec3(w0_offset, 0.0, 0.0)
    w3 = Vec3(w0.x, length, w0.z)

    # calculate middle control points with some randomness
    w1_x = w0_offset + length * curvature * random.uniform(0.5, 1.0)
    w1_y = length * 0.25
    w1_z = length * curvature * 0.1 * random.uniform(0, 0.5)
    w1 = Vec3(w1_x, w1_y, w1_z)
    
    w2_x = w0_offset + length * curvature * random.uniform(0.7, 1.2)
    w2_y = length * 0.75
    w2_z = length * curvature * 0.1 * random.uniform(-0.1, 0.1)
    w2 = Vec3(w2_x, w2_y, w2_z)
    
    # create the Bezier curve
    return BezierCurve(w0, w1, w2, w3)

def create_barb(start: Vec3, direction: Vec3, spine_direction: Vec3,
                length: float, droop: float = 0.2, curve_strength: float = 0.4,
                convergence_point: Vec3 = None, convergence_factor: float = 0.0) -> BezierCurve:
    """Create a single barb (secondary feather) as a bezier curve.
    Optional convergence parameters allow barbs to gradually bend toward a specific point."""
    # normalize direction
    direction = direction.normalize()
    spine_direction = spine_direction.normalize()

    # create gravity vector for droop effect
    gravity = Vec3(0, -1, 0)

    w0 = start
    end_direction = direction * 0.5 + spine_direction * (curve_strength * 0.5)
    end_direction = end_direction + gravity * droop
    end_direction = end_direction.normalize()
    w3 = w0 + end_direction * length

    # w1 stays closer to original direction for straighter start
    w1_length = length * 0.6
    w1 = w0 + direction * w1_length
    
    # w2 pulls toward the curved end with increased influence, for sharper curve
    w2_length = length * 0.15
    curve_direction = direction * 0.1 + spine_direction * (curve_strength * 0.9)
    curve_direction = curve_direction.normalize()
    w2 = w3 - curve_direction * w2_length

    # if we want to do convergence
    if convergence_point is not None and convergence_factor > 0.0:
        # calculate vector from start to convergence point
        to_convergence = convergence_point - start
        to_convergence_dir = to_convergence.normalize()
        
        # calculate w3 first, it has strongest influence on overall direction
        w3_blend = 0.8 * convergence_factor
        scaled_convergence = to_convergence.normalize() * length
        convergent_w3 = w0 + scaled_convergence
        w3 = w3 * (1.0 - w3_blend) + convergent_w3 * w3_blend

        # w1 handle: apply minimal influence
        w1_blend = 0.2 * convergence_factor
        convergent_w1_dir = (direction * 0.7 + to_convergence_dir * 0.3).normalize()
        convergent_w1 = w0 + convergent_w1_dir * w1_length
        w1 = w1 * (1.0 - w1_blend) + convergent_w1 * w1_blend

        # for w2 handle: create smooth curve that leads to w3
        w2_blend = 0.6 * convergence_factor
        convergent_curve_dir = (curve_direction * 0.5 + to_convergence_dir * 0.5).normalize()
        convergent_w2 = w3 - convergent_curve_dir * w2_length * 0.8
        w2 = w2 * (1.0 - w2_blend) + convergent_w2 * w2_blend

    return BezierCurve(w0, w1, w2, w3)

def create_barbule(start: Vec3, direction: Vec3, length: float,
                   waviness: float = 0.08) -> BezierCurve:
    """Create a single barbule (tertiary feather) as a bezier curve."""
    # add small random variation to direction
    direction = direction.normalize()
    random_deflection = Vec3(
        random.uniform(-waviness, waviness),
        random.uniform(-waviness, waviness),
        random.uniform(-waviness, waviness)
    )
    direction = (direction + random_deflection).normalize()

    w0 = start
    # vary length slightly
    actual_length = add_random_variation(length, 0.2)
    w3 = w0 + direction * actual_length

    # handle length should also vary a bit
    handle_length = actual_length / 3 * add_random_variation(1.0, 0.15)

    # small random offsets for control points
    ctrl1_random = Vec3(
        random.uniform(-0.03, 0.03) * actual_length,
        random.uniform(-0.03, 0.03) * actual_length,
        random.uniform(-0.03, 0.03) * actual_length
    )
    ctrl2_random = Vec3(
        random.uniform(-0.03, 0.03) * actual_length,
        random.uniform(-0.03, 0.03) * actual_length,
        random.uniform(-0.03, 0.03) * actual_length
    )

    # control points along the direction
    w1 = w0 + direction * handle_length + ctrl1_random
    w2 = w3 - direction * handle_length + ctrl2_random

    return BezierCurve(w0, w1, w2, w3)

def add_barbules_to_barb(k, barbule_count, barb, up_vector, barbule_side, barbule_length):
    # position along barb (parametric)
    t_barbule = (k + 1) / (barbule_count + 1)

    # cubic bezier interpolation for barbule position
    u = t_barbule
    barbule_pos = Vec3(
        (1-u)**3 * barb.w0.x + 3*(1-u)**2*u * barb.w1.x + 3*(1-u)*u**2 * barb.w2.x + u**3 * barb.w3.x,
        (1-u)**3 * barb.w0.y + 3*(1-u)**2*u * barb.w1.y + 3*(1-u)*u**2 * barb.w2.y + u**3 * barb.w3.y,
        (1-u)**3 * barb.w0.z + 3*(1-u)**2*u * barb.w1.z + 3*(1-u)*u**2 * barb.w2.z + u**3 * barb.w3.z
    )

    # calculate tangent at the barbule position
    local_barb_dir = get_spine_tangent_at(barb, u)

    # get perpendicular direction to the barb
    barbule_perp_dir = local_barb_dir.cross(up_vector).normalize() * barbule_side

    base_angle_blend = 0.7
    late_tip_threshold = 0.9 # only increase angle after 90% of barb length

    # barb angle formula
    additional_angle = 0
    if u > late_tip_threshold:
        u_normalized = (u - late_tip_threshold) / (1.0 - late_tip_threshold)
        additional_angle = 0.2 * (1.0 / (1.0 + math.exp(-15 * (u_normalized - 0.6))))
        # additional_angle = 0.2 * (1.0 / (1.0 + math.exp(-12 * (u_normalized - 0.5)))) # use this for a narrower angle

    angle_blend = min(0.9, base_angle_blend + additional_angle)
    barbule_dir = (barbule_perp_dir * (1 - angle_blend) + local_barb_dir * angle_blend).normalize()

    # current barbule length; shorter near tip
    # linear
    # current_barbule_length = barbule_length * (1 - t_barbule * 0.3)
    # sigmoid
    midPoint = 0.85 # where the transition happens (0.85 = 85% along the barbule)
    sharpness = 8.0 # higher value = sharper transition
    tSigmoid = 1.0 / (1.0 + math.exp(sharpness * (midPoint - t_barbule)))
    current_barbule_length = barbule_length + tSigmoid * (-barbule_length / 2) # towards the end, barbule length halves

    # create barbule curve with the angled direction
    return create_barbule(barbule_pos, barbule_dir, current_barbule_length)

def generate_feather(output_path: str,
                     num_feathers: int = 1,
                     spine_length: float = 2.0,
                     barb_length: float = 0.3,
                     barbule_length: float = 0.008,
                     barb_spacing: float = 0.008,
                     barbule_density: float = 1.0, # at 1.0, barbules cover 1/3 of the barb space (increasing this means increasing barbule count)
                     spine_root_radius: float = SPINE_ROOT_RADIUS,
                     barb_root_radius: float = BARB_ROOT_RADIUS,
                     barbule_root_radius: float = BARBULE_ROOT_RADIUS,
                     asymmetry: float = -0.5, # controls left-right asymmetry
                     shape_type: str = "broad", # normal
                     group_probability: float = 0.15, # probability of starting a new group
                     group_direction_variance: float = 0.3, # how much group direction can vary
                     use_convergence: bool = False, # whether grouped barbs converge or not
                     random_seed: int = None):
    """Generate a complete feather structure and write to .ply file."""
    # TODO IDEAS:
    # - add curling for each side, treated separately per side and optional
    # - increase group direction variance for base of feather
    # - maybe just let user have more control over areas and how they are treated
    # - spine hat auf unterseite eine kerbe
    # - multiple segments for barbs
    # - maybe allow both convergence and normal grouping at the same time, with some randomness/ratio

    if random_seed is not None:
        random.seed(random_seed)
    else:
        random.seed()

    # shape type modifications
    shape_factors = {
        "normal": {"spine_curvature": 0.2, "barb_angle_blend": 0.4, "barb_curve_strength": 0.4},
        "pointed": {"spine_curvature": 0.15, "barb_angle_blend": 0.6, "barb_curve_strength": 0.4},
        "round": {"spine_curvature": 0.25, "barb_angle_blend": 0.4, "barb_curve_strength": 0.4},
        "broad": {"spine_curvature": 0.3, "barb_angle_blend": 0.3, "barb_curve_strength": 0.4}, # TODO ref photo 2025-04-30 15-44-13
        "narrow": {"spine_curvature": 0.1, "barb_angle_blend": 0.7, "barb_curve_strength": 0.4}
    }
    if shape_type not in shape_factors: # use defaults if type not recognized
        shape_type = "normal"

    spine_curvature = shape_factors[shape_type]["spine_curvature"]
    barb_angle_blend = shape_factors[shape_type]["barb_angle_blend"]
    barb_curve_strength = shape_factors[shape_type]["barb_curve_strength"]

    barbule_count_modifier = (6 * BARBULE_ROOT_RADIUS)
    
    # save each feather component type separately
    all_spines = []
    all_barbs = []
    all_barbules = []

    for i in range(num_feathers):
        # 1. create the main spine
        spine_curve = create_spine_curve(length=spine_length, curvature=spine_curvature,
                                        w0_offset=i*0.5)
        all_spines.append(spine_curve)

        # 2. create barbs along the spine
        # calculate perpendicular direction to the spine (assuming spine is mostly along y)
        up_vector = Vec3(0, 0, 1)
        total_spine_length = calculate_bezier_arc_length(spine_curve)

        # create barbs on both sides
        for side in [-1, 1]:
            current_t = 0

            # variables for managing barb groups
            in_group = False
            group_direction_offset = Vec3(0, 0, 0)
            group_size = 0
            max_group_size = 0
            group_convergence_point = None

            while current_t < 1.0:
                u = current_t

                # get position and tangent at point u
                barb_pos = interpolate_bezier(spine_curve, u)
                spine_dir = get_spine_tangent_at(spine_curve, u)

                # barb length formula
                # base_profile = math.sin(u * math.pi) # basic bell curve 0 -> 1 -> 0
                base_profile = math.pow(math.sin(u * math.pi), 0.7)
                sigmoid_tip = 1.0 / (1.0 + math.exp((u - 0.85) * 15)) # sharp drop-off after u=0.85
                length_profile = base_profile * (0.7 + 0.3 * sigmoid_tip)

                # apply shape type modifications
                if shape_type == "pointed":
                    # sharper taper toward tip
                    length_profile *= (1.0 - 0.5 * (u ** 1.5))
                elif shape_type == "round":
                    # more rounded, fuller shape
                    length_profile *= (1.0 - 0.4 * ((u - 0.5) ** 2))
                elif shape_type == "broad":
                    # fuller middle section
                    length_profile *= (1.0 - 0.3 * ((u - 0.4) ** 2))
                elif shape_type == "narrow":
                    # more elongated shape
                    length_profile *= (1.0 - 0.5 * ((u - 0.3) ** 2))

                # ensure minimum length (prevent zero-length barbs), which is smaller at tip
                min_length_factor = 0.05 * (1.0 - 0.8 * (u ** 2))  # gets smaller toward tip but never zero
                length_profile = max(length_profile, min_length_factor)

                # apply asymmetry
                side_factor = 1.0 + (asymmetry * side)
                current_barb_length = barb_length * length_profile * side_factor

                # barb angle formula
                perp_dir = spine_dir.cross(up_vector).normalize() * side
                base_angle_progression = 0.1 + 0.3 * (u ** 1.5) # starts more perpendicular, gradually angles forward
                tip_angle_factor = 1.0 / (1.0 + math.exp(-(u - 0.85) * 12)) # sigmoid centered at u=0.85
                tip_angle_adjustment = 0.5 * tip_angle_factor # maximum 0.5 additional forward angle

                # combined blend factor
                angle_blend = barb_angle_blend + base_angle_progression + tip_angle_adjustment
                angle_blend = min(0.8, angle_blend) # cap at some angle

                # blend perpendicular with forward (spine) direction
                barb_dir = (perp_dir * (1 - angle_blend) + 
                            spine_dir * angle_blend).normalize()

                # calculate group sizes based on position along spine
                if u < 0.3: # base region
                    base_group_size = int(2 + 3 * (u / 0.3)) # gradually increase from 2-5
                elif u > 0.7: # tip region
                    tip_progress = (u - 0.7) / 0.3 # 0 to 1 across tip region
                    base_group_size = int(9 - 4 * tip_progress) # gradually decrease from 9-5
                else: # middle region
                    middle_progress = (u - 0.3) / 0.4 # 0 to 1 across middle region
                    base_group_size = int(10 + 10 * math.sin(middle_progress * math.pi/2)) # peak at ~25?

                # decide if we want to start a new group
                if not in_group:
                    # modify group probability based on position
                    # higher at base, moderate in middle, lower at tip
                    if u < 0.3:
                        actual_group_prob = group_probability * 1.7 * (1.0 - u/0.5)
                    elif u > 0.7:
                        actual_group_prob = group_probability * 0.7
                    else:
                        actual_group_prob = group_probability

                    if random.random() < actual_group_prob:
                        in_group = True

                        # add some randomness to group size within appropriate ranges
                        variation = random.uniform(0.8, 1.2)
                        group_size = max(2, int(base_group_size * variation))
                        max_group_size = group_size

                        # determine group size based on position
                        group_size = random.randint(max(2, base_group_size // 2), base_group_size)
                        max_group_size = group_size

                        # generate random offset direction for this group; more variation in the middle
                        position_factor = 1 - 2 * abs(u - 0.5) # 0 at ends, 1 in the middle
                        variation_scale = group_direction_variance * position_factor

                        # direction offset is perpendicular to spine direction
                        perp_dir = spine_dir.cross(up_vector).normalize()
                        forward_offset = random.uniform(-0.1, 0.4) * variation_scale
                        side_offset = random.uniform(-0.2, 0.2) * variation_scale

                        group_direction_offset = (spine_dir * forward_offset +
                                                perp_dir * side_offset * side)
                        
                        if use_convergence:
                            # create convergence point for this group
                            # first estimate the forward range this group will span
                            group_span_t = (group_size * barb_spacing) / total_spine_length

                            # calculate a position somewhat ahead of group
                            convergence_t = min(1.0, current_t + group_span_t * random.uniform(2.0, 3.0))
                            convergence_spine_pos = interpolate_bezier(spine_curve, convergence_t)
                            convergence_spine_dir = get_spine_tangent_at(spine_curve, convergence_t)

                            convergence_perp_dir = convergence_spine_dir.cross(Vec3(0, 0, 1)).normalize() * side

                            # position convergence outward from spine, use current barb length as reference
                            outward_distance = current_barb_length * random.uniform(0.6, 0.9)

                            # add slight forward effect
                            forward_offset = convergence_spine_dir * (current_barb_length * random.uniform(0.8, 1.2))

                            # combine to get final convergence position
                            group_convergence_point = convergence_spine_pos + convergence_perp_dir * outward_distance + forward_offset

                # vars for convergence
                convergence_point = None
                convergence_factor = 0.0

                # reduce group size counter if in a group and end it if necessary
                if in_group:
                    group_size -= 1
                    if group_size <= 0:
                        in_group = False

                    # apply group direction offset if in a group
                    # group_position = 1.0 - group_size / max_group_size # old formula, less powerful gaps
                    group_position = max(0.4, 1.0 - (group_size / max_group_size) * 0.6) # new formula, more powerful gaps
                    blend_factor = math.sin(group_position * math.pi) * 0.8
                    barb_dir = (barb_dir + group_direction_offset * blend_factor).normalize()

                    if use_convergence:
                        # apply convergence for this group
                        convergence_point = group_convergence_point
                        base_strength = 0.8
                        # calculate convergence factor based on group position
                        curve_factor = 0.5 + 0.5 * math.pow(group_position, 0.6)
                        position_scale = 0.8 + 0.4 * min(1.0, u * 1.5)
                        convergence_factor = base_strength * curve_factor * position_scale
                        convergence_factor *= random.uniform(0.95, 1.05)

                # create the barb curve
                barb = create_barb(barb_pos, barb_dir, spine_dir, current_barb_length, droop=0.2,
                                curve_strength=barb_curve_strength,
                                convergence_point=convergence_point,
                                convergence_factor=convergence_factor)
                all_barbs.append(barb)

                # move to next position along spine
                position_spacing = barb_spacing * (1.0 - 0.3 * (u ** 1.8)) # 2 instead of 1.8
                current_t += position_spacing / total_spine_length
                current_t = min(current_t, 1.0)

                # 3. create barbules along each barb
                barbule_count = max(2, int(current_barb_length * barbule_density / barbule_count_modifier))

                for barbule_side in [-1, 1]:
                    for k in range(barbule_count):
                        barbule = add_barbules_to_barb(k, barbule_count, barb, up_vector, barbule_side, barbule_length)
                        all_barbules.append(barbule)

    # 4. convert all curves to segments for .ply export
    segments_spines = [curve.to_segment() for curve in all_spines]
    segments_barbs = [curve.to_segment() for curve in all_barbs]
    segments_barbules = [curve.to_segment() for curve in all_barbules]

    # 5. write to .ply file
    path_spines = output_path + "_spines.ply"
    path_barbs = output_path + "_barbs.ply"
    path_barbules = output_path + "_barbules.ply"

    write_ply_file(path_spines, segments_spines, len(all_spines))
    write_ply_file(path_barbs, segments_barbs, len(all_barbs))
    write_ply_file(path_barbules, segments_barbules, len(all_barbules))

    count_beziers = len(all_spines) + len(all_barbs) + len(all_barbules)
    print(f"Generated {num_feathers} feather(s) with {count_beziers} bezier curves in total.")
    print(f"Written to: {path_spines}, {path_barbs}, {path_barbules}")

    return # all_curves

if __name__ == "__main__":
    output_file_prefix = ".\\tests\\feather\\meshes\\feather"
    generate_feather(output_file_prefix, num_feathers=1, use_convergence=True, random_seed=42)

    input("Press Enter to exit...")