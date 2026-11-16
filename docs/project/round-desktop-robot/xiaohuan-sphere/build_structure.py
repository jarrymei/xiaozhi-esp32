"""生成球形桌宠结构样机。尺寸单位为毫米。"""
from pathlib import Path

import numpy as np
import trimesh as tm

OUTPUT = Path(__file__).resolve().parent / "structure"
ENGINE = "manifold"


def box(lo, hi):
    lo, hi = np.array(lo), np.array(hi)
    mesh = tm.creation.box(hi - lo)
    mesh.apply_translation((lo + hi) / 2)
    return mesh


def cylinder_y(radius, depth, x, y, z):
    mesh = tm.creation.cylinder(radius, height=depth, sections=48)
    mesh.apply_transform(tm.transformations.rotation_matrix(np.pi / 2, [1, 0, 0]))
    mesh.apply_translation([x, y, z])
    return mesh


def union(*parts):
    return tm.boolean.union(parts, engine=ENGINE)


def difference(base, *cuts):
    return tm.boolean.difference([base, *cuts], engine=ENGINE)


def intersect(*parts):
    return tm.boolean.intersection(parts, engine=ENGINE)


def shell(front):
    outer = tm.creation.uv_sphere(radius=45, count=[61, 120])
    inner = tm.creation.uv_sphere(radius=43, count=[61, 120])
    y0, y1 = (-50, 0) if front else (0, 50)
    outer = intersect(outer, box([-50, y0, -38], [50, y1, 50]))
    inner = intersect(inner, box([-50, -50, -36], [50, 50, 50]))
    return difference(outer, inner)


def slot_y(x, y, z, travel=8, diameter=3.4, depth=8):
    radius = diameter / 2
    return union(
        cylinder_y(radius, depth, x, y, z - travel / 2),
        cylinder_y(radius, depth, x, y, z + travel / 2),
        box([x-radius, y-depth/2, z-travel/2], [x+radius, y+depth/2, z+travel/2]),
    )


# 模块包络按宽、前后深、竖向高布置，实测后可更新。
MODULE_BOUNDS = {
    "speaker": ([-31, 23, -9], [-23, 28, 9]),
    "mic": ([24, 19, 8], [30, 24, 18]),
    "switch": ([-6, 20, 30.8], [6, 24, 34]),
}


def accessory_seats(outer):
    # 内侧开口的凹座供模块从前方放入，留薄泡棉或胶固定空间。
    seats = (
        box([-32.4, 25.2, -10.4], [-21.6, 44, 10.4]),
        box([22.65, 21.5, 6.7], [31.35, 43, 19.3]),
        box([-7.4, 22, 30.15], [7.4, 37, 35.4]),
    )
    pockets = [
        box([lo[0]-.25, 0, lo[2]-.25], [hi[0]+.25, hi[1]+.25, hi[2]+.25])
        for lo, hi in MODULE_BOUNDS.values()
    ]
    # 喇叭开五个出声孔，麦克风开单孔，拨杆开矩形槽。
    openings = [
        *(cylinder_y(1.05, 30, -27, 42, z) for z in (-6, -3, 0, 3, 6)),
        cylinder_y(1, 30, 27, 38, 13),
        box([-4, 23.8, 31.5], [4, 38, 33.5]),
    ]
    return [intersect(outer, seat) for seat in seats], pockets + openings


# 停放位中心为 X、Y、Z。卡座沿前后方向套住接头外壳。
PARK_POINTS = {
    "speaker": (-32, 14, -14),
    "mic": (34, 15, 8),
    "switch": (28, 8, 24.5),
}


def parking_clips():
    inner = tm.creation.uv_sphere(radius=42.8, count=[61, 120])
    for name, (x, y, z) in PARK_POINTS.items():
        clip = intersect(inner, box([x-3.25, y-2.5, z-5.4], [x+3.25, y+5, z+3.15]))
        clip = difference(
            clip,
            box([x-2.15, y-3, z-1.95], [x+2.15, y+2.2, z+1.95]),
            # 开放线槽让线从插头背后引出，螺钉位于线槽下方。
            box([x-.85, y-3, z-.85], [x+.85, y+5.5, z+4]),
            cylinder_y(1.1, 9, x, y+1, z-3.65),
            cylinder_y(1.65, 6, x, y-3, z-3.65),
        )
        yield name, clip


def cover(front):
    base = shell(front)
    y0, y1 = (-6, 0) if front else (0, 6)
    # 四个磁铁座位于接缝内侧，实体外缘裁到原球面。
    pads = [box([x-4.5, y0, z-4.5], [x+4.5, y1, z+4.5])
            for x in (-39, 39) for z in (-8, 8)]
    outer = tm.creation.uv_sphere(radius=45, count=[61, 120])
    base = union(base, *(intersect(outer, pad) for pad in pads))
    pockets = [cylinder_y(3.15, 3.4, x, -1.5 if front else 1.5, z)
               for x in (-39, 39) for z in (-8, 8)]
    # 两个底部凸耳给内部支架提供壳体固定孔。
    if not front:
        seats, cuts = accessory_seats(outer)
        base = union(base, *seats)
        pockets.extend(cuts)
        for x, y, z in PARK_POINTS.values():
            pad = box([x-3.25, y+5, z-5.4], [x+3.25, 46, z+3.15])
            base = union(base, intersect(outer, pad))
            pockets.append(cylinder_y(.82, 3.4, x, y+6.5, z-3.65))
        # USB 开孔沿暂定插拔轴贯穿曲面，保留球壳外轮廓。
        pockets.append(box([-6.5, 26, -32.5], [6.5, 46, -25.5]))
        for x in (-14, 14):
            base = union(base, box([x-4, 3, -37], [x+4, 14, -30.4]))
            pockets.append(cylinder_y(1.7, 12, x, 8.5, -33.5))
    return difference(base, *pockets)


def carrier():
    # 屏幕框与开发板托架由两根纵梁连接，支架整体从前方抽出。
    screen = difference(
        box([-24, -19, -25], [24, -16, 25]),
        box([-14.6, -20, -17], [14.6, -15, 17]),
        *(slot_y(x, -17.5, z) for x in (-19.5, 19.5) for z in (-12, 12)),
    )
    board = difference(
        box([-22.5, 22, -30], [22.5, 25, 30]),
        box([-13, 21, -25], [13, 26, 25]),
        *(slot_y(x, 23.5, z, travel=12) for x in (-19, 19) for z in (-17, 17)),
    )
    beams = [box([x-2.5, -17, -27], [x+2.5, 23, -22]) for x in (-20, 20)]
    feet = [box([x-4.5, -2, -35.5], [x+4.5, 2.8, -23]) for x in (-16, 16)]
    frame = difference(
        union(screen, board, *beams, *feet),
        # 下横边中央让出插头包络，保留左右立边和横边两端。
        box([-6.5, 21, -33], [6.5, 26, -24.8]),
        *(cylinder_y(1.7, 7, x, 0.5, -33.5) for x in (-14, 14)),
    )


    # 裁去托架四角，让后壳内壁保留装配余量。
    return intersect(frame, tm.creation.uv_sphere(radius=42.4, count=[61, 120]))


def main():
    OUTPUT.mkdir(exist_ok=True)
    front, rear, frame = cover(True), cover(False), carrier()
    for name, mesh in (("front-cover", front), ("rear-shell", rear), ("carrier", frame)):
        path = OUTPUT / f"{name}.stl"
        mesh.export(path)
        check = tm.load_mesh(path)
        assert check.is_volume and len(check.split()) == 1, name
        print(name, np.round(check.extents, 3), "mm")
    display = box([-15.75, -15.98, -19.5], [15.75, -9, 19.5])
    board = box([-17, 8, -30], [17, 21.98, 30])
    # 插头内端停在主板名义后表面；真实接口方向和插入量待实测。
    usb = box([-6, 22, -32], [6, 46, -26])
    wire_a = box([7, 22.1, -24], [10, 27.5, -22])
    wire_b = box([7, 26, -31], [10, 27.5, -23])
    modules = [box(lo, hi) for lo, hi in MODULE_BOUNDS.values()]
    for name, clip in parking_clips():
        for obstacle in (rear, frame, board, usb, wire_a, wire_b, *modules):
            overlap = intersect(clip, obstacle)
            assert overlap.is_empty or min(overlap.extents) < .001 or abs(overlap.volume) < .05, name
        path = OUTPUT / f"parking-clip-{name}.stl"
        clip.export(path)
        loaded = tm.load_mesh(path)
        assert loaded.is_volume and len(loaded.split()) == 1, name
        print(path.name)
    for check in (usb, wire_a, wire_b, *modules):
        for obstacle in (rear, frame, board):
            overlap = intersect(check, obstacle)
            assert overlap.is_empty or abs(overlap.volume) < 0.05, overlap.volume
    for module in modules:
        for route in (usb, wire_a, wire_b):
            assert intersect(module, route).is_empty
    for left, right in ((front, rear), (front, frame), (rear, frame),
                        (front, display), (rear, board), (frame, display),
                        (frame, board)):
        overlap = intersect(left, right)
        assert overlap.is_empty or min(overlap.extents) < 0.001 or abs(overlap.volume) < 0.05, overlap.volume


if __name__ == "__main__":
    main()

