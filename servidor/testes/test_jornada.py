"""Trechos da MESMA viagem, reunidos.

    python3 -m unittest discover -s servidor/testes

O aparelho não encerra a viagem quando o carro é desligado: sem fix ele nem
chega a contar o tempo parado, e o `viagem.est` a retoma no religamento com a
distância continuando de onde parou. O arquivo, porém, ganha nome novo — o da
hora da retomada. Uma viagem com três pausas vira quatro arquivos.

Isso produziu dois sintomas em 11/10/2026: a viagem aparecia partida, e a
média de um trecho curto saía em 196 km/h, porque a distância ACUMULADA da
viagem inteira era dividida pela duração daquele pedaço.

⚠️ Coordenadas sintéticas. Os arquivos reais começam em casa.
"""

import datetime
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import viagem  # noqa: E402


def trecho(inicio: str, pontos: int, dist0: float, passo_s: int = 6,
           kmh: float = 40.0, lat0: float = -10.0) -> str:
    """Um trecho v3 que começa com a distância já em `dist0`.

    É o que o firmware grava ao RETOMAR: a distância é cumulativa da viagem,
    não do arquivo.
    """
    t0 = datetime.datetime.strptime(inicio, "%Y-%m-%dT%H:%M:%SZ")
    passo_km = kmh * (passo_s / 3600.0)
    linhas = ["# coruja_gps viagem v3",
              "utc;lat;lon;v_media;dist_km;radar_m;radar_kmh;perto_m;"
              "perto_kmh;rumo;zona;n_radares"]
    for i in range(pontos):
        q = t0 + datetime.timedelta(seconds=i * passo_s)
        linhas.append(
            f"{q.strftime('%Y-%m-%dT%H:%M:%SZ')};"
            f"{lat0 + i * 0.001:.5f};{-40.0 + i * 0.001:.5f};"
            f"{kmh:.1f};{dist0 + i * passo_km:.2f};;;;;90.0;1;0")
    return "\n".join(linhas) + "\n"


def grava(pasta: Path, nome: str, texto: str) -> Path:
    caminho = pasta / nome
    caminho.write_text(texto, encoding="utf-8")
    return caminho


class DistanciaDoTrecho(unittest.TestCase):
    """A distância de um trecho é o que se andou NELE."""

    def test_trecho_retomado_nao_herda_a_distancia_anterior(self):
        # O caso de 11/10/2026: o terceiro trecho começou com 15,87 km já
        # rodados e andou 1,45. Reportar 17,32 como "distância do trecho"
        # é somar o que foi andado antes da pausa.
        with tempfile.TemporaryDirectory() as d:
            c = grava(Path(d), "20261011_152603.log",
                      trecho("2026-10-11T15:26:00Z", 54, dist0=15.87))
            v = viagem.le_viagem(c)
            self.assertAlmostEqual(v.distancia_km, 53 * 40.0 * 6 / 3600,
                                   places=2)

    def test_a_media_do_trecho_sai_plausivel(self):
        # 196,1 km/h com máxima de 45,2 foi o que a página mostrou. A média
        # do percurso não pode passar da máxima observada.
        with tempfile.TemporaryDirectory() as d:
            c = grava(Path(d), "20261011_152603.log",
                      trecho("2026-10-11T15:26:00Z", 54, dist0=15.87))
            v = viagem.le_viagem(c)
            self.assertLessEqual(v.v_media_kmh, v.v_maxima_kmh + 0.1)

    def test_trecho_que_comeca_do_zero_nao_muda(self):
        # A correção não pode mexer no caso comum, que é a viagem inteira
        # num arquivo só.
        with tempfile.TemporaryDirectory() as d:
            c = grava(Path(d), "20261011_124010.log",
                      trecho("2026-10-11T12:40:00Z", 163, dist0=0.0))
            v = viagem.le_viagem(c)
            self.assertAlmostEqual(v.distancia_km, 162 * 40.0 * 6 / 3600,
                                   places=2)


class AgrupamentoEmJornadas(unittest.TestCase):
    """Trechos encadeados pela distância são a mesma viagem."""

    def monta(self, d: Path) -> list[viagem.Viagem]:
        grava(d, "20261011_124010.log",
              trecho("2026-10-11T12:40:00Z", 163, dist0=0.0))
        grava(d, "20261011_143912.log",
              trecho("2026-10-11T14:39:12Z", 134, dist0=10.80))
        grava(d, "20261011_152603.log",
              trecho("2026-10-11T15:26:00Z", 54, dist0=19.73))
        grava(d, "20261011_192802.log",
              trecho("2026-10-11T19:28:00Z", 143, dist0=0.0))
        return [viagem.le_viagem(p) for p in sorted(d.glob("*.log"))]

    def test_tres_trechos_encadeados_viram_uma_jornada(self):
        with tempfile.TemporaryDirectory() as d:
            js = viagem.agrupa_em_jornadas(self.monta(Path(d)))
            self.assertEqual(len(js), 2)
            self.assertEqual(len(js[0].trechos), 3)
            self.assertEqual(len(js[1].trechos), 1)

    def test_distancia_zerada_comeca_jornada_nova(self):
        # O sinal de viagem nova é a distância VOLTAR, não o tempo: duas
        # viagens podem ser seguidas, e uma pausa pode durar horas.
        with tempfile.TemporaryDirectory() as d:
            js = viagem.agrupa_em_jornadas(self.monta(Path(d)))
            self.assertEqual(js[1].trechos[0].nome, "20261011_192802.log")

    def test_a_jornada_soma_a_distancia_dos_trechos(self):
        with tempfile.TemporaryDirectory() as d:
            js = viagem.agrupa_em_jornadas(self.monta(Path(d)))
            esperado = sum(t.distancia_km for t in js[0].trechos)
            self.assertAlmostEqual(js[0].distancia_km, esperado, places=2)

    def test_a_media_ignora_o_tempo_parado_entre_trechos(self):
        # Do primeiro registro ao último vão quase sete horas, das quais seis
        # com o carro desligado. Dividir por elas daria uma média que não
        # descreve viagem nenhuma.
        with tempfile.TemporaryDirectory() as d:
            j = viagem.agrupa_em_jornadas(self.monta(Path(d)))[0]
            self.assertAlmostEqual(j.v_media_kmh, 40.0, delta=1.0)
            self.assertLess(j.duracao_gravada.total_seconds(),
                            (j.fim - j.inicio).total_seconds())

    def test_a_maxima_da_jornada_e_a_maior_dos_trechos(self):
        with tempfile.TemporaryDirectory() as d:
            j = viagem.agrupa_em_jornadas(self.monta(Path(d)))[0]
            self.assertAlmostEqual(
                j.v_maxima_kmh, max(t.v_maxima_kmh for t in j.trechos),
                places=1)

    def test_a_jornada_conta_as_pausas(self):
        with tempfile.TemporaryDirectory() as d:
            j = viagem.agrupa_em_jornadas(self.monta(Path(d)))[0]
            self.assertEqual(j.pausas, 2)      # tres trechos, duas pausas

    def test_qualquer_trecho_encontra_a_jornada_dele(self):
        # As URLs antigas apontam para um trecho do meio. Elas têm de
        # continuar abrindo -- agora a viagem inteira.
        with tempfile.TemporaryDirectory() as d:
            js = viagem.agrupa_em_jornadas(self.monta(Path(d)))
            achada = viagem.jornada_de(js, "20261011_152603.log")
            self.assertIsNotNone(achada)
            self.assertEqual(achada.trechos[0].nome, "20261011_124010.log")

    def test_lista_vazia_nao_estoura(self):
        self.assertEqual(viagem.agrupa_em_jornadas([]), [])


class MapaDaJornada(unittest.TestCase):
    """Os trechos num mapa só, sem inventar o caminho das pausas."""

    def test_cada_trecho_e_uma_linha_separada(self):
        # Ligar o fim de um trecho ao começo do outro desenharia um trajeto
        # que ninguém percorreu -- e, pior, em linha reta por cima de
        # quarteirões.
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            grava(p, "a.log", trecho("2026-10-11T12:40:00Z", 20, 0.0))
            grava(p, "b.log", trecho("2026-10-11T14:39:12Z", 20, 5.0,
                                     lat0=-10.5))
            j = viagem.Jornada([viagem.le_viagem(p / "a.log"),
                                viagem.le_viagem(p / "b.log")])
            gj = viagem.trilha_da_jornada(j)
            self.assertEqual(len(gj["features"]), 2)

    def test_nenhuma_linha_atravessa_a_pausa(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            grava(p, "a.log", trecho("2026-10-11T12:40:00Z", 20, 0.0))
            grava(p, "b.log", trecho("2026-10-11T14:39:12Z", 20, 5.0,
                                     lat0=-10.5))
            j = viagem.Jornada([viagem.le_viagem(p / "a.log"),
                                viagem.le_viagem(p / "b.log")])
            for f in viagem.trilha_da_jornada(j)["features"]:
                lats = [c[1] for c in f["geometry"]["coordinates"]]
                # Um salto de meio grau (~55 km) entre vertices vizinhos so
                # apareceria se os dois trechos estivessem na mesma linha.
                saltos = [abs(b - a) for a, b in zip(lats, lats[1:])]
                self.assertLess(max(saltos), 0.1)


if __name__ == "__main__":
    unittest.main()
