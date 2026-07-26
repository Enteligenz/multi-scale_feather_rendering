import random
import math
import struct
import os

from .feather_gen_bezier_and_vec3 import *
from .addon_preferences import get_prefs
from .registry import SceneRegistry

def perlin_noise_1d(x, octaves=1, persistence=0.5):
    """Simple 1D Perlin-like noise function for natural variation."""
    total = 0
    max_value = 0
    for i in range(octaves):
        frequency = 2 ** i
        amplitude = persistence ** i
        
        # Use sine waves with prime number offsets for pseudo-randomness
        value = math.sin(x * frequency * 0.1 + 0.1) * 0.5 + 0.5
        value += math.sin(x * frequency * 0.2 + 7.1) * 0.25 + 0.25
        value += math.sin(x * frequency * 0.3 + 13.7) * 0.125 + 0.125
        value /= 1.375  # Normalize to [0, 1]
        
        total += value * amplitude
        max_value += amplitude
    
    return total / max_value if max_value > 0 else 0
    
def add_random_variation(value, variation_factor=0.2):
    """Add random variation to a value within a given factor."""
    variation = (random.random() * 2 - 1) * variation_factor
    return value * (1 + variation)

def interpolate_bezier(curve: BezierCurve, t: float) -> Vec3:
    """Interpolate a point on a cubic bezier curve."""
    u = t
    one_minus_u = 1 - u
    
    # cubic bezier formula
    return Vec3(
        one_minus_u**3 * curve.w0.x + 3*one_minus_u**2*u * curve.w1.x + 3*one_minus_u*u**2 * curve.w2.x + u**3 * curve.w3.x,
        one_minus_u**3 * curve.w0.y + 3*one_minus_u**2*u * curve.w1.y + 3*one_minus_u*u**2 * curve.w2.y + u**3 * curve.w3.y,
        one_minus_u**3 * curve.w0.z + 3*one_minus_u**2*u * curve.w1.z + 3*one_minus_u*u**2 * curve.w2.z + u**3 * curve.w3.z
    )

def calculate_bezier_arc_length(curve: BezierCurve, num_samples: int = 100) -> float:
    """Calculate the arc length of a bezier curve by sampling points."""
    total_length = 0
    prev_point = interpolate_bezier(curve, 0)

    for i in range(1, num_samples + 1):
        t = i / num_samples
        current_point = interpolate_bezier(curve, t)
        segment_length = (current_point - prev_point).length()
        total_length += segment_length
        prev_point = current_point

    return total_length

def get_spine_tangent_at(spine, u):
    """Get tangent vector along the spine (or barb) at parameter t (0-1)."""
    # derivative of the cubic bezier curve
    tangent_x = 3*(1-u)**2 * (spine.w1.x - spine.w0.x) + \
                6*(1-u)*u * (spine.w2.x - spine.w1.x) + \
                3*u**2 * (spine.w3.x - spine.w2.x)
    tangent_y = 3*(1-u)**2 * (spine.w1.y - spine.w0.y) + \
                6*(1-u)*u * (spine.w2.y - spine.w1.y) + \
                3*u**2 * (spine.w3.y - spine.w2.y)
    tangent_z = 3*(1-u)**2 * (spine.w1.z - spine.w0.z) + \
                6*(1-u)*u * (spine.w2.z - spine.w1.z) + \
                3*u**2 * (spine.w3.z - spine.w2.z)
    
    return Vec3(tangent_x, tangent_y, tangent_z).normalize()

def write_ply_file(output_path, segments, num_beziers):
    # note that for feathers, num_segments is always 1
    with open(output_path, "wb") as file:
        # write .ply header in ASCII
        header = f"""ply
format binary_little_endian 1.0
element num_beziers {num_beziers}
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
        file.write(header.encode('utf-8')) # write header as bytes

        # write dummy normal data
        for bezier in range(num_beziers):
            file.write(struct.pack("<3f", 0.0, 0.0, 0.0))

        # write segment data in binary format (float32)
        for seg in segments:
            file.write(struct.pack("<12f", *seg)) # little-endian 12 float values per segment

def create_path(registry, shape_name):
    rel_filepath = os.path.join(get_prefs().mesh_dir_name, shape_name + ".ply")
    rel_filepath = rel_filepath.replace('\\', '/') # Ensure the shape path is not using \ to keep the xml valid
    abs_filepath = os.path.join(registry.path, rel_filepath)
    return (rel_filepath, abs_filepath)