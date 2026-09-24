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
import math
import os
import pty
import select
import sys
import termios
import time
import tty
from datetime import datetime, timezone
from pathlib import Path

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


def metros(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Equirretangular. A mesma aproximação que o firmware usa, e pelo mesmo
    motivo: sobre 16 km o erro contra Haversine é de centímetros."""
    return math.hypot((b[0] - a[0]) * 111320.0,
                      (b[1] - a[1]) * 111320.0 * math.cos(math.radians(a[0])))


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


def rumo_graus(a: tuple[float, float], b: tuple[float, float]) -> float:
    dy = (b[0] - a[0]) * 111320.0
    dx = (b[1] - a[1]) * 111320.0 * math.cos(math.radians(a[0]))
    return math.degrees(math.atan2(dx, dy)) % 360.0


def frase_rmc(lat: float, lon: float, kmh: float, rumo: float,
              agora: datetime) -> str:
    """Monta a RMC como o módulo monta, com o checksum calculado."""
    def grau_minuto(v: float, digitos: int) -> str:
        v = abs(v)
        g = int(v)
        m = (v - g) * 60.0
        return f"{g:0{digitos}d}{m:07.4f}"

    ns = "S" if lat < 0 else "N"
    ew = "W" if lon < 0 else "E"
    nos = kmh / 1.852

    # Com o veículo parado o módulo real deixa o rumo em branco. Reproduzir
    # isso é o ponto do simulador: é o caso que quebra parser desatento.
    campo_rumo = "" if kmh < 0.1 else f"{rumo:.1f}"

    corpo = (f"$GNRMC,{agora:%H%M%S}.00,A,"
             f"{grau_minuto(lat, 2)},{ns},{grau_minuto(lon, 3)},{ew},"
             f"{nos:.2f},{campo_rumo},{agora:%d%m%y},,,A")
    cs = 0
    for c in corpo[1:]:
        cs ^= ord(c)
    return f"{corpo}*{cs:02X}\r\n"


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--rota", type=Path, default=ROTA)
    p.add_argument("--velocidade", type=float, default=VELOCIDADE_INICIAL_KMH,
                   help="km/h iniciais")
    p.add_argument("--sem-link", action="store_true",
                   help=f"nao cria o atalho {LINK.name}")
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
    kmh = max(0.0, args.velocidade)
    rodado = 0.0
    descartadas = 0
    entrada = sys.stdin.fileno()
    anterior = termios.tcgetattr(entrada) if os.isatty(entrada) else None

    try:
        # A pausa fica **dentro** do try, e o cbreak vem antes dela: se algo
        # estourar durante a espera, o `finally` devolve o terminal ao modo
        # anterior. Ligar o cbreak fora do try deixaria o shell do operador
        # sem eco caso a espera falhasse.
        if anterior is not None:
            # Em modo canônico o kernel só entrega a linha no Enter — ele
            # ainda está montando ela, com direito a backspace. Por isso
            # `input()` (e `scanf()`, e `getchar()`) esperam Enter **por
            # construção**: trocar a função não muda nada, quem segura os
            # bytes é o modo do terminal. É a mesma disciplina de linha do
            # `setraw` da pty lá em cima, aqui do lado do teclado.
            tty.setcbreak(entrada)
            print("  qualquer tecla para iniciar · q sai... ",
                  end="", flush=True)
            if os.read(entrada, 1).decode(errors="ignore").lower() == "q":
                return 0
            print()
        # Sem tty (saída em pipe) não há tecla a esperar, e pausar travaria
        # um uso perfeitamente válido: `simula_gps.py | tee captura.nmea`.

        proximo = time.monotonic()
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
                              datetime.now(timezone.utc)).encode()
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
        if not args.sem_link and LINK.is_symlink():
            try:
                LINK.unlink()   # o atalho aponta para uma pty que ja morreu
            except OSError:
                pass
        print()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
