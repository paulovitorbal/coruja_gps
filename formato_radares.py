#!/usr/bin/env python3
"""
formato_radares.py — contrato do formato radares.bin

Módulo compartilhado entre o conversor de referência (converte.py) e qualquer
parser novo. Escrever um parser para outro formato de entrada é:

    from formato_radares import Ponto, TipoPonto, Sentido, escreve

    pontos = []
    for registro in minha_fonte():
        pontos.append(Ponto(
            lat=-19.799916, lon=-44.021044,   # graus decimais
            limite=30,                         # km/h; 0 = sem limite aferível
            rumo=257,                          # graus, 0-359
            tipo=TipoPonto.RADAR_FIXO,
            sentido=Sentido.UNIDIRECIONAL,
        ))
    escreve(pontos, "radares.bin")

O `escreve()` cuida de quantização, empacotamento, ordenação por latitude e
CRC-32 — as quatro coisas que um parser novo erraria em silêncio se fizesse à
mão. Em especial a ORDENAÇÃO: a busca binária do firmware depende dela e
retorna resultado errado sem avisar se a invariante for violada.

Layout completo em formato_dados.md §2 e §3.
"""
import struct
import zlib
from enum import IntEnum
from typing import NamedTuple, Iterable, List

# ------------------------------------------------------------------ cabeçalho
MAGIC = b"RDR1"
VERSAO = 1
EXP_ESCALA = 5                 # coordenadas em graus x 10^5
ESCALA = 10 ** EXP_ESCALA       # -> resolução de ~1,1 m
TAM_REGISTRO = 12
TAM_CABECALHO = 16

# Teto de segurança: acima disso a carga integral estoura a SRAM do RP2350.
# Ver formato_dados.md §1 para a conta.
TETO_PONTOS = 40_000

# Sentinela de "sem limite de velocidade aferível" no campo `limite`.
SEM_LIMITE = 0


class TipoPonto(IntEnum):
    """
    Tipo de equipamento. Os valores são os do padrão iGO8 e vão nos bits 2-4
    do byte de flags, então precisam caber em 3 bits (0-7).

    ATENÇÃO a quem for adaptar: NÃO deduza o tipo pelo perfil de velocidade.
    Foi tentado e deu errado — ver a advertência em formato_dados.md §0.2.
    """

    RADAR_FIXO = 1           # radar fixo de velocidade
    SEMAFORO_COM_RADAR = 2   # semáforo que afere avanço de sinal E velocidade
    SEMAFORO_CAMERA = 3      # câmera de avanço de sinal; NÃO afere velocidade
    RADAR_MOVEL = 5          # ponto de operação de fiscalização móvel

    @property
    def afere_velocidade(self) -> bool:
        """
        Se False, o ponto não tem limite e nunca deve entrar em Zona de Perigo.
        Ver requirements.md RF03.3: o gatilho é `limite == 0`, não o tipo — há
        registros de SEMAFORO_CAMERA com limite por inconsistência da base.
        """
        return self is not TipoPonto.SEMAFORO_CAMERA

    @property
    def e_semaforo(self) -> bool:
        """Fiscaliza avanço de sinal. Usado para o ícone na tela (R-26)."""
        return self in (TipoPonto.SEMAFORO_COM_RADAR, TipoPonto.SEMAFORO_CAMERA)


class Sentido(IntEnum):
    """
    Como o rumo do ponto deve ser comparado com o rumo do veículo.
    Vai nos bits 0-1 do byte de flags.
    """

    OMNIDIRECIONAL = 0   # ignora o rumo; nunca descartar por sentido
    UNIDIRECIONAL = 1    # vale só o rumo indicado (pista oposta = outro registro)
    BIDIRECIONAL = 2     # vale o rumo indicado E o oposto


class Ponto(NamedTuple):
    """Um ponto de interesse, em unidades humanas. `escreve()` quantiza."""

    lat: float                  # graus decimais
    lon: float                  # graus decimais
    limite: int                 # km/h; 0 = SEM_LIMITE
    rumo: int                   # graus, 0-359
    tipo: TipoPonto
    sentido: Sentido


def empacota(p: Ponto) -> bytes:
    """Um registro de 12 bytes. Ver formato_dados.md §3.1."""
    if not (-90 <= p.lat <= 90) or not (-180 <= p.lon <= 180):
        raise ValueError(f"coordenada fora do globo: {p.lat},{p.lon}")
    if not (0 <= p.limite <= 255):
        raise ValueError(f"limite fora de 0-255: {p.limite}")
    if not (0 <= p.rumo <= 359):
        raise ValueError(f"rumo fora de 0-359: {p.rumo}")

    # Rumo em passos de 2 graus: o filtro de sentido do firmware é de +-30
    # graus, então a quantização não altera nenhuma decisão e poupa 1 byte.
    rumo_q = (int(p.rumo) % 360) // 2
    flags = (int(p.sentido) & 0x03) | ((int(p.tipo) & 0x07) << 2)
    return struct.pack(
        "<iiBBBB",
        int(round(p.lat * ESCALA)),
        int(round(p.lon * ESCALA)),
        int(p.limite),
        rumo_q,
        flags,
        0,                       # padding, mantém o registro alinhado em 4
    )


def escreve(pontos: Iterable[Ponto], caminho) -> int:
    """
    Grava radares.bin. Retorna a quantidade de registros.

    Ordena por latitude — invariante obrigatória do formato — calcula o CRC-32
    e monta o cabeçalho. Um parser novo não precisa saber nada disso.
    """
    lista = list(pontos)
    if not lista:
        raise ValueError("nenhum ponto a gravar")
    if len(lista) > TETO_PONTOS:
        raise ValueError(
            f"{len(lista)} pontos excede o teto de {TETO_PONTOS}: a carga "
            "integral não caberia na SRAM do RP2350 (ver formato_dados.md §1)"
        )

    registros = sorted((empacota(p) for p in lista),
                       key=lambda r: struct.unpack_from("<i", r)[0])
    dados = b"".join(registros)
    cab = struct.pack("<4sHBBII", MAGIC, VERSAO, EXP_ESCALA, TAM_REGISTRO,
                      len(registros), zlib.crc32(dados))
    with open(caminho, "wb") as f:
        f.write(cab + dados)
    return len(registros)


class RegistroLido(NamedTuple):
    lat: float
    lon: float
    limite: int
    rumo: int
    tipo: TipoPonto
    sentido: Sentido


def le(caminho) -> List[RegistroLido]:
    """
    Lê e VALIDA um radares.bin, nas mesmas verificações que o firmware faz
    (formato_dados.md §2). Útil para testar um parser novo.
    """
    bruto = open(caminho, "rb").read()
    if len(bruto) < TAM_CABECALHO:
        raise ValueError("arquivo menor que o cabeçalho")
    magic, versao, exp, tam, n, crc = struct.unpack_from("<4sHBBII", bruto)
    if magic != MAGIC:
        raise ValueError(f"magic inesperado: {magic!r}")
    if versao != VERSAO or exp != EXP_ESCALA or tam != TAM_REGISTRO:
        raise ValueError(f"versão/escala/tamanho inesperados: {versao},{exp},{tam}")
    if not (0 < n <= TETO_PONTOS):
        raise ValueError(f"n_pontos implausível: {n}")
    if TAM_CABECALHO + n * TAM_REGISTRO != len(bruto):
        raise ValueError("tamanho do arquivo não bate com n_pontos")
    dados = bruto[TAM_CABECALHO:]
    if zlib.crc32(dados) != crc:
        raise ValueError("CRC-32 não confere")

    saida, anterior = [], None
    for i in range(n):
        la, lo, li, ru, fl, _pad = struct.unpack_from("<iiBBBB", dados, i * TAM_REGISTRO)
        if anterior is not None and la < anterior:
            raise ValueError(f"registro {i} fora de ordem: a busca binária "
                             "do firmware erraria em silêncio")
        anterior = la
        saida.append(RegistroLido(
            lat=la / ESCALA, lon=lo / ESCALA, limite=li, rumo=ru * 2,
            tipo=TipoPonto((fl >> 2) & 0x07), sentido=Sentido(fl & 0x03),
        ))
    return saida
