"""Gera o projeto do KiCad: `coruja.kicad_sch`, `.kicad_pro` e a tabela de libs.

A fonte continua sendo o `netlist.py`, traduzido pelo `kicad_mapa.py`. Aqui só
se desenha.

## Ligação por RÓTULO, não por fio

Cada pino ganha um toco de fio de 2,54 mm e um rótulo com o nome da rede. Para
o KiCad, dois rótulos iguais são a mesma rede — é tão ligado quanto um fio.

A alternativa seria rotear 83 nós automaticamente, e o resultado é um emaranhado
que ninguém revisa. Como o `bom_schematic.md` é o documento onde a fiação é
conferida por gente, o esquemático precisa ser **legível**; e quem precisa
saber onde `BZ_COL` passa procura o nome, não segue a linha.

Efeito colateral bom: a posição dos símbolos deixa de ter significado elétrico,
então arrumar o desenho nunca muda a placa.

## UUID determinístico

O KiCad identifica tudo por UUID. Gerados ao acaso, cada execução produziria um
arquivo inteiro diferente e o `git diff` seria inútil. Aqui eles saem de
`uuid5` sobre o nome do objeto: mesma entrada, mesmo arquivo.
"""
from __future__ import annotations

import pathlib
import uuid

from kicad_mapa import MAPA, no_kicad, pinos, titulo_da_peca
import kicad_sym
from kicad_sym import FONTE, PASSO, VERSAO, representantes, simbolo
from netlist import NETS
from expressao_s import Txt, despeja

#: Espaço de nomes próprio: UUID estável entre execuções e entre máquinas.
RAIZ_UUID = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/paulovitorbal/coruja_gps")

#: Folha A3 paisagem (420 × 297 mm) — o tamanho que se imprime sem reduzir.
#: Com `ALTURA_UTIL` em 250 as 22 peças empacotam em 3 colunas e cabem; em A2
#: cabiam em 2 colunas altas, ocupando 171 × 395 mm e desperdiçando a folha.
PAPEL = "A3"
MARGEM = 15.0
ALTURA_UTIL = 250.0

#: Largura reservada à coluna de rótulos, à esquerda de cada símbolo. O nome
#: de rede mais comprido do projeto cabe aqui.
COLUNA_ROTULO = 34.0
#: Distância entre colunas de símbolos.
PASSO_COLUNA = 112.0
#: Respiro vertical entre dois símbolos da mesma coluna.
FOLGA = 2 * PASSO


def _uid(chave: str) -> str:
    return str(uuid.uuid5(RAIZ_UUID, chave))


def _n(x: float) -> str:
    return f"{x:g}"


def _grade(x: float) -> float:
    """Arredonda para a grade de 2,54 mm.

    Fora da grade, o fio encosta no pino **no desenho** e não liga de fato —
    é a falha clássica de esquemático gerado por programa, e não aparece até
    alguém exportar a netlist e dar falta de um nó.
    """
    return round(x / PASSO) * PASSO


def _efeitos(justifica: str | None = None, oculto: bool = False) -> list:
    e: list = ["effects", ["font", ["size", _n(FONTE), _n(FONTE)]]]
    if justifica:
        e.append(["justify", justifica])
    if oculto:
        e.append(["hide", "yes"])
    return e


def valor_da_peca(peca: str) -> str:
    """Título sem o designador repetido na frente: `R1 330R` vira `330R`."""
    titulo = titulo_da_peca(peca)
    prefixo = peca + " "
    return titulo[len(prefixo):] if titulo.startswith(prefixo) else titulo


#: Rede de cada nó, indexada uma vez.
#:
#: O nome da peça aqui é o mesmo da netlist. Já não foi: enquanto o Pico era
#: dividido em `PICO_A` e `PICO_B`, a busca não casava com o `PICO` da netlist
#: e os 40 pinos saíam sem rótulo — sem erro nenhum, com o ERC passando. Quem
#: acusou foi o `verifica_kicad.py`, em 2026-10-05. A peça única acabou com a
#: tradução e com o problema.
_INDICE: dict[tuple[str, str], str] = {
    no: rede for rede, nos in NETS.items() for no in nos
}


def rede_do_no(peca: str, nome_pino: str) -> str | None:
    """Nome da rede ligada a um pino, ou None se o pino não é usado."""
    return _INDICE.get((peca, nome_pino))


# ---------------------------------------------------------------- geometria --


def _tamanho(peca: str) -> tuple[float, float]:
    """Largura e altura da caixa do símbolo, na mesma conta do `kicad_sym`."""
    sim = simbolo(peca)
    corpo = next(f for f in sim if isinstance(f, list) and f[0] == "symbol")
    ret = next(f for f in corpo if isinstance(f, list) and f[0] == "rectangle")
    ini = next(f for f in ret if isinstance(f, list) and f[0] == "start")
    return abs(float(ini[1])) * 2, abs(float(ini[2])) * 2


def arranja() -> dict[str, tuple[float, float]]:
    """Posição de cada símbolo, em colunas, do mais alto para o mais baixo.

    Os dois headers do Pico vêm primeiro e juntos: são o que alguém procura.
    """
    ordem = sorted(MAPA, key=lambda p: (-_tamanho(p)[1], p))
    pos: dict[str, tuple[float, float]] = {}
    col, y = 0, MARGEM
    for peca in ordem:
        _, altura = _tamanho(peca)
        if y + altura > MARGEM + ALTURA_UTIL and pos:
            col += 1
            y = MARGEM
        x = MARGEM + COLUNA_ROTULO + col * PASSO_COLUNA
        pos[peca] = (_grade(x), _grade(y + altura / 2))
        y += altura + FOLGA
    return pos


# ------------------------------------------------------------------ desenho --


def _instancia(peca: str, x: float, y: float) -> list:
    ref, nome, footprint, _ = MAPA[peca]
    meia_l, meia_a = (v / 2 for v in _tamanho(peca))

    s: list = [
        "symbol",
        ["lib_id", Txt(f"coruja:{nome}")],
        ["at", _n(x), _n(y), "0"],
        ["unit", "1"],
        ["exclude_from_sim", "no"],
        ["in_bom", "yes"],
        ["on_board", "yes"],
        ["dnp", "no"],
        ["uuid", Txt(_uid(f"sym:{peca}"))],
        ["property", Txt("Reference"), Txt(ref),
         ["at", _n(x), _n(y - meia_a - PASSO), "0"], _efeitos()],
        ["property", Txt("Value"), Txt(valor_da_peca(peca)),
         ["at", _n(x), _n(y - meia_a - PASSO + FONTE * 1.5), "0"], _efeitos()],
        ["property", Txt("Footprint"), Txt(footprint),
         ["at", _n(x), _n(y), "0"], _efeitos(oculto=True)],
        ["property", Txt("Datasheet"), Txt(""),
         ["at", _n(x), _n(y), "0"], _efeitos(oculto=True)],
        ["property", Txt("Description"), Txt(titulo_da_peca(peca)),
         ["at", _n(x), _n(y), "0"], _efeitos(oculto=True)],
    ]
    for pad, _nome_pino in pinos(peca):
        s.append(["pin", Txt(str(pad)), ["uuid", Txt(_uid(f"pin:{peca}:{pad}"))]])
    s.append(["instances",
              ["project", Txt("coruja"),
               ["path", Txt(f"/{_uid('folha')}"),
                ["reference", Txt(ref)], ["unit", "1"]]]])
    return s


def _ligacoes(peca: str, x: float, y: float) -> list[list]:
    """Toco de fio e rótulo para cada pino que a netlist usa."""
    meia_l, meia_a = (v / 2 for v in _tamanho(peca))
    saida: list[list] = []
    topo = meia_a - PASSO
    for i, (pad, nome_pino) in enumerate(pinos(peca)):
        rede = rede_do_no(peca, nome_pino)
        # Coordenada do pino na folha. O eixo Y do símbolo aponta para CIMA e
        # o da folha para BAIXO — daí o sinal trocado. Errar isso espelha o
        # símbolo inteiro e só se percebe olhando o desenho.
        px = x - meia_l - PASSO
        py = y - (topo - i * PASSO)
        fim = px - PASSO

        if rede is None:
            # Pino que a netlist não usa. Marcar com `no_connect` é o que
            # distingue "decidimos não ligar" de "esqueceram de ligar": sem a
            # marca o ERC acusa, e 21 avisos legítimos escondem o ilegítimo
            # que aparecer depois.
            saida.append(["no_connect",
                          ["at", _n(px), _n(py)],
                          ["uuid", Txt(_uid(f"nc:{peca}:{pad}"))]])
            continue
        saida.append(["wire",
                      ["pts", ["xy", _n(px), _n(py)], ["xy", _n(fim), _n(py)]],
                      ["stroke", ["width", "0"], ["type", "default"]],
                      ["uuid", Txt(_uid(f"fio:{peca}:{pad}"))]])
        saida.append(["label", Txt(rede),
                      ["at", _n(fim), _n(py), "0"],
                      _efeitos(justifica="right"),
                      ["uuid", Txt(_uid(f"rot:{peca}:{pad}"))]])
    return saida


def esquematico() -> list:
    pos = arranja()
    usados = representantes()

    lib: list = ["lib_symbols"]
    for nome in sorted(usados):
        s = simbolo(usados[nome])
        s[1] = Txt(f"coruja:{nome}")
        lib.append(s)

    sch: list = [
        "kicad_sch",
        ["version", VERSAO],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
        ["uuid", Txt(_uid("folha"))],
        ["paper", Txt(PAPEL)],
        ["title_block",
         ["title", Txt("coruja_gps — placa portadora")],
         ["company", Txt("projeto de estudo")],
         ["comment", "1", Txt("Gerado por gera_kicad.py a partir de netlist.py")],
         ["comment", "2", Txt("Ligacao por rotulo de rede; posicao nao tem efeito eletrico")]],
        lib,
    ]
    for peca in sorted(pos):
        x, y = pos[peca]
        sch.extend(_ligacoes(peca, x, y))
    for peca in sorted(pos):
        x, y = pos[peca]
        sch.append(_instancia(peca, x, y))

    sch.append(["sheet_instances", ["path", Txt("/"), ["page", Txt("1")]]])
    sch.append(["embedded_fonts", "no"])
    return sch


PROJETO = """{
  "board": {"design_settings": {}},
  "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
  "meta": {"filename": "coruja.kicad_pro", "version": 3},
  "net_settings": {"classes": [{"name": "Default", "track_width": 0.25}]},
  "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
  "sheets": [],
  "text_variables": {}
}
"""

#: Tabela de footprints do projeto.
#:
#: Aponta para a biblioteca instalada do KiCad pela variável dele
#: (`KICAD10_FOOTPRINT_DIR`), e não por caminho absoluto — senão o projeto só
#: abre nesta máquina, e o `/Applications/...` do macOS não existe no Linux.
#:
#: Sem esta tabela o ERC acusa `footprint_link_issues` em toda peça, porque o
#: kicad-cli roda sem a configuração global do usuário.
def _tabela_fp() -> str:
    libs = sorted({fp.partition(":")[0] for _, _, fp, _ in MAPA.values()})

    def _uri(b: str) -> str:
        # A biblioteca própria mora NO PROJETO; as outras, na instalação do
        # KiCad. Apontar `coruja` para KICAD10_FOOTPRINT_DIR acharia um
        # diretório inexistente e o footprint sumiria sem erro no esquemático.
        raiz = "${KIPRJMOD}" if b == "coruja" else "${KICAD10_FOOTPRINT_DIR}"
        return f"{raiz}/{b}.pretty"

    linhas = "\n".join(
        f'\t(lib (name "{b}")(type "KiCad")'
        f'(uri "{_uri(b)}")(options "")(descr ""))'
        for b in libs)
    return "(fp_lib_table\n\t(version 7)\n" + linhas + "\n)\n"


TABELA_SIM = """(sym_lib_table
	(version 7)
	(lib (name "coruja")(type "KiCad")(uri "${KIPRJMOD}/coruja.kicad_sym")(options "")(descr "Simbolos do projeto"))
)
"""


def main() -> None:
    destino = pathlib.Path(__file__).parent / "kicad"
    destino.mkdir(exist_ok=True)

    # A biblioteca sai JUNTO, de propósito. O esquemático embute uma cópia de
    # cada símbolo, e o KiCad compara as duas: se forem geradas por comandos
    # separados, basta alguém rodar um e não o outro para elas divergirem.
    #
    # Aconteceu em 2026-10-05, ao renomear o módulo GPS: o título da peça mudou,
    # o esquemático foi regerado e a biblioteca não. Quem acusou foi o ERC, com
    # `lib_symbol_mismatch` — nada mais teria acusado.
    kicad_sym.main()

    (destino / "coruja.kicad_sch").write_text(despeja(esquematico()) + "\n")
    # ⚠️ O .kicad_pro só é criado se NÃO existir. O KiCad o reescreve ao abrir
    # o projeto, acrescentando classes de rede, regras de projeto e
    # preferências — 9 KB contra os 350 bytes do nosso mínimo. Sobrescrever
    # apagaria tudo isso sem aviso.
    projeto = destino / "coruja.kicad_pro"
    if not projeto.exists():
        projeto.write_text(PROJETO)
    (destino / "sym-lib-table").write_text(TABELA_SIM)
    (destino / "fp-lib-table").write_text(_tabela_fp())

    nos = sum(len(v) for v in NETS.values())
    print(f"{destino}/coruja.kicad_sch: {len(MAPA)} simbolos, "
          f"{len(NETS)} redes, {nos} nos")


if __name__ == "__main__":
    main()
