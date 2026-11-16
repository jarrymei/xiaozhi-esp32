"""读取固定版本的原版EMO二进制STL，记录外包尺寸；不把外包尺寸当作安装腔尺寸。"""

import hashlib
import json
import struct
import urllib.request
import urllib.parse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

COMMIT = "184f32e354968001d18d6c4e7f21e2a79278a546"
API = (
    "https://api.github.com/repos/maker-community/VerdureLab/contents/"
    f"verdure-emo?ref={COMMIT}"
)


def fetch(url):
    # 中文模型文件名需要转换为URL编码
    url = urllib.parse.quote(url, safe=":/?=&%")
    request = urllib.request.Request(url, headers={"User-Agent": "robot-project-docs"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def measure(item):
    data = fetch(item["download_url"])
    count = struct.unpack_from("<I", data, 80)[0]
    if len(data) != 84 + 50 * count:
        raise ValueError(f"不是预期的二进制STL：{item['name']}")
    lower = [float("inf")] * 3
    upper = [float("-inf")] * 3
    # 每个三角面由法向量、三个顶点和属性构成；只统计顶点外包范围
    for index in range(count):
        vertices = struct.unpack_from("<9f", data, 84 + index * 50 + 12)
        for offset in (0, 3, 6):
            for axis in range(3):
                value = vertices[offset + axis]
                lower[axis] = min(lower[axis], value)
                upper[axis] = max(upper[axis], value)
    return {
        "file": item["name"],
        "size_xyz_model_units": [round(upper[i] - lower[i], 3) for i in range(3)],
        "triangles": count,
        "sha256": hashlib.sha256(data).hexdigest(),
        "source": item["download_url"],
    }


def main():
    files = json.loads(fetch(API))
    files = [item for item in files if item["name"].endswith(".stl")]
    with ThreadPoolExecutor(max_workers=4) as pool:
        measured = list(pool.map(measure, files))
    report = {
        "commit": COMMIT,
        "note": "STL本身没有单位；数值按该类模型通常使用的毫米解释，打印前须在切片软件确认。外包尺寸不是内腔、PCB尺寸或螺孔间距，不能单独证明兼容。",
        "models": measured,
    }
    output = Path(__file__).resolve().parents[1] / "references" / "EMO原版模型尺寸核查.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    for item in measured:
        print(item["file"], item["size_xyz_model_units"])


if __name__ == "__main__":
    main()
