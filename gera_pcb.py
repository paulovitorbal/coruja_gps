"""Gera `coruja.kicad_pcb`: contorno, posicionamento e redes.

    python3 gera_pcb.py

**Posiciona, não roteia.** Posicionamento é decisão de projeto e sai da planta
baixa, que foi conferida no papel contra as peças. Roteamento é trabalho de
ferramenta e fica para o editor — ou para depois.

## De onde vem cada coisa

| | |
| :--- | :--- |
| Contorno e furos de fixação | `planta.py` |
| Posição de cada módulo | `planta.py`, já conferida contra sobreposição |
| Footprint de cada peça | `kicad_mapa.py` — os mesmos do esquemático |
| Rede de cada ilha | `netlist.py`, via `no_kicad()` |

A rede sai da **mesma fonte** do esquemático. Não se exporta netlist do
esquemático para a placa: as duas leem o `netlist.py`, e é por isso que não
podem divergir.

## O esqueleto do formato

Os blocos `general`, `paper`, `layers` e `setup` são copiados de uma placa que
o próprio KiCad instala. Escrever isso à mão é adivinhar dezenas de campos cujo
erro aparece como "não foi possível carregar", sem dizer onde — foi o que
aconteceu duas vezes com a biblioteca de símbolos.
"""
from __future__ import annotations

import math
import pathlib
import uuid

import arranjo
import planta
from expressao_s import Txt, carrega, despeja, filhos
from gera_kicad import RAIZ_UUID, _n, valor_da_peca
from kicad_mapa import MAPA, no_kicad, pinos, titulo_da_peca
from ocupacao import caixa_na_placa
from netlist import NETS

#: Placa de referência instalada pelo KiCad, de onde sai o esqueleto.
MODELO = pathlib.Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/template/"
    "Arduino_Pro_Mini/Arduino_Pro_Mini.kicad_pcb")

FOOTPRINTS = pathlib.Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")
PROPRIA = pathlib.Path(__file__).parent / "kicad"

#: Furo de fixação da placa: footprint da biblioteca, M3 de passagem.
FP_FURO = "MountingHole:MountingHole_3.2mm_M3_DIN965_Pad"

#: Espessura do traço do contorno, e largura da borda sem cobre.
TRACO_BORDA = 0.1


def _uid(chave: str) -> str:
    return str(uuid.uuid5(RAIZ_UUID, "pcb:" + chave))


def _raiz_da_lib(lib: str) -> pathlib.Path:
    return PROPRIA if lib == "coruja" else FOOTPRINTS


def carrega_footprint(caminho_lib: str) -> list:
    lib, _, nome = caminho_lib.partition(":")
    arq = _raiz_da_lib(lib) / f"{lib}.pretty" / f"{nome}.kicad_mod"
    if not arq.exists():
        raise SystemExit(f"footprint inexistente: {caminho_lib} ({arq})")
    return carrega(arq.read_text())


# ------------------------------------------------------------------ redes --


def tabela_de_redes() -> tuple[list[list], dict[tuple[str, int], tuple[int, str]]]:
    """Lista de redes do arquivo, e o mapa (referência, pad) -> (número, nome).

    A rede 0 é a "sem rede" do KiCad e tem de existir, sempre primeiro.
    """
    nomes = sorted(NETS)
    redes = [["net", "0", Txt("")]]
    por_no: dict[tuple[str, int], tuple[int, str]] = {}
    for i, nome in enumerate(nomes, start=1):
        # ⚠️ O nome da rede na placa tem de ser IDÊNTICO ao que o esquemático
        # produz. Rótulo local na folha raiz gera `/NOME`, com a barra do
        # caminho da folha — e sem ela a verificação de paridade acusa cada
        # ilha, uma a uma. Foram 104 avisos em 2026-10-05, até o relatório da
        # interface mostrar `VSYS_5V` contra `/VSYS_5V`.
        redes.append(["net", str(i), Txt("/" + nome)])
        for peca, pino in NETS[nome]:
            por_no[no_kicad(peca, pino)] = (i, "/" + nome)

    # Pino sem ligação também tem nome de rede no esquemático — o KiCad
    # inventa `unconnected-(REF-NOME-PadN)` para cada um. Deixá-los sem rede na
    # placa faz a paridade acusar os 21, um a um, sem que haja erro nenhum:
    # são os `no_connect` que nós mesmos marcamos de propósito.
    proximo = len(redes)
    for peca, (ref, _simb, _fp, _n) in sorted(MAPA.items()):
        for pad, nome_pino in pinos(peca):
            if (ref, pad) in por_no:
                continue
            partes = [ref]
            # O trecho do nome é omitido quando ele é igual ao número do pad —
            # é por isso que os pinos do Pico, nomeados pelo número físico,
            # saem sem ele. A barra vira `{slash}`.
            if str(nome_pino) != str(pad):
                partes.append(str(nome_pino).replace("/", "{slash}"))
            partes.append(f"Pad{pad}")
            rotulo = "unconnected-(" + "-".join(partes) + ")"
            redes.append(["net", str(proximo), Txt(rotulo)])
            por_no[(ref, pad)] = (proximo, rotulo)
            proximo += 1

    return redes, por_no


# ------------------------------------------------------------- colocação --


def _gira(px: float, py: float, rot: float) -> tuple[float, float]:
    """Ponto local para deslocamento na placa, na convenção do KiCad.

    O ângulo é anti-horário na tela, e a tela tem Y para baixo — então em
    coordenada cresce no sentido horário. Conferido contra o que o DRC
    reportou: um ponto local (24,13; -8,89) num footprint a 90 graus apareceu
    deslocado de (-8,89; -24,13).
    """
    a = math.radians(rot)
    return (px * math.cos(a) + py * math.sin(a),
            -px * math.sin(a) + py * math.cos(a))


def _transforma_zona(zona: list, x: float, y: float, rot: float) -> list:
    saida = []
    for item in zona:
        if isinstance(item, list) and item[0] == "polygon":
            pts = [p for p in item if isinstance(p, list) and p[0] == "pts"][0]
            novos = ["pts"]
            for pt in pts[1:]:
                dx, dy = _gira(float(pt[1]), float(pt[2]), rot)
                novos.append(["xy", _n(x + dx), _n(y + dy)])
            saida.append(["polygon", novos])
        else:
            saida.append(item)
    return saida


def CAMPOS(peca: str) -> dict[str, str]:
    """Os quatro campos que a verificação de paridade compara."""
    ref, _simbolo, _caminho, _n = MAPA[peca]
    # ⚠️ `Footprint` NÃO entra. Ela é redundante — o `lib_id` da peça já diz
    # qual footprint é — e acrescentá-la cria uma propriedade que a biblioteca
    # não tem, o que faz o KiCad acusar `lib_footprint_mismatch`.
    #
    # A verificação de paridade nunca pediu esse campo: ela compara
    # `Description`. Acrescentei os dois de uma vez em 2026-10-05 e o segundo
    # sobrou — corrigir um aviso criando outro.
    return {"Reference": ref,
            "Value": valor_da_peca(peca),
            "Description": titulo_da_peca(peca)}


def coloca(peca: str, x: float, y: float, rot: float,
           por_no: dict) -> list:
    """Um footprint posicionado, com a rede em cada ilha."""
    ref, _simbolo, caminho, _n_pads = MAPA[peca]
    fp = carrega_footprint(caminho)
    nome = fp[1]

    vistos: set[str] = set()
    saida: list = ["footprint", Txt(f"{caminho.partition(':')[0]}:{nome}")]
    saida.append(["layer", Txt("F.Cu")])
    saida.append(["uuid", Txt(_uid(f"fp:{ref}"))])
    saida.append(["at", _n(x), _n(y)] + ([_n(rot)] if rot else []))

    for filho in fp[2:]:
        if not isinstance(filho, list):
            continue
        tag = filho[0]
        if tag in ("version", "generator", "generator_version", "layer"):
            continue
        # Os campos têm de bater com os do símbolo: a verificação de paridade
        # compara os dois, e divergência aqui é um aviso por peça e por campo.
        if tag == "property" and len(filho) > 2 and filho[1] in CAMPOS(peca):
            filho = list(filho)
            filho[2] = Txt(CAMPOS(peca)[filho[1]])
            vistos.add(str(filho[1]))
        if tag == "zone":
            # ⚠️ Zona DENTRO de um footprint é gravada em coordenada ABSOLUTA
            # da placa, ao contrário dos pads, que ficam locais. Copiá-la como
            # está faz a área de restrição aterrissar na origem da placa.
            #
            # O footprint do Pico tem duas: a antena de 2,4 GHz do Pico W (onde
            # não pode cobre nenhum) e o espaço do cabo USB. Sem esta
            # transformação, a do Pico caiu no canto da placa e engoliu uma
            # ilha do conversor. Quem acusou foi o DRC, em 2026-10-05.
            filho = _transforma_zona(filho, x, y, rot)
        if tag == "pad":
            filho = list(filho)
            numero = filho[1]
            chave = (ref, int(numero)) if str(numero).isdigit() else None
            rede = por_no.get(chave) if chave else None
            if rede is not None:
                filho.append(["net", str(rede[0]), Txt(rede[1])])
            filho.append(["uuid", Txt(_uid(f"pad:{ref}:{numero}"))])
        saida.append(filho)

    # ⚠️ Campo que o footprint não tem precisa ser ACRESCENTADO, não só
    # substituído. Os footprints da biblioteca do KiCad não trazem `Footprint`
    # nem `Description`, e sem elas a paridade acusa a peça inteira. Substituir
    # só o que existe resolveu `Value` e deixou os outros dois de fora —
    # 2026-10-05.
    for campo, valor in CAMPOS(peca).items():
        if campo in vistos:
            continue
        saida.append(["property", Txt(campo), Txt(valor),
                      ["at", "0", "0", "0"],
                      ["layer", Txt("F.Fab")],
                      ["hide", "yes"],
                      ["uuid", Txt(_uid(f"prop:{ref}:{campo}"))],
                      ["effects", ["font", ["size", "1", "1"],
                                   ["thickness", "0.15"]]]])
    return saida


def furo_de_fixacao(i: int, x: float, y: float) -> list:
    fp = carrega_footprint(FP_FURO)
    saida: list = ["footprint", Txt(FP_FURO)]
    saida.append(["layer", Txt("F.Cu")])
    saida.append(["uuid", Txt(_uid(f"furo:{i}"))])
    saida.append(["at", _n(x), _n(y)])
    for filho in fp[2:]:
        if isinstance(filho, list) and filho[0] in (
                "version", "generator", "generator_version", "layer"):
            continue
        if (isinstance(filho, list) and filho[0] == "property"
                and len(filho) > 2 and filho[1] == "Reference"):
            filho = list(filho)
            filho[2] = Txt(f"H{i}")
        saida.append(filho)
    return saida


# ------------------------------------------------------------- gráficos --


def contorno() -> list[list]:
    """Edge.Cuts: o retângulo da placa."""
    L, P = planta.PCB_L, planta.PCB_P
    cantos = [(0, 0), (L, 0), (L, P), (0, P)]
    saida = []
    for i, (x1, y1) in enumerate(cantos):
        x2, y2 = cantos[(i + 1) % 4]
        saida.append(["gr_line",
                      ["start", _n(x1), _n(y1)], ["end", _n(x2), _n(y2)],
                      ["stroke", ["width", _n(TRACO_BORDA)], ["type", "default"]],
                      ["layer", Txt("Edge.Cuts")],
                      ["uuid", Txt(_uid(f"borda:{i}"))]])
    return saida


def plano_de_terra(lado: str, indice: int) -> list:
    """Preenchimento de GND cobrindo a placa inteira.

    Terra em plano, e não em trilha, é o que se faz: dá caminho de retorno
    curto para cada sinal, baixa a impedância da alimentação e, aqui, serve de
    **plano de terra para a antena** — que é o que um patch cerâmico quer
    debaixo de si.

    A zona de exclusão da antena e a do Pico W recortam este plano sozinhas: o
    KiCad respeita `keepout` ao preencher.
    """
    L, P = planta.PCB_L, planta.PCB_P
    numero = next(i for i, r in enumerate(tabela_de_redes()[0])
                  if len(r) > 2 and str(r[2]) == "/GND")
    return [
        "zone",
        ["net", str(numero)], ["net_name", Txt("/GND")],
        ["layer", Txt(lado)],
        ["uuid", Txt(_uid(f"terra:{lado}"))],
        ["name", Txt(f"GND {lado}")],
        ["hatch", "edge", "0.5"],
        ["connect_pads", ["clearance", "0.5"]],
        ["min_thickness", "0.25"],
        ["fill", ["thermal_gap", "0.5"], ["thermal_bridge_width", "0.5"]],
        ["polygon", ["pts",
                     ["xy", "0", "0"], ["xy", _n(L), "0"],
                     ["xy", _n(L), _n(P)], ["xy", "0", _n(P)]]],
    ]


#: Nome da placa e origem, em serigrafia. Quem pega a placa anos depois
#: precisa saber o que é e onde está a fonte — sem isso, é um retângulo verde
#: com peças.
NOME_DA_PLACA = "Coruja GPS PCB v1"
URL_DO_PROJETO = "github.com/paulovitorbal/coruja_gps"


def _gr_texto(texto: str, x: float, y: float, tam: float,
              chave: str, camada: str = "F.SilkS", espessura: float = 0.2,
              centrado: bool = False) -> list:
    # ⚠️ `justify mirror` NÃO centraliza — espelha o texto, e texto espelhado
    # na face de cima é defeito de fabricação. Centralizado é o padrão: basta
    # não declarar justify. (2026-10-05)
    efeitos = ["effects", ["font", ["size", _n(tam), _n(tam)],
                           ["thickness", _n(espessura)]]]
    if not centrado:
        efeitos.append(["justify", "left"])
    return ["gr_text", Txt(texto),
            ["at", _n(x), _n(y), "0"],
            ["layer", Txt(camada)],
            ["uuid", Txt(_uid(f"txt:{chave}"))],
            efeitos]


def identificacao(x: float, y: float) -> list[list]:
    """Nome e URL em serigrafia, em duas linhas."""
    return [_gr_texto(NOME_DA_PLACA, x, y, 2.6, "nome", espessura=0.35),
            _gr_texto(URL_DO_PROJETO, x, y + 4.2, 1.8, "url")]


def marca_da_antena() -> list[list]:
    """Desenha a área da antena na SERIGRAFIA, não só como zona.

    A zona de exclusão é invisível para quem monta: ela existe para a
    verificação de projeto, não para o olho. Mas a antena é **colada com fita**
    em cima da placa, e quem cola precisa ver onde.

    O retângulo e o texto também protegem a regra depois da montagem: alguém
    que olhe a placa pronta vê que aquele quadrado é reservado, em vez de achar
    que sobrou espaço.
    """
    _, zx, zy, zw, zd, _ = next(z for z in planta.PCB_ZONAS if z[0] == "ANTENA")
    x0, y0 = planta.posicao_kicad(zx, zy + zd)
    x1, y1 = planta.posicao_kicad(zx + zw, zy)
    cantos = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    saida = []
    for i, (ax, ay) in enumerate(cantos):
        bx, by = cantos[(i + 1) % 4]
        saida.append(["gr_line",
                      ["start", _n(ax), _n(ay)], ["end", _n(bx), _n(by)],
                      ["stroke", ["width", "0.25"], ["type", "dash"]],
                      ["layer", Txt("F.SilkS")],
                      ["uuid", Txt(_uid(f"antena_silk:{i}"))]])
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    saida.append(_gr_texto("ANTENA GPS", cx, cy - 2.0, 2.2, "antena_nome",
                           espessura=0.3, centrado=True))
    saida.append(_gr_texto("colar aqui - nao usar", cx, cy + 1.6, 1.4,
                           "antena_nota", centrado=True))
    return saida


def area_da_antena() -> list:
    """Zona de exclusão sob a antena.

    O patch cerâmico quer plano de terra contínuo embaixo e nada por cima. Uma
    trilha cruzando ali não dá erro em lugar nenhum — o sintoma seria fix
    demorado ou instável, que ninguém atribui ao desenho da placa.

    Por isso a regra vira ZONA: a verificação de projeto passa a reclamar
    sozinha se alguém roteia por cima.
    """
    nome, zx, zy, zw, zd, _ = next(z for z in planta.PCB_ZONAS if z[0] == "ANTENA")
    x0, y0 = planta.posicao_kicad(zx, zy + zd)
    x1, y1 = planta.posicao_kicad(zx + zw, zy)
    return [
        "zone",
        ["net", "0"], ["net_name", Txt("")],
        ["layers", Txt("F.Cu"), Txt("B.Cu")],
        ["uuid", Txt(_uid("zona:antena"))],
        ["name", Txt("ANTENA — area proibida")],
        ["hatch", "edge", "0.5"],
        ["connect_pads", ["clearance", "0.5"]],
        ["min_thickness", "0.25"],
        ["keepout",
         ["tracks", "not_allowed"], ["vias", "not_allowed"],
         ["pads", "not_allowed"], ["copperpour", "allowed"],
         ["footprints", "not_allowed"]],
        ["polygon", ["pts",
                     ["xy", _n(x0), _n(y0)], ["xy", _n(x1), _n(y0)],
                     ["xy", _n(x1), _n(y1)], ["xy", _n(x0), _n(y1)]]],
    ]


# ------------------------------------------------------------------ placa --

#: Onde fica a origem do footprint de cada peça, em coordenada do KiCad, e a
#: rotação. Derivado da planta — ver o comentário de cada um.
def posicoes() -> dict[str, tuple[float, float, float]]:
    z = {n: (x, y, w, d) for n, x, y, w, d, _ in planta.PCB_ZONAS}

    def centro(nome):
        x, y, w, d = z[nome]
        return planta.posicao_kicad(x + w / 2, y + d / 2)

    # Conversor e GPS têm origem no CENTRO do contorno; microSD e Pico, no
    # pino 1. A diferença é de como cada footprint foi gerado, e está anotada
    # no `gera_footprint.py`.
    cx, cy = centro("conversor 12V")
    gx, gy = centro("GPS")

    px, py, pw, pd = z["Pico 2 W"]
    pico_x = px + (pw - 17.78) / 2
    pico_y = planta.posicao_kicad(0, py + pd)[1] + (pd - 48.26) / 2

    sx, sy, sw, sd = z["leitor microSD"]
    sd_x = sx + 2.54
    sd_y = planta.posicao_kicad(0, sy + sd)[1] + 20.32

    return {
        "CONV": (cx, cy, 90.0),
        "GPS": (gx, gy, 0.0),
        "PICO": (pico_x, pico_y, 0.0),
        "SD": (sd_x, sd_y, 0.0),
    }


#: Peças ainda sem posição decidida: passivos, conectores de painel e a
#: proteção de entrada.
#:
#: Ficam **fora do contorno**, numa coluna à direita — que é onde o KiCad
#: deixa peça recém-importada, e é a convenção que diz "ainda não colocada".
#:
#: Por que não distribuí-las por conta própria: várias são longas (o
#: porta-fusível tem 25 mm, o TVS 15) e a placa já está ocupada o bastante
#: para que a colocação delas seja decisão de projeto, não sobra de espaço.
#: Empilhá-las dentro da placa produziria centenas de violações falsas e
#: afogaria as verdadeiras.
FAIXA_X = 120.0
FAIXA_Y0 = 5.0
FAIXA_PASSO = 14.0


def placa() -> list:
    modelo = carrega(MODELO.read_text())
    redes, por_no = tabela_de_redes()
    pos = posicoes()

    pcb: list = [
        "kicad_pcb",
        ["version", "20241229"],
        ["generator", Txt("coruja_gps")],
        ["generator_version", Txt("10.0")],
    ]
    for tag in ("general", "paper", "layers", "setup"):
        bloco = filhos(modelo, tag)
        if bloco:
            pcb.append(bloco[0])
    pcb.extend(redes)

    # Área realmente ocupada pelos módulos da planta, mais a zona da antena.
    ocupadas = [caixa_na_placa(MAPA[p][2], x, y, r) for p, (x, y, r) in pos.items()]
    zx, zy, zw, zd = next((x, y, w, d) for n, x, y, w, d, _ in planta.PCB_ZONAS
                          if n == "ANTENA")
    a0 = planta.posicao_kicad(zx, zy + zd)
    a1 = planta.posicao_kicad(zx + zw, zy)
    ocupadas.append((a0[0], a0[1], a1[0], a1[1]))

    furos, probl_furo = arranjo.coloca_furos(
        FP_FURO, ocupadas, (planta.PCB_L, planta.PCB_P))
    ocupadas += [caixa_na_placa(FP_FURO, fx, fy) for fx, fy in furos]

    pequenas, sobraram = arranjo.coloca_pequenas(ocupadas)
    if probl_furo or sobraram:
        raise SystemExit("arranjo impossivel:\n  " +
                         "\n  ".join(probl_furo + [f"sem lugar: {p}" for p in sobraram]))

    for peca, (x, y, rot) in sorted({**pos, **pequenas}.items()):
        pcb.append(coloca(peca, x, y, rot, por_no))

    faltam = set(MAPA) - set(pos) - set(pequenas)
    for i, peca in enumerate(sorted(faltam)):
        pcb.append(coloca(peca, FAIXA_X, FAIXA_Y0 + i * FAIXA_PASSO, 0.0, por_no))

    for j, (fx, fy) in enumerate(furos, start=1):
        pcb.append(furo_de_fixacao(j, fx, fy))

    pcb.extend(contorno())
    pcb.extend(marca_da_antena())
    # Identificação numa faixa livre da borda inferior.
    pcb.extend(identificacao(22.0, planta.PCB_P - 8.0))
    pcb.append(area_da_antena())
    # Terra nas duas faces. O preenchimento de fato só acontece quando alguém
    # abre a placa e manda preencher — o arquivo guarda a zona, não o cobre.
    for i, lado in enumerate(("F.Cu", "B.Cu")):
        pcb.append(plano_de_terra(lado, i))
    return pcb


def main() -> None:
    problemas = planta.conferencia()
    if problemas:
        raise SystemExit("planta invalida:\n  " + "\n  ".join(problemas))
    destino = pathlib.Path(__file__).parent / "kicad" / "coruja.kicad_pcb"

    # ⚠️ NUNCA sobrescrever placa roteada.
    #
    # Tudo aqui é gerado e pode ser refeito a qualquer momento — menos as
    # trilhas. Elas são trabalho humano, feitas no editor, e não existem em
    # fonte nenhuma: se este arquivo for reescrito, horas de roteamento somem
    # sem aviso e sem como voltar.
    #
    # Em 2026-10-05 a placa foi roteada à mão, com 295 segmentos e 46 vias.
    # A partir daí, regerar deixou de ser operação inócua.
    if destino.exists():
        texto = destino.read_text()
        n_seg = texto.count("(segment")
        if n_seg:
            raise SystemExit(
                f"RECUSADO: {destino.name} tem {n_seg} segmentos de trilha.\n"
                "  Regerar apagaria o roteamento, que nao esta em fonte nenhuma.\n"
                "  Se a intencao e mesmo recomecar do zero, mova o arquivo antes:\n"
                f"    mv {destino} {destino}.roteada")

    destino.write_text(despeja(placa()) + "\n")
    nos = sum(len(v) for v in NETS.values())
    print(f"{destino}")
    print(f"  {planta.PCB_L:.0f} x {planta.PCB_P:.0f} mm, {len(MAPA)} pecas, "
          f"{len(NETS)} redes, {nos} nos")
    print(f"  {len(posicoes())} modulos pela planta, o resto colocado "
          f"automaticamente contra a area real de cada footprint")


if __name__ == "__main__":
    main()
