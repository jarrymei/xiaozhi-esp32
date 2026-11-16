"""用用户提供的舵机图构建 B1 双足样机及安装耳试件，单位为毫米。"""

import json
from pathlib import Path

import cadquery as cq


OUT = Path(__file__).resolve().parent / "output"
OUT.mkdir(exist_ok=True)

# 用户图纸上的标注。安装耳以轴侧耳面为局部 z=0。
BODY_L = 22.8
BODY_W = 12.1
EAR_SPAN = 32.3
EAR_T = 2.7
BASE_TO_EAR = 21.0
BASE_TO_NOSE = 28.3
BASE_TO_TIP = 33.0

# 设计值，用试件检查开口间隙。轴心长向偏置用于装配预览，实物由原配摇臂转印。
GAP = 0.4
AXIS_X_PREVIEW = -5.5
CLAMP_BOLT_X = 19.5
HIP_X = 27.0
HIP_Z = 84.0
ANKLE_Z = 38.5
WINDOW_L = BODY_L + 2 * GAP
WINDOW_W = BODY_W + 2 * GAP


def block(x1, x2, y1, y2, z1, z2):
    return cq.Solid.makeBox(x2-x1, y2-y1, z2-z1, cq.Vector(x1, y1, z1))


def bore(radius, length, x, y, z, axis=(0, 0, 1)):
    return cq.Solid.makeCylinder(radius, length, cq.Vector(x, y, z), cq.Vector(*axis))


def round_case(x1, x2, y1, y2, z1, z2, radius):
    # 沿深度方向圆角，正面为圆角矩形。
    solid = block(x1, x2, y1, y2, z1, z2)
    return cq.Workplane(obj=solid).edges("|Y").fillet(radius).val()


def hip(shape, x):
    return (shape.rotate((0, 0, 0), (1, 0, 0), 180)
            .rotate((0, 0, 0), (0, 0, 1), 90)
            .translate((x, -AXIS_X_PREVIEW, HIP_Z)))


def ankle(shape, x):
    return (shape.rotate((0, 0, 0), (0, 1, 0), -90)
            .rotate((0, 0, 0), (0, 0, 1), 90)
            .translate((x, 0, ANKLE_Z)))


def mount_window():
    return block(-WINDOW_L/2, WINDOW_L/2, -WINDOW_W/2, WINDOW_W/2, -1, 4)


def bolt_holes():
    # 两颗夹座螺丝位于耳端外，和舵机原有孔距无关。
    return [bore(1.65, 14, x, 0, -9) for x in (-CLAMP_BOLT_X, CLAMP_BOLT_X)]


def seat():
    return block(-23, 23, -11, 11, 0, 3).cut(mount_window(), *bolt_holes()).clean()


def clamp():
    # 压条跨在耳背面。两颗螺丝各配螺母，按实物选择平垫和夹紧量。
    return block(12.3, 23, -6, 6, -EAR_T-3, -EAR_T).cut(bolt_holes()[1]).clean()


def servo_reference():
    # 壳体为图纸包络，轴座与轴径仅用于摆放预览。
    body = block(-BODY_L/2, BODY_L/2, -BODY_W/2, BODY_W/2, -BASE_TO_EAR, BASE_TO_NOSE-BASE_TO_EAR)
    ears = block(-EAR_SPAN/2, EAR_SPAN/2, -BODY_W/2, BODY_W/2, -EAR_T, 0)
    shaft = bore(2.4, BASE_TO_TIP-BASE_TO_NOSE, AXIS_X_PREVIEW, 0, BASE_TO_NOSE-BASE_TO_EAR)
    return body.fuse(ears, shaft).clean()


def deck():
    plate = block(-48, 48, -31, 31, 81, 84)
    for x in (-HIP_X, HIP_X):
        plate = plate.cut(hip(mount_window(), x), *(hip(hole, x) for hole in bolt_holes()))
    for x in (-41, 41):
        for y in (-18, 18):
            plate = plate.cut(bore(1.65, 6, x, y, 79))
    return plate.clean()


def shell():
    outside = round_case(-48, 48, -31, 31, 84, 155, 12)
    inside = round_case(-44, 44, -27, 35, 80, 151, 9)
    # 显示区尺寸取项目内现有 LCD 资料，四周留装配余量。
    opening = round_case(-14.6, 14.6, -34, -25, 105.5, 139.5, 1.1)
    part = outside.cut(inside, opening)
    # 后盖柱和底架柱与侧壁重叠，导孔供样机螺丝试配。
    for x in (-42, 42):
        for z in (99, 140):
            part = part.fuse(bore(4.6, 8, x, 23, z, (0, 1, 0)))
            part = part.cut(bore(1.35, 9, x, 23, z, (0, 1, 0)))
    for x in (-41, 41):
        for y in (-18, 18):
            part = part.fuse(bore(5.2, 10, x, y, 84))
            part = part.cut(bore(1.35, 11, x, y, 83.5))
    for side in (-1, 1):
        # 主控托盘由后侧沿支撑条滑入。屏幕托边靠在 PCB 边角，中央留出接口位置。
        low, high = sorted((side*39, side*45))
        part = part.fuse(block(low, high, -17, 28, 106, 109))
        low, high = sorted((side*12, side*17.6))
        part = part.fuse(block(low, high, -28, -21, 100.5, 103))
        low, high = sorted((side*16.2, side*18.5))
        part = part.fuse(block(low, high, -28, -22, 102, 143))
    return part.clean()


def rear_cover():
    cover = round_case(-48, 48, 31, 34, 84, 155, 12)
    cover = cover.cut(block(-9, 9, 30, 35, 83, 98))
    for x in (-42, 42):
        for z in (99, 140):
            cover = cover.cut(bore(1.65, 5, x, 30, z, (0, 1, 0)))
    return cover.clean()


def leg():
    carrier = ankle(seat(), 0)
    # 两侧肋避开舵机壳体，顶板作为可取下描孔的髋连接坯。
    left = block(-11, -7.5, -3, 10, ANKLE_Z+21, 67)
    right = block(7.5, 11, -3, 10, ANKLE_Z+21, 67)
    hip_pad = block(-15, 15, -12, 13, 65, 69)
    return carrier.fuse(left, right, hip_pad).clean()


def shoe():
    # 鞋底与前立板承接原配摇臂，板上孔在拆下后按摇臂转印。
    sole = cq.Workplane(obj=block(-21.5, 21.5, -39, 28, 0, 7)).edges("|Z").fillet(7).val()
    front = block(-16, 16, -19, -15.5, 5, 49)
    left = block(-17, -12, -25, -12, 5, 18)
    right = block(12, 17, -25, -12, 5, 18)
    return sole.fuse(front, left, right).clean()


def electronics_tray():
    # 两对绑带孔跨过开发板。下方可垫绝缘片，托盘后侧由后盖限位。
    tray = block(-43.6, 43.6, -17, 31, 109, 112)
    for x in (-22, 22):
        for y in (-14.5, 19):
            tray = tray.cut(block(x-2.5, x+2.5, y-1.5, y+1.5, 108, 113))
    return tray.clean()


def ear_cover():
    # 装饰件可用可移除胶贴固定，避开后盖和结构螺丝。
    outer = block(48, 54, -7, 16, 108, 138)
    return cq.Workplane(obj=outer).edges("|X").fillet(7).val()


def face_bezel():
    # 薄面罩扩大黑色屏脸轮廓，屏幕有效区通过中间窗口露出。
    outer = round_case(-43, 43, -32.2, -31, 88.5, 150.5, 11)
    opening = round_case(-14.6, 14.6, -33, -30, 105.5, 139.5, 1.1)
    return outer.cut(opening).clean()


def export_part(name, solid):
    assert solid.isValid() and len(solid.Solids()) == 1, name
    bb = solid.BoundingBox()
    on_bed = solid.translate((-bb.xmin, -bb.ymin, -bb.zmin))
    cq.exporters.export(on_bed, str(OUT / f"{name}.stl"), tolerance=0.08, angularTolerance=0.15)
    return [round(v, 2) for v in (bb.xlen, bb.ylen, bb.zlen)]


base, case, cover, leg_part, foot, cap = deck(), shell(), rear_cover(), leg(), shoe(), clamp()
tray, ear, bezel = electronics_tray(), ear_cover(), face_bezel()
parts = {"01_head_shell": case, "02_rear_cover": cover, "03_hip_deck": base,
         "04_leg_link": leg_part, "05_shoe": foot, "06_ear_clamp": cap,
         "07_servo_fit_coupon": seat(), "08_electronics_tray": tray, "09_ear_cover": ear,
         "10_face_bezel": bezel}
sizes = {name: export_part(name, part) for name, part in parts.items()}

visuals = [("head_shell", case, (0.23, 0.29, 0.38)), ("rear_cover", cover, (0.31, 0.37, 0.47)),
           ("hip_deck", base, (0.38, 0.43, 0.55)), ("electronics_tray", tray, (0.46, 0.55, 0.60)),
           ("ear_cover_R", ear, (0.65, 0.58, 0.81)), ("ear_cover_L", ear.mirror("YZ"), (0.65, 0.58, 0.81)),
           ("face_bezel", bezel, (0.065, 0.085, 0.11))]
servo = servo_reference()
for side, x in (("L", -HIP_X), ("R", HIP_X)):
    visuals.extend([(f"leg_{side}", leg_part.translate((x, 0, 0)), (0.40, 0.47, 0.58)),
                    (f"shoe_{side}", foot.translate((x, 0, 0)), (0.24, 0.29, 0.38)),
                    (f"reference_hip_servo_{side}", hip(servo, x), (0.74, 0.63, 0.39)),
                    (f"reference_ankle_servo_{side}", ankle(servo, x), (0.74, 0.63, 0.39))])
    for index, ear_cap in enumerate((cap, cap.mirror("YZ")), 1):
        visuals.append((f"hip_clamp_{side}_{index}", hip(ear_cap, x), (0.62, 0.57, 0.76)))
        visuals.append((f"ankle_clamp_{side}_{index}", ankle(ear_cap, x), (0.62, 0.57, 0.76)))

# 显示面板与开发板用占位件检查净空间，均不导出为打印零件。
visuals.extend([("reference_lcd", block(-15.75, 15.75, -27, -24, 103, 142), (0.10, 0.18, 0.22)),
                ("reference_esp32", block(-31, 31, -12, 16, 112, 127), (0.28, 0.55, 0.46))])
assembly = cq.Assembly(name="EMO_B1_prototype")
for name, part, color in visuals:
    assembly.add(part, name=name, color=cq.Color(*color))
assembly.export(str(OUT / "EMO_B1_assembly.step"))

def find_overlaps(items):
    # 包围盒先排除距离较远的零件，再区分实体相交和接触面。
    bounded = [(name, part, part.BoundingBox()) for name, part, _ in items]
    overlaps = []
    for index, (name, part, a) in enumerate(bounded):
        for other_name, other, b in bounded[index+1:]:
            if any(getattr(a, axis+"max") <= getattr(b, axis+"min") + 0.001 or
                   getattr(b, axis+"max") <= getattr(a, axis+"min") + 0.001 for axis in "xyz"):
                continue
            volume = sum(s.Volume() for s in part.intersect(other).Solids())
            if volume > 0.03:
                overlaps.append([name, other_name, round(volume, 3)])
    return overlaps


def posed(hip_angle, ankle_angle):
    for name, part, color in visuals:
        for side, x, direction in (("L", -HIP_X, -1), ("R", HIP_X, 1)):
            if name == f"shoe_{side}":
                z = ANKLE_Z + AXIS_X_PREVIEW
                part = part.rotate((x, 0, z), (x, 1, z), ankle_angle * direction)
            if (name in (f"shoe_{side}", f"leg_{side}", f"reference_ankle_servo_{side}") or
                    name.startswith(f"ankle_clamp_{side}_")):
                part = part.rotate((x, 0, 0), (x, 0, 1), hip_angle * direction)
        yield name, part, color


poses = [{"左右镜像髋角": a, "左右镜像踝角": b, "相交体积_mm3": find_overlaps(list(posed(a, b)))}
         for a, b in ((0, 0), (5, 0), (-5, 0), (0, 5), (0, -5))]
report = {"单位": "mm", "打印件外包尺寸": sizes, "离散姿态": poses}
(OUT / "检查结果.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

# 保存与 CAD 同源的轻量网格供预览和复查。
meshes = []
for name, part, color in visuals:
    vertices, faces = part.tessellate(0.2, 0.25)
    meshes.append([name, color, [v.toTuple() for v in vertices], faces])
(OUT / "preview_meshes.json").write_text(json.dumps(meshes, separators=(",", ":")), encoding="utf-8")
print(json.dumps(report, ensure_ascii=False))
