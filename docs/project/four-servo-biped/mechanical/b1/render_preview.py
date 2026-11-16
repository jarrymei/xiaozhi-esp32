"""从 CAD 三角网格生成样机的两幅正交预览。"""

import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
MESHES = json.loads((ROOT / "output/preview_meshes.json").read_text(encoding="utf-8"))
FONT = "C:/Windows/Fonts/msyh.ttc"
image = Image.new("RGB", (1540, 900), "#eef2f6")
draw = ImageDraw.Draw(image)


def label(x, y, value, size, color="#223246"):
    draw.text((x, y), value, font=ImageFont.truetype(FONT, size), fill=color, anchor="mt")


def panel(x, view, hidden):
    # 从观察方向计算水平和竖直基向量，按远近绘制模型三角面。
    camera = np.asarray(view, dtype=float)
    camera /= np.linalg.norm(camera)
    right = np.cross([0, 0, 1], camera)
    right /= np.linalg.norm(right)
    up = np.cross(camera, right)
    basis = np.array([right, -up, camera]).T
    polygons = []
    all_points = []
    lamp = np.array([-0.45, -0.55, 0.70])
    for name, color, vertices, faces in MESHES:
        if name in hidden:
            continue
        vertices = np.asarray(vertices)
        triangles = vertices[np.asarray(faces)]
        projected = triangles @ basis
        normals = np.cross(triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0])
        lengths = np.linalg.norm(normals, axis=1)
        normals = normals / np.maximum(lengths[:, None], 1e-10)
        shading = 0.65 + 0.40 * np.abs(normals @ lamp)
        all_points.append(projected[:, :, :2].reshape(-1, 2))
        for points, shade in zip(projected, shading):
            rgb = tuple(int(min(255, v*shade*255)) for v in color)
            polygons.append((points[:, 2].mean(), points[:, :2], rgb))
    xy = np.concatenate(all_points)
    low, high = xy.min(axis=0), xy.max(axis=0)
    scale = min(632/(high[0]-low[0]), 548/(high[1]-low[1]))
    center = (low + high) / 2
    for _, points, color in sorted(polygons, key=lambda value: value[0]):
        shifted = (points-center)*scale + [x, 479]
        draw.polygon([tuple(point) for point in shifted], fill=color)


label(770, 24, "B1  桌面机器人机械样机", 45)
label(770, 84, "舵机按用户图纸包络 · 耳外压条固定 · 摇臂孔按实物转印", 26, "#4b6277")
for left in (28, 784):
    draw.rounded_rectangle((left, 144, left+728, 808), 24, fill="white", outline="#d7e0e7", width=2)
label(392, 164, "正面装配", 30)
label(1148, 164, "拆下后盖查看舵机夹座与托盘", 30)
panel(392, (1.0, -3.6, 1.25), set())
panel(1148, (-1.2, 3.6, 1.05), {"rear_cover"})
label(770, 830, "黄色为舵机占位 · 绿色为开发板占位 · 样机外包约 108 × 73 × 155 mm（含耳罩）", 25, "#4b6277")
image.save(ROOT / "output/装配预览.png")
