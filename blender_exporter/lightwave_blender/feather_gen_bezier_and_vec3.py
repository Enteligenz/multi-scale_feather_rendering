import math

class Vec3:
    """Simple 3D vector class."""
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x = float(x)
        self.y = float(y)
        self.z = float(z)

    def __add__(self, other):
        return Vec3(self.x + other.x, self.y + other.y, self.z + other.z)
    
    def __sub__(self, other):
        return Vec3(self.x - other.x, self.y - other.y, self.z - other.z)
    
    def __mul__(self, scalar):
        return Vec3(self.x * scalar, self.y * scalar, self.z * scalar)
    
    def normalize(self):
        length = math.sqrt(self.x**2 + self.y**2 + self.z**2)
        if length > 0:
            return Vec3(self.x/length, self.y/length, self.z/length)
        return Vec3()
    
    def cross(self, other):
        return Vec3(
            self.y * other.z - self.z * other.y,
            self.z * other.x - self.x * other.z,
            self.x * other.y - self.y * other.x
        )
    
    def length(self):
        """NOTE: this does not make a lot of sense, but is sometimes useful."""
        return math.sqrt(self.x**2 + self.y**2 + self.z**2)
    
    def to_tuple(self):
        return (self.x, self.y, self.z)
    
    def __str__(self):
        """Return a string representation of the Vector."""
        return (f"Vec3(), x: {self.x:.4f}, y: {self.y:.4f}, z: {self.z:.4f}\n")
    
class BezierCurve:
    """Represents a cubic bezier curve with 4 control points."""
    def __init__(self, w0: Vec3, w1: Vec3, w2: Vec3, w3: Vec3):
        self.w0 = w0
        self.w1 = w1
        self.w2 = w2
        self.w3 = w3

    def to_segment(self):
        """Convert to the format used in the .ply file."""
        return (self.w0.x, self.w0.y, self.w0.z,
                self.w1.x, self.w1.y, self.w1.z,
                self.w2.x, self.w2.y, self.w2.z,
                self.w3.x, self.w3.y, self.w3.z)
    
    def __str__(self):
        """Return a string representation of the curve."""
        return (f"BezierCurve()\n"
                f" w0: ({self.w0.x:.4f}, {self.w0.y:.4f}, {self.w0.z:.4f})\n"
                f" w1: ({self.w1.x:.4f}, {self.w1.y:.4f}, {self.w1.z:.4f})\n"
                f" w2: ({self.w2.x:.4f}, {self.w2.y:.4f}, {self.w2.z:.4f})\n"
                f" w3: ({self.w3.x:.4f}, {self.w3.y:.4f}, {self.w3.z:.4f})\n")