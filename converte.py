#!/usr/bin/env python3
"""
converte.py — parser de referência: base no padrão iGO8 -> radares.bin

ENTRADA — CSV com cabeçalho:
    X,Y,TYPE,SPEED,DirType,Direction
    X = longitude (PRIMEIRO), Y = latitude

A ordem X,Y com longitude primeiro é a armadilha clássica deste formato:
trocar as duas não gera erro algum, as coordenadas caem no oceano Índico e o
sistema simplesmente nunca detecta nada.

OUTRO FORMATO DE ENTRADA? Use este arquivo como modelo. Todo o trabalho de
formato binário está em formato_radares.py — seu parser só precisa produzir
uma lista de `Ponto` e chamar `escreve()`. Ver o docstring de lá.

    python3 converte.py base_igo8.txt radares.bin
"""
import csv
import sys
from pathlib import Path

from formato_radares import Ponto, TipoPonto, Sentido, escreve, le

# Colunas do padrão iGO8, na ordem em que aparecem.
COL_LON, COL_LAT, COL_TIPO, COL_LIMITE, COL_SENTIDO, COL_RUMO = (
    "X", "Y", "TYPE", "SPEED", "DirType", "Direction"
)


def parse(entrada):
    """Rende (pontos, rejeitados). Rejeita, nunca 'conserta' em silêncio."""
    pontos, rejeitados = [], []
    with open(entrada, newline="", encoding="utf-8-sig", errors="replace") as f:
        for n, r in enumerate(csv.DictReader(f), start=2):   # linha 1 = cabeçalho
            try:
                lon = float(r[COL_LON])      # X vem PRIMEIRO
                lat = float(r[COL_LAT])
                tipo = TipoPonto(int(r[COL_TIPO]))
                limite = int(r[COL_LIMITE])
                sentido = Sentido(int(r[COL_SENTIDO]))
                rumo = int(r[COL_RUMO]) % 360
            except (TypeError, KeyError) as e:
                rejeitados.append((n, f"campo ausente: {e}")); continue
            except ValueError as e:
                rejeitados.append((n, f"valor inválido: {e}")); continue

            # Validação de domínio geográfico. Ajuste a caixa se a sua base
            # cobrir outra região.
            if not (-34.0 <= lat <= 6.0):
                rejeitados.append((n, f"latitude fora da região: {lat}")); continue
            if not (-75.0 <= lon <= -33.0):
                rejeitados.append((n, f"longitude fora da região: {lon}")); continue
            if not (0 <= limite <= 255):
                rejeitados.append((n, f"limite fora de 0-255: {limite}")); continue

            pontos.append(Ponto(lat=lat, lon=lon, limite=limite, rumo=rumo,
                                tipo=tipo, sentido=sentido))
    return pontos, rejeitados


def main():
    ent = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("base_igo8.txt")
    sai = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("radares.bin")

    pontos, rejeitados = parse(ent)
    if not pontos:
        print(f"erro: nenhum ponto válido em {ent}", file=sys.stderr)
        return 1

    n = escreve(pontos, sai)
    tam = sai.stat().st_size
    print(f"{ent} -> {sai}")
    print(f"  {n} registros gravados, {tam} B ({tam / 1024:.1f} KB)")
    print(f"  RAM no Pico: {n * 12 / 1024:.1f} KB")

    le(sai)      # relê com as mesmas validações do firmware
    print("  validado: cabeçalho, CRC-32 e ordenação por latitude")

    if rejeitados:
        print(f"  {len(rejeitados)} linhas REJEITADAS:")
        for ln, m in rejeitados[:20]:
            print(f"     linha {ln}: {m}")
        if len(rejeitados) > 20:
            print(f"     ... e mais {len(rejeitados) - 20}")
        return 1
    print("  nenhuma linha rejeitada")
    return 0


if __name__ == "__main__":
    sys.exit(main())
