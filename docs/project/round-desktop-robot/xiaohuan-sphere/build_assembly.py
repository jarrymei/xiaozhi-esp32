"""导出可打印压片和带材质的桌宠装配模型。"""
import numpy as np
import trimesh as tm

from build_structure import MODULE_BOUNDS, OUTPUT, box, cylinder_y, difference, intersect, parking_clips, union

ROOT = OUTPUT.parent


def clip(side, z):
    # 压片跨过屏幕背面，台阶脚落在屏幕框长孔旁。
    def block(x0, x1, y0, y1):
        left, right = sorted((side*x0, side*x1))
        return box([left, y0, z-3.5], [right, y1, z+3.5])

    bridge = union(block(14.5, 23, -8.6, -5.6),
                   block(16.3, 23, -16, -8))
    return difference(bridge, cylinder_y(1.7, 12, side*19.5, -10.8, z))


def export_clip(side, name):
    part = clip(side, 0)
    part.apply_transform(tm.transformations.rotation_matrix(np.pi/2, [1, 0, 0]))
    part.apply_translation([0, 0, -part.bounds[0, 2]])
    path = OUTPUT / name
    part.export(path)
    loaded = tm.load_mesh(path)
    assert loaded.is_volume and len(loaded.split()) == 1


def material(name, rgba, metallic=0, roughness=.42):
    return tm.visual.material.PBRMaterial(
        name=name, baseColorFactor=rgba, metallicFactor=metallic,
        roughnessFactor=roughness, alphaMode="BLEND" if rgba[3] < 255 else "OPAQUE",
    )


def main():
    export_clip(-1, "screen-clip-left.stl")
    export_clip(1, "screen-clip-right.stl")
    scene = tm.Scene()
    glass = material("透明浅蓝罩", [153, 220, 245, 73], roughness=.16)
    shell = material("石墨灰后壳", [48, 56, 68, 255], roughness=.62)
    plastic = material("哑光托架", [173, 132, 80, 255], roughness=.65)
    rubber = material("黑色扎带", [30, 32, 36, 255], roughness=.75)
    steel = material("钢制紧固件", [185, 193, 205, 255], .85, .27)
    nickel = material("镀镍磁铁", [208, 219, 222, 255], .92, .24)
    pcb = material("绿色主板占位", [40, 118, 82, 255])
    lcd = material("屏幕框", [29, 39, 49, 255])
    pixels = material("青色显示面", [36, 190, 213, 255], roughness=.24)
    meshes = {key: tm.load_mesh(OUTPUT / (key+".stl"))
              for key in ("front-cover", "rear-shell", "carrier")}
    screen = box([-15.75, -15.98, -19.5], [15.75, -9, 19.5])
    board = box([-17, 8, -30], [17, 21.98, 30])

    def add(name, mesh, mat):
        mesh = mesh.copy()
        mesh.visual = tm.visual.TextureVisuals(material=mat)
        scene.add_geometry(mesh, node_name=name, geom_name=name)

    add("透明前罩", meshes["front-cover"], glass)
    add("后壳", meshes["rear-shell"], shell)
    add("内部托架", meshes["carrier"], plastic)
    add("LCD 包络", screen, lcd)
    # 用薄平面表示显示区，置于屏幕包络前表面。
    add("LCD 显示区", box([-12.5, -15.99, -14.5], [12.5, -15.98, 14.5]), pixels)
    add("ESP32-S3 包络", board, pcb)
    for side in (-1, 1):
        for z in (-12, 12):
            clamp = clip(side, z)
            for obstacle in (screen, board, *meshes.values()):
                overlap = intersect(clamp, obstacle)
                assert overlap.is_empty or min(overlap.extents) < .001 or abs(overlap.volume) < .05
            add(f"压片 {side} {z}", clamp, plastic)
            x = side*19.5
            add(f"压片螺杆 {side} {z}", cylinder_y(1.5, 16, x, -13.8, z), steel)
            add(f"压片螺钉头 {side} {z}", cylinder_y(2.7, 2, x, -4.6, z), steel)
            add(f"压片螺母 {side} {z}", cylinder_y(2.8, 2.4, x, -20.2, z), steel)
        for z in (-8, 8):
            for end in (-1, 1):
                add(f"磁铁 {side} {z} {end}", cylinder_y(3, 3, side*39, end*1.55, z), nickel)
        x = side*14
        add(f"底脚螺杆 {side}", cylinder_y(1.5, 20, x, 8, -33.5), steel)
        add(f"底脚螺钉头 {side}", cylinder_y(2.7, 2, x, -3, -33.5), steel)
        add(f"底脚螺母 {side}", cylinder_y(2.8, 2.4, x, 15.2, -33.5), steel)
    for z in (-17, 17):
        # 扎带从后框长孔穿过，跨过主板前表面，扣头留在前侧。
        tie = union(
            box([-19.45, 7.3, z-1.25], [19.45, 8, z+1.25]),
            box([-19.45, 25, z-1.25], [19.45, 25.7, z+1.25]),
            box([-19.45, 7.3, z-1.25], [-18.75, 25.7, z+1.25]),
            box([18.75, 7.3, z-1.25], [19.45, 25.7, z+1.25]),
            box([13, 5.6, z-2], [17, 7.5, z+2]),
        )
        add(f"主板扎带 {z}", tie, rubber)

    accessories = (
        ("speaker", "喇叭", [155, 96, 208, 255]),
        ("mic", "麦克风", [209, 91, 145, 255]),
        ("switch", "拨动开关", [65, 150, 216, 255]),
    )
    for key, label, rgba in accessories:
        add(label + "占位", box(*MODULE_BOUNDS[key]), material(label, rgba))

    for key, parking_clip in parking_clips():
        add("可拆接头卡座 " + key, parking_clip, plastic)

    # glTF 使用米及 Y 向上。STL 继续使用毫米。
    transform = tm.transformations.rotation_matrix(-np.pi/2, [1, 0, 0])
    transform[:3, :3] *= .001
    scene.apply_transform(transform)
    path = ROOT / "xiaohuan-assembly.glb"
    scene.export(path)
    checked = tm.load_scene(path)
    assert len(checked.geometry) == len(scene.geometry)
    assert any(g.visual.material.alphaMode == "BLEND" for g in checked.geometry.values())
    print(path.name, len(checked.geometry), "件", np.round(checked.extents*1000, 2), "mm")


if __name__ == "__main__":
    main()
