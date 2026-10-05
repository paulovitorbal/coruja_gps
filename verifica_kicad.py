"""Confere que o projeto do KiCad diz a MESMA coisa que o `netlist.py`.

Esta é a verificação que vale. As outras — símbolo carrega, esquemático abre —
provam que o arquivo é bem formado, não que está certo. Aqui o próprio KiCad
exporta a netlist dele, e ela é comparada nó a nó com a fonte.

A classe de erro que isso pega é a pior do projeto: **ligação trocada que não
gera erro nenhum**. O esquemático fecha, o KiCad não reclama, a placa é
fabricada e o defeito aparece no laboratório. O deslocamento de 20 pinos entre
os dois headers do Pico (ver `kicad_mapa.py`) é exatamente desse tipo.

    python3 verifica_kicad.py

Sai 0 quando os dois conjuntos de nós são idênticos.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

from kicad_mapa import MAPA, no_kicad
from netlist import NETS
from expressao_s import carrega, filhos, valor

KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
FOOTPRINTS = pathlib.Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")


def nos_da_fonte() -> dict[str, set[tuple[str, int]]]:
    """Redes do `netlist.py`, já traduzidas para (referência, pad)."""
    saida: dict[str, set[tuple[str, int]]] = {}
    for rede, nos in NETS.items():
        saida[rede] = {no_kicad(peca, nome) for peca, nome in nos}
    return saida


def nos_do_kicad(netlist: pathlib.Path) -> dict[str, set[tuple[str, int]]]:
    """Redes como o KiCad as exportou."""
    no = carrega(netlist.read_text())
    redes = filhos(no, "nets")
    if not redes:
        raise SystemExit("netlist exportada sem bloco `nets`")
    saida: dict[str, set[tuple[str, int]]] = {}
    for net in filhos(redes[0], "net"):
        nome = valor(net, "name")
        if nome is None:
            continue
        # O KiCad prefixa com a folha (`/GND`) e inventa `unconnected-(...)`
        # para pino sem rede. Um pino que DEVERIA estar ligado e não está some
        # da rede dele e é acusado na comparação nó a nó — então descartar os
        # `unconnected` aqui não esconde nada.
        nome = str(nome)
        if nome.startswith("unconnected-"):
            continue
        nome = nome.lstrip("/")
        pinos = set()
        for n in filhos(net, "node"):
            ref, pino = valor(n, "ref"), valor(n, "pin")
            if ref is not None and pino is not None:
                pinos.add((str(ref), int(pino)))
        saida[nome] = pinos
    return saida


def exporta(projeto: pathlib.Path, destino: pathlib.Path) -> None:
    r = subprocess.run(
        [KICAD_CLI, "sch", "export", "netlist", "-o", str(destino), str(projeto)],
        capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"kicad-cli falhou:\n{r.stdout}\n{r.stderr}")


def confere_footprints() -> list[str]:
    """Todo footprint referenciado existe na biblioteca instalada."""
    faltam = []
    for peca, (ref, _, fp, _) in sorted(MAPA.items()):
        lib, _, nome = fp.partition(":")
        if not (FOOTPRINTS / f"{lib}.pretty" / f"{nome}.kicad_mod").exists():
            faltam.append(f"{ref} ({peca}): footprint inexistente — {fp}")
    return faltam


def compara(fonte: dict, kicad: dict) -> list[str]:
    problemas = []

    # O KiCad nomeia redes sem rótulo com `Net-(...)`; as nossas têm todas
    # rótulo, então os nomes devem bater exatamente.
    so_fonte = set(fonte) - set(kicad)
    so_kicad = set(kicad) - set(fonte)
    for r in sorted(so_fonte):
        problemas.append(f"rede ausente no esquematico: {r}")
    for r in sorted(so_kicad):
        problemas.append(f"rede a mais no esquematico: {r} -> {sorted(kicad[r])}")

    for rede in sorted(set(fonte) & set(kicad)):
        a, b = fonte[rede], kicad[rede]
        for no in sorted(a - b):
            problemas.append(f"{rede}: nó da fonte ausente no esquematico: {no}")
        for no in sorted(b - a):
            problemas.append(f"{rede}: nó a mais no esquematico: {no}")
    return problemas


def main() -> int:
    raiz = pathlib.Path(__file__).parent / "kicad"
    projeto = raiz / "coruja.kicad_sch"
    if not projeto.exists():
        raise SystemExit(f"nao existe: {projeto}. Rode gera_kicad.py antes.")

    problemas = confere_footprints()

    with tempfile.TemporaryDirectory() as tmp:
        saida = pathlib.Path(tmp) / "coruja.net"
        exporta(projeto, saida)
        fonte, kicad = nos_da_fonte(), nos_do_kicad(saida)

    problemas += compara(fonte, kicad)

    nos_fonte = sum(len(v) for v in fonte.values())
    nos_kicad = sum(len(v) for v in kicad.values())
    print(f"fonte: {len(fonte)} redes / {nos_fonte} nos")
    print(f"kicad: {len(kicad)} redes / {nos_kicad} nos")

    if problemas:
        print(f"\n{len(problemas)} PROBLEMAS:")
        for p in problemas:
            print("  ", p)
        return 1
    print("\nOK — o esquematico liga exatamente o que o netlist.py manda.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
