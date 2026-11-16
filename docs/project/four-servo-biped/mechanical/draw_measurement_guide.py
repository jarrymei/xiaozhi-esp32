"""绘制舵机和摇臂测量位置，不使用标称数值或真实零件比例。"""

from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
im = Image.new("RGB", (1800, 2670), "white")
d = ImageDraw.Draw(im)
INK, BLUE, ORANGE, FAINT = "#233346", "#1264a3", "#a84414", "#9aabb7"
font_path = "C:/Windows/Fonts/msyh.ttc"


def txt(x, y, s, size=28, color=INK, center=False):
    d.text((x, y), s, font=ImageFont.truetype(font_path, size), fill=color,
           anchor="mt" if center else "lt")


def line(points, color=INK, width=4):
    d.line(points, fill=color, width=width)


def arrow(a, b, color=BLUE):
    line([a, b], color, 3)
    theta = math.atan2(b[1]-a[1], b[0]-a[0])
    for delta in (-0.45, 0.45):
        p = (b[0]-14*math.cos(theta+delta), b[1]-14*math.sin(theta+delta))
        line([b, p], color, 3)


def hd(x1, x2, y, obj_y, label):
    # 尺寸界线从实际测量边/中心延伸到尺寸线。
    for x in (x1, x2):
        line([(x, obj_y), (x, y+10 if y > obj_y else y-10)], FAINT, 2)
    arrow((x1, y), (x2, y))
    arrow((x2, y), (x1, y))
    txt((x1+x2)/2, y-39, label, 29, BLUE, True)


def vd(y1, y2, x, obj_x, label, text_x=None, text_y=None):
    for y in (y1, y2):
        line([(obj_x, y), (x+10, y)], FAINT, 2)
    arrow((x, y1), (x, y2))
    arrow((x, y2), (x, y1))
    txt(text_x if text_x is not None else x+18,
        text_y if text_y is not None else (y1+y2)/2-16, label, 28, BLUE)


def hole(x, y, r=18):
    d.ellipse((x-r, y-r, x+r, y+r), fill="white", outline=INK, width=3)
    line([(x-r-8, y), (x+r+8, y)], FAINT, 2)
    line([(x, y-r-8), (x, y+r+8)], FAINT, 2)


txt(65, 30, "舵机与摇臂尺寸测量图｜A1～A10、B1～B5", 43)
txt(65, 95, "单位：毫米　｜　全部断电测量　｜　示意非比例，不要用图上长度换算尺寸", 29, ORANGE)
txt(65, 150, "蓝色双箭头＝测量起止位置；灰色细线＝边或中心的延长线。圆孔只是示例。", 27)

txt(65, 215, "① 舵机俯视：从输出轴上方向下看（先取下摇臂）", 34)
# 壳体、耳片和轴座仅作测量基准示意。
d.rectangle((240, 495, 1160, 605), fill="#dce5eb", outline=INK, width=4)
d.rectangle((350, 430, 1050, 680), fill="#eaf0f4", outline=INK, width=4)
hole(290, 550)
hole(1110, 550)
d.ellipse((445, 495, 555, 605), fill="#b9cad5", outline=INK, width=4)
hole(500, 550, 26)
txt(500, 590, "O 轴心", 27, center=True)
hd(240, 1160, 320, 495, "A4　含安装耳总长")
hd(350, 1050, 385, 430, "A1　主体长L（不含耳）")
hd(290, 1110, 755, 550, "A5　两安装孔中心距")
vd(430, 680, 1230, 1050, "A2 主体宽W")
hd(350, 500, 845, 550, "A9 近端→轴心")
vd(430, 550, 880, 500, "A10 侧边→轴心", 930, 470)
txt(1030, 520, "选图中上侧边作基准", 24)
arrow((145, 660), (277, 561), ORANGE)
txt(65, 690, "A6 安装孔直径", 28, ORANGE)
txt(65, 730, "长槽改量宽×长", 25, ORANGE)
txt(70, 420, "近端E", 28, ORANGE)
arrow((190, 450), (350, 455), ORANGE)
txt(1190, 750, "A5、A9、A10都量到中心", 25)
txt(1190, 795, "不要量到孔边或轴边", 25)
txt(65, 900, "A10：从选定的一侧主体边量到轴心；A9：从离轴较近的短端面E量到轴心。", 28)
line([(65, 965), (1735, 965)], FAINT, 2)

txt(65, 1000, "② 舵机侧视：沿长边看，输出轴朝上（高度方向放大）", 34)
d.rectangle((350, 1190, 1050, 1500), fill="#eaf0f4", outline=INK, width=4)
d.rectangle((240, 1280, 350, 1320), fill="#dce5eb", outline=INK, width=4)
d.rectangle((1050, 1280, 1160, 1320), fill="#dce5eb", outline=INK, width=4)
d.rectangle((445, 1155, 555, 1190), fill="#b9cad5", outline=INK, width=4)
d.rectangle((474, 1105, 526, 1155), fill="#d3b75b", outline=INK, width=4)
txt(600, 1110, "输出轴、轴座凸台不计入A3", 27, ORANGE)
txt(390, 1205, "顶面T", 28)
txt(390, 1455, "底面", 28)
vd(1190, 1500, 1225, 1050, "A3 主体高H", 1245, 1430)
vd(1280, 1320, 160, 240, "A7 耳厚", 65, 1345)
# A8使用两个独立尺寸，避免把顶面到耳片高度误当耳厚。
vd(1190, 1280, 1430, 1160, "A8① 到耳上表面", 1460, 1210)
vd(1190, 1320, 1380, 1160, "A8② 到耳下表面", 1460, 1310)
txt(65, 1560, "A8请填两个数：顶面T→耳上表面、顶面T→耳下表面。两耳均需核对。", 28)
txt(65, 1610, "A3到主体壳体顶面为止，不把顶部凸台、输出轴或摇臂算进去。", 28)
line([(65, 1670), (1735, 1670)], FAINT, 2)

txt(65, 1700, "③ 摇臂：B1先记录形状；下面以双臂型举例", 34)
txt(65, 1760, "B1不是长度：拍全部附件的正反面，注明单臂／双臂／十字／圆盘。", 28, ORANGE)
d.rounded_rectangle((180, 1900, 860, 1970), radius=30, fill="#ede9db", outline=INK, width=4)
d.ellipse((465, 1875, 575, 1985), fill="#ddd5bd", outline=INK, width=4)
for x in (270, 380, 660, 770):
    hole(x, 1935, 13)
hole(520, 1935, 25)
txt(520, 1990, "O 中央轴心", 25, center=True)
hd(180, 860, 1845, 1900, "B2 总长")
vd(1875, 1985, 1000, 575, "B2 最大宽（含轮毂）", 1030, 1905)
hd(520, 660, 2070, 1935, "B4 O→孔1")
hd(520, 770, 2140, 1935, "B4 O→孔2")
txt(1060, 2050, "B4：向两侧逐孔量轴心到孔心", 27)
txt(1060, 2095, "不能拿相邻两小孔间距代替", 26)
arrow((140, 2070), (259, 1942), ORANGE)
txt(65, 2110, "B5 小孔直径", 28, ORANGE)
txt(65, 2155, "不量中央花键孔", 25, ORANGE)

txt(65, 2230, "摇臂侧视：B3分别量薄板和中央轮毂，朝向以实物为准", 30)
d.rectangle((180, 2350, 860, 2390), fill="#ede9db", outline=INK, width=4)
d.rectangle((450, 2390, 590, 2470), fill="#ddd5bd", outline=INK, width=4)
hd(450, 590, 2320, 2390, "B3 轮毂外径")
vd(2350, 2390, 950, 860, "B3 板厚", 980, 2340)
vd(2350, 2470, 1320, 590, "B3 中央轮毂总高", 1350, 2430)
txt(65, 2520, "B4两侧不必对称，B5不同孔径分别记；圆盘B2量直径，十字臂量整体长×宽。", 27)
txt(65, 2570, "有卡尺按读数记录；直尺只填约值。量不准填“待测”，不要按图猜实物尺寸。", 27)
im.save(ROOT / "servo-horn-measurement-guide.png")
