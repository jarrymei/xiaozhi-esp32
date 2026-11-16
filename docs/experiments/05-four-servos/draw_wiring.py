"""绘制四舵机分配板示意；仅表示连接关系，不表示实物孔号或尺寸。"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
FONT = "C:/Windows/Fonts/msyh.ttc"
INK, RED, GROUND, SIGNAL = "#182638", "#c72e38", "#333e4c", "#1268b3"
im = Image.new("RGB", (1600, 1770), "#ffffff")
d = ImageDraw.Draw(im)


def text(x, y, value, size=26, color=INK, center=False):
    # 图内文字使用中文字体，居中锚点便于正反面保持一致。
    d.text((x, y), value, font=ImageFont.truetype(FONT, size), fill=color,
           anchor="mt" if center else "lt")


def line(points, color, width=7):
    d.line(points, fill=color, width=width, joint="curve")


def pad(x, y, color):
    d.ellipse((x-11, y-11, x+11, y+11), fill=color, outline="white", width=2)


def board(top, back=False):
    # 沿左右方向翻面，顶部始终朝上；背面左右顺序镜像，行序不变。
    left, right = 95, 1135
    d.rectangle((left, top, right, top+360), fill="#f5efdc", outline="#8a7b57", width=3)
    for x in range(120, 1120, 25):
        for y in range(top+25, top+350, 25):
            d.ellipse((x-2, y-2, x+2, y+2), fill="#bfb492")
    mx = right-18 if back else left+18
    d.rectangle((mx-8, top+10, mx+8, top+26), fill="#ab43a0")
    xs = [245, 470, 695, 920]
    entries = [("J1", 17), ("J2", 18), ("J3", 13), ("J4", 14)]
    if back:
        entries.reverse()
    gy, vy, sy, iy = [top+n for n in (85, 145, 205, 310)]
    for x, (name, gpio) in zip(xs, entries):
        text(x, top+30, name, 28, center=True)
        if back:
            line([(x, sy), (x, iy)], SIGNAL)
        else:
            d.rectangle((x-22, gy-22, x+22, sy+22), fill="#313942")
        for y, c in ((gy, GROUND), (vy, RED), (sy, SIGNAL), (iy, SIGNAL)):
            pad(x, y, c)
        text(x, iy+20, f"← GPIO{gpio}", 23, SIGNAL, True)
    if back:
        # 两条母线各占一行，信号在下方引出，不与电源线交叉。
        for y, c in ((gy, GROUND), (vy, RED)):
            line([(155, y), (1070, y)], c, 9)
            for x in xs:
                pad(x, y, c)
    for y, label, c in ((gy, "G", GROUND), (vy, "V", RED), (sy, "S", SIGNAL)):
        text(120, y-15, label, 24, c)
    text(1170, gy-13, "G：四路负极共接", 24, GROUND)
    text(1170, vy-13, "V：四路5V共接", 24, RED)
    text(1170, sy-13, "S：四路分别引出", 24, SIGNAL)
    text(1170, iy-10, "下排为信号输入针", 23)


text(60, 35, "四舵机洞洞板：焊接与接线示意", 43)
text(60, 100, "J1→17　J2→18　J3→13　J4→14　｜　G=负极　V=外部5V　S=信号", 28)
text(60, 150, "适用独立焊盘板；位置非实物孔号、非比例。长条连通板须另核对，不能直接照焊。", 26, RED)
text(60, 210, "① 元件面：看到排针塑料座的一面（四个舵机插这里）", 30)
board(270)
text(70, 650, "紫色方块是自己画的方向标记；先标G/V/S。四组排针竖排，上G、中V、下S。", 26)
text(60, 720, "② 焊接面：保持顶部朝上，像翻书一样左右翻面", 30)
board(780, True)
text(70, 1160, "背面从左到右变成J4、J3、J2、J1；G/V/S上下不变。实线为要焊的导线。", 26)
text(60, 1230, "③ 电源与主控：电源线焊到背面的对应母线", 30)

# 总接线图单独表示，避免把元件外形误认为精确孔位。
d.rectangle((70, 1305, 465, 1535), outline=INK, width=3)
text(100, 1330, "独立稳压5V电源", 30)
text(100, 1385, "经5525母头线引出", 25)
line([(460, 1445), (800, 1445)], RED, 9)
text(510, 1400, "测定正极 → V母线", 26, RED)
line([(460, 1500), (800, 1500)], GROUND, 9)
text(510, 1515, "测定负极 → G母线", 26, GROUND)
d.rectangle((800, 1320, 1530, 1540), outline=INK, width=3)
text(830, 1340, "四路分配板", 31)
text(830, 1390, "四个V接红线；四个G接黑线", 26)
text(830, 1440, "ESP32 GND也接G母线（单独一根线）", 26)
text(830, 1490, "四个S分别接17 / 18 / 13 / 14", 26, SIGNAL)
text(70, 1590, "ESP32继续USB供电；外部5V不要接开发板5V、3V3或USB。", 29, RED)
text(70, 1640, "主电源母线用合适载流能力的铜线，不能由细杜邦线承载四颗总电流。", 26)
text(70, 1690, "断电查短路 → 不插舵机测每组V/G约5V且极性正确 → 断电后再接线。", 26)
im.save(ROOT / "four-servos-wiring.png")
