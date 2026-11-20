#!/usr/bin/env python3
"""Simula o NEO-M8N numa porta serial, percorrendo o Eixão.

Cria um terminal virtual (pty) e emite sentenças `$GNRMC` a 4 Hz, como o
módulo real configurado pelo RF01.4. O aparelho — ou qualquer programa — abre
o lado escravo como se fosse `/dev/cu.usbserial...`.

O veículo percorre a linha de centro do Eixão de ponta a ponta e volta; ida e
volta é **uma volta**. A rota vem do OpenStreetMap, não de coordenadas
inventadas: ver `gera_rota.py`.

    python3 simula_gps.py

Teclas, sem Enter:

    a   acelera 1 km/h
    d   desacelera 1 km/h
    0   para o veículo
    q   sai

O console mostra volta, percentual e velocidade.
"""
from __future__ import annotations

import argparse
import os
import pty
import select
import sys
import signal
import termios
import time
import tty
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from geo import metros, rumo_graus  # noqa: E402

AQUI = Path(__file__).resolve().parent
ROTA = AQUI / "rota_eixao.csv"

# O nome da pty muda a cada execução (`/dev/ttys012`, `013`, …), o que é
# incômodo de copiar e impossível de fixar num script. O link resolve: quem
# consome abre sempre o mesmo caminho.
LINK = AQUI / "serial"

# 4 Hz é a taxa nominal do RF01.4 — 250 ms de período. Emitir mais rápido
# esconderia problemas de acompanhamento que o aparelho teria no carro.
PERIODO_S = 0.25

VELOCIDADE_INICIAL_KMH = 60.0
# 1000 km/h é impossível para um carro, e é de propósito: serve para
# atravessar a volta de 33 km em dois minutos em vez de trinta e três, quando
# se quer chegar depressa a um trecho específico.
#
# Ainda é seguro para o teste de alertas. A 1000 km/h cada amostra avança
# 69 m, e um radar com raio de 300 m continua sendo visto por cerca de oito
# amostras seguidas. Só acima de ~8600 km/h um ponto passaria inteiro entre
# duas amostras e seria pulado sem nunca ser notado.
VELOCIDADE_MAXIMA_KMH = 1000.0
PASSO_KMH = 1.0

# Rumo calculado com antecedência, e não do trecho imediato. Os trechos têm
# ~190 m e a linha de centro guarda uma irregularidade residual de poucos
# graus; olhar 150 m à frente entrega um rumo estável, que é o que um veículo
# de verdade tem.
ANTECEDENCIA_M = 150.0


def carrega_rota(caminho: Path) -> list[tuple[float, float]]:
    pontos = []
    for linha in caminho.read_text(encoding="utf-8").splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#") or linha.startswith("lat"):
            continue
        lat, lon = linha.split(",")
        pontos.append((float(lat), float(lon)))
    if len(pontos) < 2:
        raise SystemExit(f"rota insuficiente em {caminho}")
    return pontos


class Percurso:
    """A rota percorrida como uma linha só: ida, volta, e de novo.

    Mantém a distância acumulada desde o início para poder interpolar sem
    procurar o segmento a cada passo.
    """

    def __init__(self, pontos: list[tuple[float, float]]):
        self.pontos = pontos
        self.acumulado = [0.0]
        for i in range(len(pontos) - 1):
            self.acumulado.append(self.acumulado[-1] + metros(pontos[i], pontos[i + 1]))
        self.comprimento = self.acumulado[-1]
        self.volta_completa = 2.0 * self.comprimento

    def posicao(self, distancia_na_ida: float) -> tuple[float, float]:
        """Interpola a posição a `distancia_na_ida` metros do ponto inicial."""
        d = max(0.0, min(distancia_na_ida, self.comprimento))
        # busca binária no acumulado
        lo, hi = 0, len(self.acumulado) - 1
        while hi - lo > 1:
            meio = (lo + hi) // 2
            if self.acumulado[meio] <= d:
                lo = meio
            else:
                hi = meio
        trecho = self.acumulado[hi] - self.acumulado[lo]
        t = 0.0 if trecho <= 0 else (d - self.acumulado[lo]) / trecho
        a, b = self.pontos[lo], self.pontos[hi]
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

    def onde(self, rodado: float) -> tuple[float, float, bool, int, float]:
        """Devolve (lat, lon, indo_para_o_norte, volta, fracao_da_volta)."""
        volta = int(rodado // self.volta_completa)
        na_volta = rodado % self.volta_completa
        indo = na_volta < self.comprimento
        d = na_volta if indo else self.volta_completa - na_volta
        lat, lon = self.posicao(d)
        return lat, lon, indo, volta, na_volta / self.volta_completa


def inicio_da_grade(agora: datetime) -> datetime:
    """O instante da grade de 250 ms imediatamente anterior a `agora`."""
    passo_us = int(PERIODO_S * 1_000_000)
    return agora.replace(microsecond=(agora.microsecond // passo_us) * passo_us)


def carimbo(base: datetime, k: int) -> datetime:
    """O instante da `k`-ésima sentença contada de `base`.

    Contado da base, e não do relógio de cada volta do laço: o carimbo
    descreve a GRADE de 4 Hz que o módulo emite. Lido do relógio, dois
    quartos seguidos podiam cair no mesmo centésimo, ou pular um.
    """
    return base + timedelta(seconds=k * PERIODO_S)


def grau_minuto(v: float, digitos: int) -> str:
    """Graus e minutos como a NMEA os escreve: `GGMM.MMMM`.

    ⚠️ **Arredondado em inteiro, e não pelo `%07.4f`.** A 59,99999 minutos
    o formato escrevia `60.0000`: uma sentença que diz "15 graus e 60
    minutos", que o parser ou lê como outro lugar ou recusa. O minuto cheio
    tem de subir para o grau.
    """
    v = abs(v)
    g = int(v)
    # Em décimos de milésimo de minuto, que é a resolução do campo.
    decimos = round((v - g) * 60.0 * 10_000)
    sobe, decimos = divmod(decimos, 60 * 10_000)
    g += sobe
    return f"{g:0{digitos}d}{decimos // 10_000:02d}.{decimos % 10_000:04d}"


def frase_rmc(lat: float, lon: float, kmh: float, rumo: float,
              agora: datetime) -> str:
    """Monta a RMC como o módulo monta, com o checksum calculado."""

    ns = "S" if lat < 0 else "N"
    ew = "W" if lon < 0 else "E"
    nos = kmh / 1.852

    # Com o veículo parado o módulo real deixa o rumo em branco. Reproduzir
    # isso é o ponto do simulador: é o caso que quebra parser desatento.
    campo_rumo = "" if kmh < 0.1 else f"{rumo:.1f}"

    # A fração do segundo é do carimbo, e não `.00` fixo: a 4 Hz o módulo
    # real emite .00, .25, .50 e .75, e quatro sentenças por segundo com o
    # mesmo instante escondem erro de acompanhamento de quem as lê.
    corpo = (f"$GNRMC,{agora:%H%M%S}.{agora.microsecond // 10_000:02d},A,"
             f"{grau_minuto(lat, 2)},{ns},{grau_minuto(lon, 3)},{ew},"
             f"{nos:.2f},{campo_rumo},{agora:%d%m%y},,,A")
    cs = 0
    for c in corpo[1:]:
        cs ^= ord(c)
    return f"{corpo}*{cs:02X}\r\n"


def apaga_atalho_se_for_meu(link: Path, dispositivo: str) -> None:
    """Apaga o atalho só se ele ainda apontar para a MINHA pty.

    Duas instâncias usam o mesmo caminho: a segunda refaz o atalho para a
    pty dela. Ao sair, a primeira apagava o atalho da outra, e quem
    consumia perdia a serial de uma instância que continuava viva.
    """
    try:
        if link.is_symlink() and os.readlink(link) == dispositivo:
            link.unlink()
    except OSError:
        pass


def velocidade_valida(texto: str) -> float:
    """O valor de `--velocidade`, dentro do mesmo teto das teclas.

    Sem teto, `--velocidade 20000` fazia cada amostra avançar 1,4 km: um
    radar de 300 m de raio cabia inteiro entre duas sentenças e nunca era
    alertado -- com o simulador parecendo funcionar.
    """
    try:
        v = float(texto)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{texto!r} nao e um numero")
    if not 0.0 <= v <= VELOCIDADE_MAXIMA_KMH:
        raise argparse.ArgumentTypeError(
            f"{texto} km/h: o simulador vai de 0 a "
            f"{VELOCIDADE_MAXIMA_KMH:.0f} km/h")
    return v


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--rota", type=Path, default=ROTA)
    p.add_argument("--velocidade", type=velocidade_valida,
                   default=VELOCIDADE_INICIAL_KMH,
                   help=f"km/h iniciais, de 0 a {VELOCIDADE_MAXIMA_KMH:.0f}")
    p.add_argument("--sem-link", action="store_true",
                   help=f"nao cria o atalho {LINK.name}")
    p.add_argument("--sem-pausa", action="store_true",
                   help="comeca a emitir na hora, sem esperar tecla")
    args = p.parse_args(argv[1:])

    percurso = Percurso(carrega_rota(args.rota))
    mestre, escravo = pty.openpty()

    # ⚠️ **Modo cru, obrigatório.** Sem isto a disciplina de linha da pty
    # ALTERA os dados em trânsito: medido, `\r\n` chega como `\n`. Um
    # simulador que modifica a própria saída em silêncio é pior que nenhum.
    #
    # E o estrago cresce adiante: a configuração do módulo é UBX **binário**,
    # onde `0x11` e `0x13` seriam interpretados como XON/XOFF e o quadro
    # sumiria pela metade — com sintoma de "o módulo não respondeu".
    tty.setraw(escravo)

    # Não bloqueante: sem ninguém lendo o lado escravo, o buffer da pty (~8 KB)
    # enche em cerca de 110 sentenças — 28 s a 4 Hz. Bloqueando, o simulador
    # congelaria ali; assim ele descarta e segue, que é o que um GPS de verdade
    # faz quando ninguém escuta.
    os.set_blocking(mestre, False)

    print(f"  rota  : {args.rota.name}, {percurso.comprimento/1000:.2f} km por sentido")
    print(f"  volta : {percurso.volta_completa/1000:.2f} km (ida e volta)")
    dispositivo = os.ttyname(escravo)
    print(f"  serial: {dispositivo}")
    if not args.sem_link:
        try:
            if LINK.is_symlink() or LINK.exists():
                LINK.unlink()
            LINK.symlink_to(dispositivo)
            print(f"  atalho: {LINK}  ->  {dispositivo}")
        except OSError as e:
            print(f"  (nao deu para criar o atalho: {e})")
    print(f"  taxa  : {1/PERIODO_S:.0f} Hz")
    print("  teclas: a acelera · d desacelera · 0 para · q sai")
    print()
    kmh = args.velocidade
    rodado = 0.0
    descartadas = 0
    entrada = sys.stdin.fileno()
    anterior = termios.tcgetattr(entrada) if os.isatty(entrada) else None

    # SIGTERM e como este processo costuma morrer -- `kill`, `p.terminate()`
    # de quem o roda. Sem tratar, ele morre SEM passar pelo `finally`, e o
    # terminal de quem o chamou fica em cbreak, sem eco, ate alguem rodar
    # `stty sane`. Virando SystemExit, o encerramento e o mesmo do `q`.
    def encerra(_sinal, _quadro):
        raise SystemExit(0)

    signal.signal(signal.SIGTERM, encerra)

    try:
        # O cbreak fica **dentro** do try: se algo estourar adiante, o
        # `finally` devolve o terminal ao modo anterior. Ligá-lo fora
        # deixaria o shell do operador sem eco em caso de falha.
        #
        # Em modo canônico o kernel só entrega a linha no Enter — ele ainda
        # está montando ela, com direito a backspace. Por isso `input()` (e
        # `scanf()`, e `getchar()`) esperam Enter **por construção**: trocar
        # a função não muda nada, quem segura os bytes é o modo do terminal.
        # É a mesma disciplina de linha do `setraw` da pty lá em cima, aqui
        # do lado do teclado.
        #
        # ⚠️ Isto vale para as teclas de pilotagem (a/d/0/q) e **não** só
        # para a pausa. Amarrar o cbreak ao `--sem-pausa`, como esteve por
        # algumas horas, desliga o teclado inteiro nesse modo: as teclas
        # ficam na fila de edição de linha esperando um Enter que não vem.
        if anterior is not None:
            tty.setcbreak(entrada)
            termios.tcflush(entrada, termios.TCIFLUSH)

        if anterior is not None and not args.sem_pausa:
            print("  qualquer tecla para iniciar · q sai... ",
                  end="", flush=True)
            if os.read(entrada, 1).decode(errors="ignore").lower() == "q":
                return 0
            print()
        # Sem tty (saída em pipe) não há tecla a esperar, e pausar travaria
        # um uso perfeitamente válido: `simula_gps.py | tee captura.nmea`.

        proximo = time.monotonic()
        # A grade do carimbo e fixada uma vez, no inicio: e ela que da a
        # cadencia de .00, .25, .50, .75 da taxa nominal.
        base_do_tempo = inicio_da_grade(datetime.now(timezone.utc))
        emitidas = 0
        while True:
            # --- teclado, sem bloquear ---
            # `anterior is not None` significa "stdin é um terminal". Sem
            # isso o laço gira para sempre quando stdin não é tty: `select`
            # reporta /dev/null (ou um pipe fechado) como sempre legível, o
            # `os.read` devolve b"" de EOF, nenhum ramo casa, e a condição
            # continua verdadeira. Fica em espera ocupada e nunca emite uma
            # sentença — sem erro, sem saída, só um processo a 100% de CPU.
            while anterior is not None and select.select([entrada], [], [], 0)[0]:
                tecla = os.read(entrada, 1).decode(errors="ignore").lower()
                if tecla == "a":
                    kmh = min(VELOCIDADE_MAXIMA_KMH, kmh + PASSO_KMH)
                elif tecla == "d":
                    kmh = max(0.0, kmh - PASSO_KMH)
                elif tecla == "0":
                    kmh = 0.0
                elif tecla == "q":
                    return 0

            lat, lon, indo, volta, fracao = percurso.onde(rodado)

            # Rumo com antecedência, no sentido em que o veículo anda.
            na_volta = rodado % percurso.volta_completa
            d = na_volta if indo else percurso.volta_completa - na_volta
            adiante = d + ANTECEDENCIA_M if indo else d - ANTECEDENCIA_M
            frente = percurso.posicao(adiante)
            rumo = rumo_graus((lat, lon), frente) if (lat, lon) != frente else 0.0

            frase = frase_rmc(lat, lon, kmh, rumo,
                              carimbo(base_do_tempo, emitidas)).encode()
            emitidas += 1
            try:
                escritos = os.write(mestre, frase)
                if escritos != len(frase):
                    # Escrita parcial deixaria meia sentença na linha. O
                    # parser se recupera na próxima `$`, mas conta como perda.
                    descartadas += 1
            except BlockingIOError:
                # Buffer cheio: ninguém está lendo o lado escravo. Descartar é
                # o comportamento certo — a alternativa é o simulador parar de
                # andar porque o consumidor não apareceu.
                descartadas += 1

            sentido = "norte" if indo else "sul "
            # As descartadas ficam à vista: sem isso, "ninguém está lendo a
            # serial" se parece com "o simulador está funcionando".
            aviso = f" | {descartadas} descartadas" if descartadas else ""
            sys.stdout.write(
                f"\r  volta {volta + 1:3d} | {fracao*100:5.1f}% | "
                f"{kmh:5.1f} km/h | {sentido} | "
                f"{lat:.5f},{lon:.5f}{aviso}   ")
            sys.stdout.flush()

            rodado += (kmh / 3.6) * PERIODO_S
            proximo += PERIODO_S
            atraso = proximo - time.monotonic()
            if atraso > 0:
                time.sleep(atraso)
            else:
                proximo = time.monotonic()  # recupera se algo atrasou
    except KeyboardInterrupt:
        return 0
    finally:
        if anterior is not None:
            termios.tcsetattr(entrada, termios.TCSADRAIN, anterior)
        os.close(mestre)
        os.close(escravo)
        if not args.sem_link:
            # So o MEU: o atalho pode ja ter sido refeito por outra
            # instancia, que continua viva.
            apaga_atalho_se_for_meu(LINK, dispositivo)
        print()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
