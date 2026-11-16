// 单位为毫米。原机比例未知，以下均为概念设计参数。
diameter = 90;
wall = 2;
flat_cut = 7;
part = "assembly"; // [assembly,front,rear,clearance]
show_hardware = true;
r = diameter / 2;
floor_z = -r + flat_cut;
$fn = 120;

// 前后壳共用同一球心和底平面，接缝位于 y=0。
module half_space(front) {
    translate([-r, front ? -r : 0, floor_z])
        cube([2*r, r, r-floor_z]);
}

module shell(front) {
    intersection() {
        difference() {
            sphere(r);
            intersection() {
                sphere(r-wall);
                translate([-r,-r,floor_z+wall])
                    cube([2*r,2*r,2*r]);
            }
        }
        half_space(front);
    }
}

// 彩屏、开发板和排线仅为占位。屏幕朝向负 y。
module hardware() {
    color("black") translate([-16,-17,-19]) cube([32,7,38]);
    color("cyan") translate([-12.5,-17.1,-14.5]) cube([25,0.1,29]);
    color("seagreen") translate([-17,8,-30]) cube([34,14,60]);
    color("orange") translate([-10,-10,-28]) cube([20,18,9]);
}

if (part == "front") shell(true);
if (part == "rear") shell(false);
if (part == "assembly") {
    color([0.68,0.9,1,0.25]) shell(true);
    color([0.23,0.25,0.3]) shell(false);
    if (show_hardware) hardware();
}
if (part == "clearance") {
    // F6 后应为空。只检查占位盒与壳体是否相交。
    intersection() {
        union() { shell(true); shell(false); }
        hardware();
    }
}
