"""Posições de conectores em vista breadboard, em unidades de cena do Fritzing (90/in)."""
import re, glob, os, functools
import xml.etree.ElementTree as ET

PARTS = "/Applications/Fritzing.app/Contents/Resources/fritzing-parts"
DPI = 90.0
SVGNS = "{http://www.w3.org/2000/svg}"


@functools.lru_cache(maxsize=None)
def _todas_fzp():
    return tuple(glob.glob(os.path.join(PARTS, "**", "*.fzp"), recursive=True))


@functools.lru_cache(maxsize=None)
def acha_fzp(module_id):
    for a in _todas_fzp():
        t = open(a, encoding="utf-8", errors="replace").read()
        if re.search(rf'moduleId="{re.escape(module_id)}"', t):
            return a, t
    return None, None


def _polegadas(v):
    m = re.match(r"([-\d.eE]+)\s*(in|mm|cm|px|pt)?$", v.strip())
    if not m: return None
    n, u = float(m.group(1)), (m.group(2) or "px")
    return {"in": n, "mm": n/25.4, "cm": n/2.54, "px": n/90.0, "pt": n/72.0}[u]


def _num(e, a, padrao=0.0):
    v = e.get(a)
    if v is None: return padrao
    try: return float(re.match(r"[-\d.eE]+", v.strip()).group(0))
    except (AttributeError, ValueError): return padrao


def _aplica(t, x, y):
    """Aplica uma string de transform SVG a (x,y). Suporta translate/scale/matrix."""
    for m in reversed(list(re.finditer(r"(translate|scale|matrix)\s*\(([^)]*)\)", t or ""))):
        f = m.group(1)
        p = [float(v) for v in re.split(r"[,\s]+", m.group(2).strip()) if v]
        if f == "translate":
            x += p[0]; y += (p[1] if len(p) > 1 else 0.0)
        elif f == "scale":
            sx = p[0]; sy = p[1] if len(p) > 1 else sx
            x *= sx; y *= sy
        elif f == "matrix" and len(p) == 6:
            a, b, c, d, e, f2 = p
            x, y = a*x + c*y + e, b*x + d*y + f2
    return x, y


def _centro_local(e):
    tag = e.tag.replace(SVGNS, "")
    if tag in ("circle", "ellipse"):
        return _num(e, "cx"), _num(e, "cy")
    if tag in ("rect", "image", "use", "svg"):
        return _num(e, "x") + _num(e, "width")/2, _num(e, "y") + _num(e, "height")/2
    if tag == "line":
        return (_num(e, "x1") + _num(e, "x2"))/2, (_num(e, "y1") + _num(e, "y2"))/2
    if tag == "polygon" or tag == "polyline":
        pts = [float(v) for v in re.split(r"[,\s]+", (e.get("points") or "").strip()) if v]
        if len(pts) >= 2:
            xs, ys = pts[0::2], pts[1::2]
            return sum(xs)/len(xs), sum(ys)/len(ys)
    if tag == "path":
        nums = [float(v) for v in re.findall(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", e.get("d") or "")]
        if len(nums) >= 2: return nums[0], nums[1]
    return None


def _centro(raiz, pais, svg_id):
    alvo = None
    for e in raiz.iter():
        if e.get("id") == svg_id:
            alvo = e; break
    if alvo is None: return None
    c = _centro_local(alvo)
    if c is None:
        for f in alvo.iter():                       # tenta um filho desenhável
            if f is alvo: continue
            c = _centro_local(f)
            if c is not None: break
    if c is None: return None
    x, y = c
    cadeia, e = [], alvo
    while e is not None:
        cadeia.append(e); e = pais.get(id(e))
    for e in cadeia:                                # do alvo para a raiz
        x, y = _aplica(e.get("transform"), x, y)
    return x, y


def mapa_conectores(fzp_txt):
    out = {}
    for m in re.finditer(r'<connector\b[^>]*\bid="(connector\d+)".*?</connector>', fzp_txt, re.S):
        b = m.group(0)
        p = re.search(r'<breadboardView>\s*<p\b[^>]*svgId="([^"]*)"', b, re.S)
        t = re.search(r'<breadboardView>\s*<p\b[^>]*terminalId="([^"]*)"', b, re.S)
        if p: out[m.group(1)] = (p.group(1), t.group(1) if t else None)
    return out


def _svg_breadboard(fzp_txt, fzp_path, embutido=None):
    if embutido is not None: return embutido
    m = re.search(r'<breadboardView[^>]*>\s*<layers\b[^>]*image="([^"]*)"', fzp_txt, re.S)
    if not m: return None
    rel = m.group(1)
    grupo = os.path.basename(os.path.dirname(fzp_path))          # core / contrib / obsolete
    for cand in (os.path.join(PARTS, "svg", grupo, rel),
                 os.path.join(PARTS, "svg", "core", rel),
                 os.path.join(PARTS, "svg", "contrib", rel),
                 os.path.join(PARTS, "svg", "obsolete", rel)):
        if os.path.exists(cand):
            return open(cand, encoding="utf-8", errors="replace").read()
    g = glob.glob(os.path.join(PARTS, "svg", "*", "**", os.path.basename(rel)), recursive=True)
    return open(g[0], encoding="utf-8", errors="replace").read() if g else None


def offsets(module_id, svg_embutido=None, fzp_embutido=None):
    """connectorId -> (dx, dy) em unidades de cena a partir da origem da peça."""
    if fzp_embutido is not None:
        fzp_txt, fzp_path = fzp_embutido, "embutido/x.fzp"
    else:
        fzp_path, fzp_txt = acha_fzp(module_id)
        if fzp_txt is None: return None, "fzp não encontrado"
    svg = _svg_breadboard(fzp_txt, fzp_path, svg_embutido)
    if svg is None: return None, "svg de breadboard não encontrado"

    w = re.search(r'<svg[^>]*\bwidth="([^"]*)"', svg, re.S)
    vb = re.search(r'<svg[^>]*\bviewBox="([^"]*)"', svg, re.S)
    if not (w and vb): return None, "viewBox/width ausentes"
    pol = _polegadas(w.group(1))
    p = [float(v) for v in re.split(r"[,\s]+", vb.group(1).strip()) if v]
    if pol is None or len(p) != 4: return None, "viewBox/width ilegíveis"
    vbx, vby, vbw = p[0], p[1], p[2]
    fator = pol / vbw * DPI

    try:
        raiz = ET.fromstring(re.sub(r'\sxmlns="[^"]*"', "", svg, count=1))
    except ET.ParseError as e:
        return None, f"svg malformado: {e}"
    pais = {}
    for pai in raiz.iter():
        for f in pai:
            pais[id(f)] = pai

    out, faltas = {}, []
    for cid, (sid, tid) in mapa_conectores(fzp_txt).items():
        c = _centro(raiz, pais, tid) if tid else None
        if c is None: c = _centro(raiz, pais, sid)
        if c is None: faltas.append(cid); continue
        out[cid] = ((c[0]-vbx)*fator, (c[1]-vby)*fator)
    return out, (f"sem posição: {faltas}" if faltas else None)
