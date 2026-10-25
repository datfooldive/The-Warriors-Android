#!/usr/bin/env python3
"""The on-screen controls' icons as vector strokes: each native/ui/<name>.svg (24 x 24 viewBox, stroked paths and
circles, a group's translate and scale) flattened to polylines in native/ui_paths.h, which touch.cpp draws with ImGui
at the gear's line weight. Edit an .svg, run this, commit both.

Each icon is a run of floats: a kind (0 open line, 1 closed line, 2 circle, 3 filled circle), then for a line its point
count and the points, for a circle its centre and radius; 24 x 24 units, a -1 kind ending it."""
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ICONS = ["light", "heavy", "grab", "action", "sprint", "target", "look", "guard", "command"]
NS = "{http://www.w3.org/2000/svg}"


def arc_points(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG's endpoint arc as points after its start (the spec's centre conversion)."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(2, int(math.ceil(abs(dt) / (math.pi / 16))))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return pts


def cubic(p0, p1, p2, p3, n=12):
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


def path_lines(d):
    """Polylines of a path: (points, closed) each."""
    tokens = re.findall(r"[MmLlHhVvAaCcSsZz]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?", d)
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    last_c2 = None
    lines, cur = [], []

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def flag():
        # arc flags may run together with the next number ("0 0 1 3-3" or "011")
        nonlocal i
        t = tokens[i]
        if len(t) > 1 and t[0] in "01" and not t.startswith(("0.", "1.")):
            tokens[i] = t[1:]
            return int(t[0])
        i += 1
        return int(float(t))

    def finish(closed):
        nonlocal cur
        if len(cur) > 1:
            lines.append((cur, closed))
        cur = []

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                finish(True)
                x, y = sx, sy
                cmd = None
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            finish(False)
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            sx, sy = x, y
            cur = [(x, y)]
            cmd = "l" if rel else "L"
            last_c2 = None
        elif c == "L":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
            last_c2 = None
        elif c == "H":
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
            last_c2 = None
        elif c == "V":
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
            last_c2 = None
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = flag(), flag()
            nx, ny = num(), num()
            if rel:
                nx, ny = x + nx, y + ny
            cur += arc_points(x, y, rx, ry, math.radians(phi), large, sweep, nx, ny)
            x, y = nx, ny
            last_c2 = None
        elif c in "CS":
            if c == "C":
                c1 = (num(), num())
                if rel:
                    c1 = (x + c1[0], y + c1[1])
            else:
                c1 = (2 * x - last_c2[0], 2 * y - last_c2[1]) if last_c2 else (x, y)
            c2 = (num(), num())
            p = (num(), num())
            if rel:
                c2 = (x + c2[0], y + c2[1])
                p = (x + p[0], y + p[1])
            cur += cubic((x, y), c1, c2, p)
            last_c2 = c2
            x, y = p
        else:
            raise SystemExit(f"path command {cmd} not handled")
    finish(False)
    return lines


def transform_of(el):
    t = el.get("transform", "")
    tx = ty = 0.0
    k = 1.0
    m = re.search(r"translate\(([-\d.]+)[ ,]+([-\d.]+)\)", t)
    if m:
        tx, ty = float(m.group(1)), float(m.group(2))
    m = re.search(r"scale\(([-\d.]+)\)", t)
    if m:
        k = float(m.group(1))
    return lambda p: (p[0] * k + tx, p[1] * k + ty)


def shapes(svg):
    root = ET.parse(svg).getroot()
    out = []

    def walk(el, xf):
        if el.tag == NS + "g":
            inner = transform_of(el)
            for ch in el:
                walk(ch, lambda p, inner=inner: xf(inner(p)))
        elif el.tag == NS + "path":
            for pts, closed in path_lines(el.get("d")):
                out.append(("line", [xf(p) for p in pts], closed))
        elif el.tag == NS + "circle":
            c = xf((float(el.get("cx")), float(el.get("cy"))))
            r = float(el.get("r"))
            out.append(("circle", c, r, el.get("fill", "none") != "none"))

    for ch in root:
        walk(ch, lambda p: p)
    return out


def main():
    ui = Path(__file__).resolve().parent.parent / "native" / "ui"
    lines = ["// written by tools/ui_paths.py from native/ui/*.svg: each icon's strokes, 24 x 24 units",
             "#pragma once", "", "namespace ui_paths {", ""]
    for name in ICONS:
        vals = []
        for s in shapes(ui / f"{name}.svg"):
            if s[0] == "line":
                _, pts, closed = s
                # a closing point on the start is the closure itself
                if closed and len(pts) > 2 and math.dist(pts[0], pts[-1]) < 1e-3:
                    pts = pts[:-1]
                vals += [1 if closed else 0, len(pts)] + [v for p in pts for v in p]
            else:
                _, c, r, filled = s
                vals += [3 if filled else 2, c[0], c[1], r]
        vals.append(-1)
        body = ", ".join(f"{v:.3f}f".replace(".000f", ".0f") for v in vals)
        lines.append(f"constexpr float k{name.capitalize()}[] = {{{body}}};")
    lines += ["", "}  // namespace ui_paths", ""]
    (ui.parent / "ui_paths.h").write_text("\n".join(lines))
    print(f"native/ui_paths.h: {len(ICONS)} icons")


if __name__ == "__main__":
    sys.exit(main())
