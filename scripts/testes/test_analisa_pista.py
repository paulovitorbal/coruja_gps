"""A geometria do `analisa_pista`.

É onde o erro mora: trocar seno por cosseno, ou o sinal da perpendicular,
produz números plausíveis e errados — e um limiar escolhido sobre eles
mandaria o firmware esconder o radar certo.
"""
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import analisa_pista as A  # noqa: E402


class Geometria(unittest.TestCase):
    def test_norte_puro_e_so_avanco(self):
        # Rumo 0 (norte) e um ponto ao norte: tudo ao longo, nada de lado.
        ao_longo, perp = A.ao_longo_e_perpendicular(0.0, 100.0, 0.0)
        self.assertAlmostEqual(ao_longo, 100.0)
        self.assertAlmostEqual(perp, 0.0)

    def test_ponto_a_direita_de_quem_vai_ao_norte(self):
        # Indo ao norte, um ponto a leste esta a DIREITA: perpendicular
        # positiva, avanco zero.
        ao_longo, perp = A.ao_longo_e_perpendicular(30.0, 0.0, 0.0)
        self.assertAlmostEqual(ao_longo, 0.0)
        self.assertAlmostEqual(perp, 30.0)

    def test_ponto_a_esquerda_tem_perpendicular_negativa(self):
        ao_longo, perp = A.ao_longo_e_perpendicular(-30.0, 0.0, 0.0)
        self.assertAlmostEqual(perp, -30.0)

    def test_indo_ao_LESTE_o_ponto_ao_sul_esta_a_direita(self):
        # Rumo 90 (leste). Um ponto ao sul (norte negativo) fica a direita.
        # Este caso pega troca de seno com cosseno: no rumo 0 varios erros
        # passariam despercebidos porque sen(0)=0.
        ao_longo, perp = A.ao_longo_e_perpendicular(0.0, -40.0, 90.0)
        self.assertAlmostEqual(ao_longo, 0.0)
        self.assertAlmostEqual(perp, 40.0)

    def test_indo_ao_leste_o_ponto_a_leste_e_avanco(self):
        ao_longo, perp = A.ao_longo_e_perpendicular(120.0, 0.0, 90.0)
        self.assertAlmostEqual(ao_longo, 120.0)
        self.assertAlmostEqual(perp, 0.0)

    def test_radar_atras_tem_avanco_negativo(self):
        # E o que separa "ja passei" de "esta chegando". Sem o sinal, um
        # radar deixado para tras entraria na estatistica como se estivesse
        # a frente.
        ao_longo, _ = A.ao_longo_e_perpendicular(0.0, -50.0, 0.0)
        self.assertLess(ao_longo, 0.0)

    def test_a_norma_se_conserva(self):
        # Decompor nao pode criar nem destruir distancia.
        for rumo in (0.0, 37.0, 90.0, 180.0, 271.5, 359.0):
            ao_longo, perp = A.ao_longo_e_perpendicular(37.0, -84.0, rumo)
            self.assertAlmostEqual(math.hypot(ao_longo, perp),
                                   math.hypot(37.0, -84.0), places=6)


class RumoEntrePontos(unittest.TestCase):
    def test_ao_norte(self):
        self.assertAlmostEqual(A.rumo_entre(-15.8, -47.9, -15.79, -47.9),
                               0.0, places=3)

    def test_ao_leste(self):
        self.assertAlmostEqual(A.rumo_entre(-15.8, -47.9, -15.8, -47.89),
                               90.0, places=3)

    def test_ao_sul_da_180_e_nao_menos_180(self):
        # `atan2` devolve negativo na metade sul; sem o `% 360` o rumo sairia
        # fora da convencao e toda comparacao com o rumo da base erraria.
        self.assertAlmostEqual(A.rumo_entre(-15.8, -47.9, -15.81, -47.9),
                               180.0, places=3)

    def test_a_oeste_da_270(self):
        self.assertAlmostEqual(A.rumo_entre(-15.8, -47.9, -15.8, -47.91),
                               270.0, places=3)


class MetrosRelativos(unittest.TestCase):
    def test_um_grau_de_latitude_sao_cerca_de_111_km(self):
        _, norte = A.metros_relativos(-15.8, -47.9, -14.8, -47.9)
        self.assertAlmostEqual(norte, A.M_POR_GRAU, places=3)

    def test_a_longitude_encolhe_pelo_cosseno_da_latitude(self):
        # Em Brasilia (lat -15.8) um grau de longitude vale ~107 km, nao 111.
        # Ignorar o cosseno inflaria toda distancia leste-oeste em 4%.
        leste, _ = A.metros_relativos(-15.8, -47.9, -15.8, -46.9)
        esperado = A.M_POR_GRAU * math.cos(math.radians(-15.8))
        self.assertAlmostEqual(leste, esperado, places=3)
        self.assertLess(leste, A.M_POR_GRAU)


class PistasParalelas(unittest.TestCase):
    """O caso do Eixão, montado de propósito."""

    def test_a_pista_paralela_aparece_na_perpendicular_e_nao_na_distancia(self):
        # Dois radares: um sobre a trajetoria 200 m a frente, outro na pista
        # ao lado, 40 m deslocado, tambem 200 m a frente. A DISTANCIA dos
        # dois e quase igual -- e por isso que ela nao separa as pistas. A
        # perpendicular separa.
        rumo = 0.0  # indo ao norte
        meu = A.ao_longo_e_perpendicular(0.0, 200.0, rumo)
        vizinho = A.ao_longo_e_perpendicular(40.0, 200.0, rumo)

        d_meu = math.hypot(0.0, 200.0)
        d_vizinho = math.hypot(40.0, 200.0)
        self.assertLess(abs(d_meu - d_vizinho), 5.0,
                        "as distancias deveriam ser quase iguais")

        self.assertAlmostEqual(abs(meu[1]), 0.0, places=6)
        self.assertAlmostEqual(abs(vizinho[1]), 40.0, places=6)


if __name__ == "__main__":
    unittest.main()
