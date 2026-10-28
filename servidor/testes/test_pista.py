"""Quanto cada radar passa longe da trilha — a medida que separa as pistas.

    python3 -m unittest discover -s servidor/testes

`unittest` e não pytest, como no resto do projeto.

⚠️ **Toda coordenada daqui é sintética**, à volta de (-10, -40), o mesmo par
dos exemplos do `formato_logs`. Rota real de viagem num arquivo versionado
seria o vazamento que a `privacidade.py` existe para impedir.
"""

import datetime
import sys
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import pista  # noqa: E402
import viagem  # noqa: E402

LAT0 = -10.0
LON0 = -40.0

#: Repetido de propósito: se alguém trocar a constante no módulo, estes testes
#: têm de reprovar em vez de acompanhar.
M_POR_GRAU = 111_320.0


def norte(metros: float) -> float:
    return LAT0 + metros / M_POR_GRAU


def leste(metros: float) -> float:
    import math
    return LON0 + metros / (M_POR_GRAU * math.cos(math.radians(LAT0)))


def ponto(quando_s: int, norte_m: float, leste_m: float = 0.0,
          kmh: float = 60.0) -> viagem.PontoViagem:
    return viagem.PontoViagem(
        quando=datetime.datetime(2026, 10, 28, 12, 0, 0,
                                 tzinfo=datetime.timezone.utc)
        + datetime.timedelta(seconds=quando_s),
        lat=norte(norte_m), lon=leste(leste_m),
        v_media_kmh=kmh, dist_km=0.0)


def trilha_ao_norte(comprimento_m: float = 400.0, passo_m: float = 130.0,
                    leste_m: float = 0.0, kmh: float = 60.0) -> viagem.Viagem:
    """Uma reta indo ao norte, com o espaçamento REAL de um log de 6 s.

    130 m entre amostras é o que a 78 km/h sai do arquivo v3. É esse
    espaçamento que torna o ponto-a-amostra inútil, e é por isso que a trilha
    dos testes o reproduz em vez de usar pontos densos de laboratório.
    """
    pontos = []
    d = 0.0
    k = 0
    while d <= comprimento_m:
        pontos.append(ponto(k * 6, d, leste_m, kmh))
        d += passo_m
        k += 1
    return viagem.Viagem(nome="t.log", pontos=pontos)


def radar(norte_m: float, leste_m: float, limite: int = 60,
          rumo: int = 0, sentido: int = 0) -> viagem.RadarNoMapa:
    return viagem.RadarNoMapa(norte(norte_m), leste(leste_m), limite,
                              rumo, sentido)


class DistanciaAoSegmento(unittest.TestCase):

    def test_ao_lado_do_meio_e_a_perpendicular(self):
        self.assertAlmostEqual(
            pista.distancia_ponto_segmento(50.0, 20.0, 0.0, 0.0, 100.0, 0.0),
            20.0, places=6)

    def test_alem_da_ponta_gruda_na_ponta(self):
        # Projetar na reta INFINITA daria 20 m e inventaria uma passagem num
        # trecho que o carro não percorreu.
        d = pista.distancia_ponto_segmento(200.0, 20.0, 0.0, 0.0, 100.0, 0.0)
        self.assertAlmostEqual(d, (100.0 ** 2 + 20.0 ** 2) ** 0.5, places=6)

    def test_segmento_degenerado_nao_divide_por_zero(self):
        self.assertAlmostEqual(
            pista.distancia_ponto_segmento(3.0, 4.0, 1.0, 1.0, 1.0, 1.0),
            (2.0 ** 2 + 3.0 ** 2) ** 0.5, places=6)


class FiltroDeSentido(unittest.TestCase):

    def test_omnidirecional_nunca_descarta(self):
        self.assertTrue(pista.sentido_compativel(radar(0, 0, sentido=0), 180.0))

    def test_unidirecional_aceita_o_proprio_rumo(self):
        self.assertTrue(
            pista.sentido_compativel(radar(0, 0, rumo=90, sentido=1), 100.0))

    def test_unidirecional_recusa_o_oposto(self):
        self.assertFalse(
            pista.sentido_compativel(radar(0, 0, rumo=90, sentido=1), 270.0))

    def test_bidirecional_aceita_o_oposto(self):
        self.assertTrue(
            pista.sentido_compativel(radar(0, 0, rumo=90, sentido=2), 270.0))

    def test_a_diferenca_de_rumo_atravessa_o_norte(self):
        self.assertAlmostEqual(pista.diferenca_rumo(358.0, 2.0), 4.0)


class Segmentos(unittest.TestCase):

    def test_trecho_parado_nao_vira_segmento(self):
        v = viagem.Viagem(nome="t.log", pontos=[
            ponto(0, 0.0, kmh=60.0),
            ponto(6, 130.0, kmh=2.0),
            ponto(12, 131.0, kmh=1.0),
        ])
        self.assertEqual(pista.segmentos_de(v), [])

    def test_o_rumo_sai_da_geometria_do_trecho(self):
        v = trilha_ao_norte(comprimento_m=260.0)
        for s in pista.segmentos_de(v):
            self.assertAlmostEqual(s.rumo, 0.0, places=1)


class PassagensDaViagem(unittest.TestCase):

    def test_o_espacamento_de_seis_segundos_nao_infla_a_distancia(self):
        # 🔴 A armadilha que este módulo existe para não cair. O radar está
        # 3 m ao lado da trilha, mas no MEIO de um trecho de 130 m entre
        # amostras. Ponto-a-amostra responderia ~65 m e esconderia a pista.
        r = radar(65.0, 3.0)
        d = pista.passagens_da_viagem(trilha_ao_norte(), [r])
        self.assertAlmostEqual(d[0], 3.0, delta=0.5)

    def test_uma_viagem_conta_uma_passagem_por_radar(self):
        # Dez amostras perto do mesmo ponto sao a mesma passagem. Contar por
        # amostra daria peso de dez viagens a uma so.
        r = radar(200.0, 25.0)
        d = pista.passagens_da_viagem(
            trilha_ao_norte(passo_m=20.0), [r])
        self.assertEqual(list(d), [0])

    def test_radar_do_sentido_contrario_nao_entra(self):
        # Indo ao norte, um radar que olha para o sul é da pista de volta. Sem
        # este filtro ele acumularia 3 m na ida e 40 m na volta, e a mediana
        # não separaria nada.
        r = radar(200.0, 3.0, rumo=180, sentido=1)
        self.assertEqual(pista.passagens_da_viagem(trilha_ao_norte(), [r]), {})

    def test_radar_alem_do_raio_nao_entra(self):
        r = radar(200.0, pista.RAIO_M + 50.0)
        self.assertEqual(pista.passagens_da_viagem(trilha_ao_norte(), [r]), {})

    def test_a_grade_nao_perde_radar_na_fronteira_de_celula(self):
        # O índice espacial é otimização, e otimização que perde ponto vira
        # alerta que some. A célula tem 0,005 grau; o radar vai bem em cima da
        # borda, e tem de ser achado assim mesmo.
        borda = (int(LAT0 / pista.CELULA_GRAUS)) * pista.CELULA_GRAUS
        r = viagem.RadarNoMapa(borda, leste(3.0), 60, 0, 0)
        metros_ate_a_borda = (borda - LAT0) * M_POR_GRAU
        v = trilha_ao_norte(comprimento_m=abs(metros_ate_a_borda) + 260.0)
        self.assertIn(0, pista.passagens_da_viagem(v, [r]))


class Estatisticas(unittest.TestCase):

    def test_poucas_passagens_nao_viram_verdade(self):
        acc = {0: [27.0, 26.0]}
        self.assertEqual(
            pista.estatisticas(acc, [radar(0, 27)], passagens_minimas=3), [])

    def test_a_mediana_ignora_a_passada_ruim(self):
        # Média daria 36 m e poria um radar da própria pista na lista dos
        # alheios. HDOP alto acontece; a mediana nem percebe.
        acc = {0: [3.0, 3.5, 2.8, 150.0]}
        e = pista.estatisticas(acc, [radar(0, 3)], passagens_minimas=3)[0]
        self.assertLess(e.mediana_m, 5.0)
        self.assertEqual(e.passagens, 4)
        self.assertEqual(e.maior_m, 150.0)

    def test_sai_ordenado_do_mais_afastado_para_o_mais_perto(self):
        acc = {0: [3.0] * 3, 1: [27.0] * 3}
        saida = pista.estatisticas(acc, [radar(0, 3), radar(0, 27)])
        self.assertEqual([round(e.mediana_m) for e in saida], [27, 3])

    def test_a_estatistica_carrega_o_limite_do_radar(self):
        acc = {0: [27.0] * 3}
        self.assertEqual(
            pista.estatisticas(acc, [radar(0, 27, limite=60)])[0].limite, 60)


class Acumula(unittest.TestCase):

    def test_junta_viagens_e_conta_uma_por_viagem(self):
        r = radar(200.0, 10.0)
        tudo = pista.acumula([trilha_ao_norte(), trilha_ao_norte()], [r])
        self.assertEqual(len(tudo[0]), 2)

    def test_viagem_que_nao_passa_perto_nao_entra(self):
        r = radar(200.0, 10.0)
        longe = viagem.Viagem(nome="x.log", pontos=[
            ponto(0, 50_000.0), ponto(6, 50_130.0), ponto(12, 50_260.0)])
        self.assertEqual(pista.acumula([longe], [r]), {})


if __name__ == "__main__":
    unittest.main()
