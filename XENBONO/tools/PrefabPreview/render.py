"""Draws the output of preview.exe: every piece as its box (or wedge / tent for roofs), from the front, the side and from above.

    python render.py <result.json> <out.png>

Needs numpy and OpenCV (pip install numpy opencv-python).
"""
import json
import math
import sys

import cv2
import numpy as np

COLORS = {
    "wall": (60, 90, 200), "window": (230, 200, 60), "opening": (200, 130, 60), "door": (40, 60, 120),
    "floor": (110, 160, 110), "roof": (60, 60, 230), "roof cap": (150, 80, 220), "stairs": (200, 200, 60),
    "rail": (80, 200, 200), "pillar": (150, 150, 150), "trim": (200, 150, 200), "feature": (30, 200, 255),
    "prop": (60, 170, 60),
}


def rot(yaw, x, y):
    r = math.radians(yaw)
    return x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)


def solid(t, cx, cy, z0, yaw, scale=(1, 1, 1)):
    """The corners of a piece as a list of faces (each a list of 3D points)."""
    sx, sy, sz = t["size"][0] * scale[0], t["size"][1] * scale[1], t["size"][2] * scale[2]
    hx, hy = sx / 2, sy / 2
    shape = t["shape"]

    def pt(lx, ly, lz):
        rx, ry = rot(yaw, lx, ly)
        return (cx + rx, cy + ry, z0 + lz)

    low = [(-hx, -hy), (hx, -hy), (hx, hy), (-hx, hy)]

    if shape == "box":
        top = [(-hx, -hy, sz), (hx, -hy, sz), (hx, hy, sz), (-hx, hy, sz)]
    elif shape == "shed":
        axis, sign = t["high"]
        if axis == 0:
            top = [(lx, ly, sz if lx * sign > 0 else 0) for (lx, ly) in low]
        else:
            top = [(lx, ly, sz if ly * sign > 0 else 0) for (lx, ly) in low]
    else:  # tent: high along the middle, the ridge along t["ridge"]
        ridge = t["ridge"]
        if ridge == 0:  # ridge along X: high at y = 0
            top = [(-hx, -hy, 0), (hx, -hy, 0), (hx, 0, sz), (-hx, 0, sz)]
            top2 = [(hx, hy, 0), (-hx, hy, 0)]
        else:
            top = [(-hx, -hy, 0), (0, -hy, sz), (0, hy, sz), (-hx, hy, 0)]
            top2 = [(hx, hy, 0), (hx, -hy, 0)]

    faces = []
    if shape == "tent":
        # a prism: two slopes and two triangle ends
        if t["ridge"] == 0:
            a = [(-hx, -hy, 0), (hx, -hy, 0), (hx, 0, sz), (-hx, 0, sz)]
            b = [(-hx, hy, 0), (hx, hy, 0), (hx, 0, sz), (-hx, 0, sz)]
            e1 = [(-hx, -hy, 0), (-hx, hy, 0), (-hx, 0, sz)]
            e2 = [(hx, -hy, 0), (hx, hy, 0), (hx, 0, sz)]
        else:
            a = [(-hx, -hy, 0), (-hx, hy, 0), (0, hy, sz), (0, -hy, sz)]
            b = [(hx, -hy, 0), (hx, hy, 0), (0, hy, sz), (0, -hy, sz)]
            e1 = [(-hx, -hy, 0), (hx, -hy, 0), (0, -hy, sz)]
            e2 = [(-hx, hy, 0), (hx, hy, 0), (0, hy, sz)]
        base = [(lx, ly, 0) for (lx, ly) in low]
        for f in (a, b, e1, e2, base):
            faces.append([pt(*p) for p in f])
        return faces

    base = [(lx, ly, 0) for (lx, ly) in low]
    faces.append([pt(*p) for p in base])
    faces.append([pt(*p) for p in top])
    for i in range(4):
        j = (i + 1) % 4
        faces.append([pt(*base[i]), pt(*base[j]), pt(*top[j]), pt(*top[i])])
    return faces


def project(p, az, el, scale, cx, cy, center):
    x, y, z = p[0] - center[0], p[1] - center[1], p[2] - center[2]
    ca, sa = math.cos(az), math.sin(az)
    xr = x * ca - y * sa
    yr = x * sa + y * ca
    ce, se = math.cos(el), math.sin(el)
    # yr is depth, z is up
    sy_ = yr * se + z * ce
    depth = yr * ce - z * se
    return (cx + xr * scale, cy - sy_ * scale, depth)


def view(data, az, el, size=(760, 620)):
    img = np.full((size[1], size[0], 3), 235, np.uint8)
    types = data["types"]
    polys = []
    allpts = []
    for it in data["items"]:
        t = types[it["t"]]
        for face in solid(t, it["x"], it["y"], it["z"], it["yaw"], it.get("scale", (1, 1, 1))):
            polys.append((t["kind"], face))
            allpts.extend(face)

    if not allpts:
        return img

    pts = np.array(allpts)
    center = ((pts[:, 0].max() + pts[:, 0].min()) / 2, (pts[:, 1].max() + pts[:, 1].min()) / 2, (pts[:, 2].max() + pts[:, 2].min()) / 2)
    extent = max(pts[:, 0].max() - pts[:, 0].min(), pts[:, 1].max() - pts[:, 1].min(), (pts[:, 2].max() - pts[:, 2].min()) * 1.2)
    scale = 0.62 * min(size) / extent * (1.0 if el < 1.3 else 1.1)

    # ground square of the volume
    gx, gy = data["size"][0] * 512 / 2, data["size"][1] * 512 / 2
    ground = [project((x, y, 0), az, el, scale, size[0] / 2, size[1] / 2, center)[:2] for (x, y) in [(-gx, -gy), (gx, -gy), (gx, gy), (-gx, gy)]]
    cv2.polylines(img, [np.array(ground, np.int32)], True, (150, 150, 150), 2)
    # the south edge (the front) in a heavier line
    cv2.line(img, tuple(int(v) for v in ground[0]), tuple(int(v) for v in ground[1]), (0, 120, 0), 3)

    out = []
    for kind, face in polys:
        pr = [project(p, az, el, scale, size[0] / 2, size[1] / 2, center) for p in face]
        depth = sum(p[2] for p in pr) / len(pr)
        out.append((depth, kind, pr))
    out.sort(key=lambda a: -a[0])

    for depth, kind, pr in out:
        poly = np.array([[p[0], p[1]] for p in pr], np.int32)
        col = COLORS.get(kind, (128, 128, 128))
        cv2.fillPoly(img, [poly], col)
        cv2.polylines(img, [poly], True, (20, 20, 20), 1, cv2.LINE_AA)

    return img


def main():
    data = json.load(open(sys.argv[1]))
    views = [view(data, math.radians(a), math.radians(e)) for (a, e) in [(35, 28), (-55, 28), (180 + 35, 28), (0, 88)]]
    top = np.hstack(views[:2])
    bottom = np.hstack(views[2:])
    sheet = np.vstack([top, bottom])
    label = f'{data["name"]}  size {data["size"]}  core {data["core"][0]}x{data["core"][1]}  levels {data["core"][2]}  pieces {len(data["items"])}'
    cv2.putText(sheet, label, (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 2, cv2.LINE_AA)
    legend_x = 10
    for k, c in COLORS.items():
        cv2.rectangle(sheet, (legend_x, 40), (legend_x + 14, 54), c, -1)
        cv2.putText(sheet, k, (legend_x + 18, 52), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (0, 0, 0), 1, cv2.LINE_AA)
        legend_x += 24 + 7 * len(k)
    for i, n in enumerate(data["notes"][:5]):
        cv2.putText(sheet, n, (10, 78 + i * 18), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (80, 0, 0), 1, cv2.LINE_AA)
    cv2.imwrite(sys.argv[2], sheet)
    print("wrote", sys.argv[2])


main()
