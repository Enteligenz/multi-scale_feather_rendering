import mathutils
import re


def find_unique_name(used: set[str], name: str):
    unique_name = name
    index = 0

    while unique_name in used:
        unique_name = f"{name}.{index:03d}"
        index += 1
    
    used.add(unique_name)
    return unique_name


def escape_identifier(name):
    return re.sub('[^a-zA-Z0-9_]', '_', name)


def flat_matrix(matrix):
    return [x for row in matrix for x in row]


def str_float(f: float):
    return "%.5g" % f


def str_flat_matrix(matrix):
    return ",  ".join([
        ",".join([ str_float(v) for v in row ])
        for row in matrix
    ])


def str_flat_array(array):
    if isinstance(array, float):
        return str_float(array)
    return ",".join([str_float(x) for x in array])


def orient_camera(matrix, skip_scale=False):
    # Y Up, Front -Z
    loc, rot, sca = matrix.decompose()
    return mathutils.Matrix.LocRotScale(loc, rot @ mathutils.Quaternion((0, 0, 1, 0)) @ mathutils.Quaternion((0, 0, 0, 1)), mathutils.Vector.Fill(3, 1) if skip_scale else sca)


def try_extract_node_value(value, default=0):
    try:
        return float(value)
    except:
        return default


def check_socket_if_constant(socket, value):
    if socket.is_linked:
        return False

    if socket.type == "RGBA" or socket.type == "VECTOR":
        return socket.default_value[0] == value and socket.default_value[1] == value and socket.default_value[2] == value
    else:
        return socket.default_value == value


def check_socket_if_black(socket):
    return check_socket_if_constant(socket, value=0)


def check_socket_if_white(socket):
    return check_socket_if_constant(socket, value=1)

class QuadTree:
    def __init__(self, bounds, max_depth=8, max_items=10):
        self.bounds = bounds
        self.max_depth = max_depth
        self.max_items = max_items
        self.items = [] # each items is: (point_2d, face_idx)
        self.children = None
        self.depth = 0

    def insert(self, point_3d, face_idx, depth=0):
        if self.children is not None: # already subdivided, insert into one of the children
            child_idx = self._get_quadrant((point_3d[0], point_3d[1]))
            self.children[child_idx].insert(point_3d, face_idx, depth + 1)
            return
        
        self.items.append((point_3d, face_idx))

        # subdivide
        if len(self.items) > self.max_items and depth < self.max_depth:
            self._subdivide(depth)

    # splits node into 4 children
    def _subdivide(self, depth):
        min_x, min_y, max_x, max_y = self.bounds
        mid_x = (min_x + max_x) / 2
        mid_y = (min_y + max_y) / 2

        self.children = [
            QuadTree((min_x, min_y, mid_x, mid_y), self.max_depth, self.max_items), # bottom-left
            QuadTree((mid_x, min_y, max_x, mid_y), self.max_depth, self.max_items), # bottom-right
            QuadTree((min_x, mid_y, mid_x, max_y), self.max_depth, self.max_items), # top-left
            QuadTree((mid_x, mid_y, max_x, max_y), self.max_depth, self.max_items), # top-right
        ]

        for child in self.children:
            child.depth = depth + 1

        # redistribute items to children
        for point_3d, face_idx in self.items:
            child_idx = self._get_quadrant((point_3d[0], point_3d[1]))
            self.children[child_idx].insert(point_3d, face_idx, depth + 1)
        
        self.items = []

    def _get_quadrant(self, point_2d):
        min_x, min_y, max_x, max_y = self.bounds
        mid_x = (min_x + max_x) / 2
        mid_y = (min_y + max_y) / 2

        x, y = point_2d
        if x < mid_x:
            return 0 if y < mid_y else 2 # left
        else:
            return 1 if y < mid_y else 3 # right
        
    # find nearest face for a given 2d point
    def find_nearest(self, point_3d, face_centers, best_idx=None, best_dist_sq=float('inf')):
        point_2d = (point_3d[0], point_3d[1])
        if not self._could_contain_closer(point_2d, best_dist_sq):
            return best_idx, best_dist_sq
        
        if self.children is not None:
            # search children, starting with quadrant containing the point
            quadrant = self._get_quadrant(point_2d)
            for i in range(4):
                child_idx = (quadrant + i) % 4
                best_idx, best_dist_sq = self.children[child_idx].find_nearest(point_3d, face_centers, best_idx, best_dist_sq)
        else:
            # leaf node: check all items
            for point_2d_item, face_idx in self.items:
                # calculate actual 3d distance
                dist_sq = (face_centers[face_idx][0] - point_3d[0]) ** 2 + \
                         (face_centers[face_idx][1] - point_3d[1]) ** 2 + \
                         (face_centers[face_idx][2] - point_3d[2]) ** 2
                
                if dist_sq < best_dist_sq:
                    best_dist_sq = dist_sq
                    best_idx = face_idx

        return best_idx, best_dist_sq

    # check if this node's bounds could contain a point closer than best_dist_sq
    def _could_contain_closer(self, point_2d, best_dist_sq):
        min_x, min_y, max_x, max_y = self.bounds
        x, y = point_2d

        # find closest point in bounds to query point
        closest_x = max(min_x, min(x, max_x))
        closest_y = max(min_y, min(y, max_y))

        # calculate distance to closest point in bounds (2d for quick rejection)
        dist_sq = (closest_x - x) ** 2 + (closest_y - y) ** 2

        return dist_sq < best_dist_sq
    
# builds a quadtree from mesh face centers
def build_face_quadtree(me):
    if len(me.polygons) == 0:
        return None, [], []
    
    face_centers = []
    face_normals = []

    # collect face data
    for face in me.polygons:
        face_center = mathutils.Vector()
        for vert_idx in face.vertices:
            face_center += me.vertices[vert_idx].co
        face_center /= len(face.vertices)

        face_centers.append(face_center)
        face_normals.append(face.normal.copy())

    # calculate bounds for quadtree (using xy plane)
    min_x = min(fc.x for fc in face_centers)
    max_x = max(fc.x for fc in face_centers)
    min_y = min(fc.y for fc in face_centers)
    max_y = max(fc.y for fc in face_centers)

    # add small padding to avoid edge cases
    padding = 0.001
    bounds = (min_x - padding, min_y - padding, max_x + padding, max_y + padding)

    # build quadtree
    quadtree = QuadTree(bounds)
    for i, fc in enumerate(face_centers):
        quadtree.insert((fc.x, fc.y), i)

    return quadtree, face_centers, face_normals
