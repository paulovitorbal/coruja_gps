#!/usr/bin/env python3
"""
gera_fritzing.py — gera coruja_gps.fzz a partir da tabela de ligações

Produz um sketch do Fritzing (1.0.x) com o circuito do detector de radares.
A fonte da verdade é a tabela NETS abaixo, transcrita de bom_schematic.md;
mudou a fiação, edite aqui e regenere.

Os moduleId e os IDs de conector foram VERIFICADOS contra a biblioteca
instalada em /Applications/Fritzing.app (2.565 peças) em 2026-09-17.

Quatro módulos não têm peça na biblioteca (NEO-M8N, display, breakout microSD
Adafruit, KY-040) e são representados por headers fêmea genéricos com a
contagem correta de pinos e rótulo no título. A peça do microSD que existe na
biblioteca tem ZERO conectores — é inutilizável.

VISTA PROTOBOARD: fios reais, com geometria calculada a partir da posição
verdadeira de cada pino. As posições vêm de fritzing_geo.py, que lê o SVG de
breadboard de cada peça, resolve o mapeamento connector->svgId do .fzp e aplica
os transforms de grupo. A escala da cena (90 unidades por polegada) foi medida
empiricamente contra o sketch jogo-perguntas.fzz, com erro zero em 6 amostras.

Cores dos fios por rede (CORES abaixo): preto = GND, vermelho = 5V,
azul = 3V3, e cores distintas por grupo de sinal para facilitar o rastreio
durante a soldagem na perfboard.

VISTAS ESQUEMÁTICO E PCB: conexões diretas peça-a-peça, sem fio. O Fritzing
desenha o ratsnest nos pinos certos. Use "Rotear" se quiser traços sólidos.

    python3 gera_fritzing.py
"""
import os
import shutil
import sys
import argparse
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fritzing_geo as geo

AQUI = Path(__file__).resolve().parent
SAIDA_PADRAO = AQUI / "coruja_gps.fzz"

# A peça do Pico 2 W não vem na biblioteca padrão do Fritzing: ela é copiada de
# um sketch que já a tenha. O .fzz versionado deste projeto já a carrega
# embutida, então ele serve de fonte e o gerador fica autossuficiente para quem
# clona o repositório. O sketch externo fica como alternativa.
FONTES_DA_PECA_PICO = [
    AQUI / "coruja_gps.fzz",
    Path.home() / "Documents" / "Fritzing" / "jogo-perguntas.fzz",
]
FRITZING_VERSION = "1.0.7.2026-04-14.CD-2576-0-394a8bb4"

# ------------------------------------------------------------- netlist ------
# PECAS, CONN, pino() e NETS vivem em `netlist.py`, que e a fonte unica:
# o gerador do KiCad le o mesmo dado. Duas transcricoes da mesma netlist
# divergiriam, e divergiriam em silencio.
from netlist import CONN, NETS, PECAS, pino  # noqa: E402

POS = {
    "PICO": (600, 300),
    "GPS": (1100, 180), "SD": (1100, 380), "TFT": (1100, 600), "ENC": (1100, 840),
    "J12V": (60, 60), "F1": (170, 60), "TVS1": (260, 140), "C5": (330, 140),
    "CONV": (120, 240), "D1": (300, 120), "C1": (420, 200), "C2": (500, 200),
    "R1": (250, 560), "R2": (250, 640), "R3": (250, 720), "LED": (80, 640),
    "R4": (250, 900), "Q1": (120, 960), "BZ": (400, 1020),
    "R5": (380, 60),
}

# Paleta: uma cor por TENSAO, e verde para todo sinal.
#
# Decidido pelo autor em 2026-09-29, substituindo o esquema anterior, que
# dava cor propria a cada grupo de sinal (SPI0, SPI1, display, GPS, encoder,
# LED, buzzer -- sete cores).
#
# O que se ganha: conferir alimentacao de relance. Na perfboard, o erro que
# custa caro e de tensao, nao de sinal -- trocar um fio de sinal da um
# periferico que nao responde, trocar 12 V por 3,3 V destroi Pico, GPS,
# display e cartao juntos. Quatro cores memorizaveis servem a esse erro
# melhor que onze.
#
# As cinco cores sao as da caixa de fios do autor, 10 m de cada. Uma
# convencao que exige comprar fio avulso e abandonada no meio da montagem;
# esta sai do estoque que ja esta na bancada.
#
# O que se perde: rastreabilidade visual por grupo de sinal. Com tudo verde,
# distinguir o SCL do cartao do SCL do display exige seguir o fio ou
# consultar a netlist. E consequencia aceita, nao descuido.
#
# ⚠️ **12 V NAO e vermelho, de proposito** -- e a unica regra herdada do
# esquema anterior. A convencao antiga mandava vermelho para "5 V e 12 V";
# com o 12 V dentro do gabinete, duas tensoes na mesma cor a 3 cm uma da
# outra e convite ao erro descrito acima. Amarelo o separa.
CORES = {
    "GND":           "#000000",   # preto
    "12V_ENTRADA":   "#ffe500",   # amarelo
    "12V_PROTEGIDO": "#ffe500",   # amarelo
    "5V_CONV":       "#ff1a1a",   # vermelho
    "VSYS_5V":       "#ff1a1a",   # vermelho
    "3V3":           "#418dd9",   # azul
}
# Todo o resto e sinal, e sinal e verde.
COR_PADRAO = "#4faf4e"

# Posição na vista protoboard. Espaçamento generoso para os fios ficarem
# legíveis; arraste no Fritzing como preferir.
POS_BB = {
    "PICO": (300, 120),
    "GPS": (620, 60), "SD": (620, 150), "TFT": (620, 240), "ENC": (620, 330),
    "J12V": (40, 40), "F1": (120, 40), "TVS1": (180, 100), "C5": (230, 100),
    "CONV": (60, 170), "D1": (170, 60), "C1": (150, 150), "C2": (230, 150),
    "R1": (60, 380), "R2": (60, 420), "R3": (60, 460),
    "LED": (60, 520), "R4": (60, 600), "Q1": (180, 620),
    "BZ": (320, 640), "R5": (430, 60),
}

VISTAS = [("breadboardView", "breadboard"),
          ("schematicView", "schematic"),
          ("pcbView", "copper0")]


def liga(nets):
    """Expande cada rede em pares encadeados: a-b, b-c, c-d..."""
    pares = []
    for nome, pontos in nets.items():
        for i in range(len(pontos) - 1):
            pares.append((pontos[i], pontos[i + 1], nome))
    return pares


def fonte_da_peca_pico():
    """Primeiro arquivo disponível que contenha a peça do Pico."""
    for caminho in FONTES_DA_PECA_PICO:
        if caminho.exists():
            with zipfile.ZipFile(caminho) as z:
                if any("rpi_pico" in n for n in z.namelist()):
                    return caminho
    return None


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Gera o .fzz a partir da tabela de ligações deste arquivo.")
    p.add_argument("--saida", type=Path, default=SAIDA_PADRAO,
                   help=f"arquivo a escrever; padrão {SAIDA_PADRAO.name}")
    p.add_argument("--forcar", action="store_true",
                   help="sobrescreve um arquivo existente; ver o aviso abaixo")
    args = p.parse_args(argv)
    SAIDA = args.saida

    # Proteção do R-29: o .fzz versionado foi editado à mão — roteamento de
    # fios, posicionamento e notas — e regerar por cima DESTRÓI esse trabalho,
    # que não está em nenhum outro lugar. O gerador é a fonte da NETLIST; o
    # .fzz é a fonte do LAYOUT. Para comparar visualmente, gere em outro nome.
    if SAIDA.exists() and not args.forcar:
        print(f"erro: {SAIDA.name} já existe e não será sobrescrito.\n"
              f"\n"
              f"  Esse arquivo pode ter edição manual de layout que o gerador\n"
              f"  não sabe reproduzir: roteamento de fios, posição das peças e\n"
              f"  notas. Regerar por cima apaga tudo isso.\n"
              f"\n"
              f"  Para comparar visualmente, gere com outro nome:\n"
              f"      python3 {Path(__file__).name} --saida coruja_gps_v4.fzz\n"
              f"\n"
              f"  Se tem certeza de que quer descartar o layout atual:\n"
              f"      python3 {Path(__file__).name} --forcar\n",
              file=sys.stderr)
        return 1

    SKETCH_COM_PICO = fonte_da_peca_pico()
    if SKETCH_COM_PICO is None:
        raise SystemExit(
            "erro: preciso da peça do Pico embutida em algum destes:\n"
            + "".join(f"  {c}\n" for c in FONTES_DA_PECA_PICO)
            + "       (part.rpi_pico-tht_1.fzp e seus 3 SVGs)"
        )

    idx = {pid: 100 + i for i, (pid, *_r) in enumerate(PECAS)}
    pares = liga(NETS)

    # ---- offsets reais de cada pino, lidos dos SVG de breadboard ----------
    with zipfile.ZipFile(SKETCH_COM_PICO) as z:
        nomes = z.namelist()
        fzp_pico = next(n for n in nomes if n.endswith("rpi_pico-tht_1.fzp"))
        svg_pico = next(n for n in nomes if "breadboard" in n and "rpi_pico" in n)
        pico_fzp_txt = z.read(fzp_pico).decode("utf-8", "replace")
        pico_svg_txt = z.read(svg_pico).decode("utf-8", "replace")

    off, problemas = {}, []
    for pid, mid, _arq, titulo, _props, _nomes in PECAS:
        kw = {}
        if pid == "PICO":
            kw = dict(svg_embutido=pico_svg_txt, fzp_embutido=pico_fzp_txt)
        o, err = geo.offsets(mid, **kw)
        if o is None:
            problemas.append(f"{titulo}: {err}")
            o = {}
        off[pid] = o
    if problemas:
        print("AVISO — posições não resolvidas (fios podem sair soltos):", file=sys.stderr)
        for p_ in problemas:
            print(f"  {p_}", file=sys.stderr)

    def abs_pino(pid, cid):
        bx, by = POS_BB.get(pid, (100, 100))
        dx, dy = off.get(pid, {}).get(cid, (0.0, 0.0))
        return bx + dx, by + dy

    # ---- conexões: breadboard passa por fio; esquemático/pcb são diretas --
    fios = []            # (modelIndex, cor, x, y, dx, dy, (pa,ca), (pb,cb))
    conn_bb = {}         # (peça, connectorId) -> [(modelIndex_fio, connector_do_fio)]
    conn_dir = {}        # (peça, connectorId) -> [(peça_alvo, connector_alvo)]
    mi_fio = 500
    for (pa, na), (pb, nb), net in pares:
        ca, cb = pino(pa, na), pino(pb, nb)
        conn_dir.setdefault((pa, ca), []).append((pb, cb))
        conn_dir.setdefault((pb, cb), []).append((pa, ca))
        ax, ay = abs_pino(pa, ca)
        bx, by = abs_pino(pb, cb)
        fios.append((mi_fio, CORES.get(net, COR_PADRAO), ax, ay, bx - ax, by - ay,
                     (pa, ca), (pb, cb), net))
        conn_bb.setdefault((pa, ca), []).append((mi_fio, "connector0"))
        conn_bb.setdefault((pb, cb), []).append((mi_fio, "connector1"))
        mi_fio += 1

    L = ['<?xml version="1.0" encoding="UTF-8"?>',
         f'<module fritzingVersion="{FRITZING_VERSION}">',
         '    <views>']
    for v, _lay in VISTAS:
        L.append(f'        <view name="{v}" backgroundColor="#ffffff" '
                 f'gridSize="0.1in" showGrid="1" alignToGrid="1" viewFromBelow="0"/>')
    L += ['    </views>', '    <instances>']

    # ---------------- instâncias de peça ----------------
    for pid, mid, arq, titulo, props, _nomes in PECAS:
        L.append(f'        <instance moduleIdRef="{escape(mid)}" '
                 f'modelIndex="{idx[pid]}" path="{escape(arq)}">')
        for k, val in props.items():
            L.append(f'            <property name="{escape(k)}" value="{escape(val)}"/>')
        L.append(f'            <title>{escape(titulo)}</title>')
        L.append('            <views>')
        for vista, camada in VISTAS:
            if vista == "breadboardView":
                x, y = POS_BB.get(pid, (100, 100))
            else:
                x, y = POS.get(pid, (100, 100))
            L.append(f'                <{vista} layer="{camada}">')
            L.append(f'                    <geometry z="2.5" x="{x:.4f}" y="{y:.4f}"/>')
            meus = sorted({c for (p_, c) in conn_dir if p_ == pid},
                          key=lambda c: int(c[9:]))
            if meus:
                L.append('                    <connectors>')
                for c in meus:
                    L.append(f'                        <connector connectorId="{c}" '
                             f'layer="{camada}">')
                    L.append('                            <connects>')
                    if vista == "breadboardView":
                        for (mfio, cfio) in conn_bb.get((pid, c), []):
                            L.append(f'                                <connect '
                                     f'connectorId="{cfio}" modelIndex="{mfio}" '
                                     f'layer="breadboardWire"/>')
                    else:
                        for (alvo, calvo) in conn_dir[(pid, c)]:
                            L.append(f'                                <connect '
                                     f'connectorId="{calvo}" modelIndex="{idx[alvo]}" '
                                     f'layer="{camada}"/>')
                    L.append('                            </connects>')
                    L.append('                        </connector>')
                L.append('                    </connectors>')
            L.append(f'                </{vista}>')
        L.append('            </views>')
        L.append('        </instance>')

    # ---------------- instâncias de fio (só protoboard) ----------------
    for n, (mfio, cor, x, y, dx, dy, (pa, ca), (pb, cb), net) in enumerate(fios):
        L.append(f'        <instance moduleIdRef="WireModuleID" '
                 f'modelIndex="{mfio}" path="wire.fzp">')
        L.append(f'            <title>{escape(net)}</title>')
        L.append('            <views>')
        L.append('                <breadboardView layer="breadboardWire">')
        L.append(f'                    <geometry z="{3.5 + n * 0.001:.4f}" '
                 f'x="{x:.4f}" y="{y:.4f}" x1="0" y1="0" '
                 f'x2="{dx:.4f}" y2="{dy:.4f}" wireFlags="64"/>')
        L.append(f'                    <wireExtras mils="22.2222" color="{cor}" '
                 f'opacity="1" banded="0"/>')
        L.append('                    <connectors>')
        for cfio, (pt, ct) in (("connector0", (pa, ca)), ("connector1", (pb, cb))):
            L.append(f'                        <connector connectorId="{cfio}" '
                     f'layer="breadboardWire">')
            L.append('                            <geometry x="0" y="0"/>')
            L.append('                            <connects>')
            L.append(f'                                <connect connectorId="{ct}" '
                     f'modelIndex="{idx[pt]}" layer="breadboard"/>')
            L.append('                            </connects>')
            L.append('                        </connector>')
        L.append('                    </connectors>')
        L.append('                </breadboardView>')
        L.append('            </views>')
        L.append('        </instance>')

    L += ['    </instances>', '</module>', '']
    fz = "\n".join(L)

    tmp = AQUI / "_fzz_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir()
    (tmp / "coruja_gps.fz").write_text(fz, encoding="utf-8")
    with zipfile.ZipFile(SKETCH_COM_PICO) as z:
        for nome in z.namelist():
            if "rpi_pico" in nome:
                (tmp / os.path.basename(nome)).write_bytes(z.read(nome))
    with zipfile.ZipFile(SAIDA, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(tmp.iterdir()):
            z.write(f, f.name)
    shutil.rmtree(tmp)

    import collections
    porcor = collections.Counter(c for _m, c, *_r in fios)
    print(f"{SAIDA.name}: {len(PECAS)} peças, {len(NETS)} redes, {len(fios)} fios")
    nomes_cor = {"#000000": "preto (GND)",
                 "#ffe500": "amarelo (12V)",
                 "#ff1a1a": "vermelho (5V)",
                 "#418dd9": "azul (3V3)",
                 "#4faf4e": "verde (sinais)"}
    for cor, n in porcor.most_common():
        print(f"  {n:>3} fios  {nomes_cor.get(cor, cor)}")
    print(f"  {SAIDA.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
