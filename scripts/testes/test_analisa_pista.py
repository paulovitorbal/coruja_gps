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
import formato_radares as F  # noqa: E402

#: Origem das viagens sinteticas. Nenhum log real entra aqui: viagem e dado de
#: localizacao, e a geometria se prova melhor com uma pista que se conhece.
LAT0, LON0 = -15.8, -47.9

CABECALHO_V3 = ("# coruja_gps viagem v3\n"
                "utc;lat;lon;v_media;dist_km;radar_m;radar_kmh;perto_m;"
                "perto_kmh;rumo;zona;n_radares\n")


def deslocado(leste_m: float, norte_m: float) -> tuple[float, float]:
    """(lat, lon) a `leste_m`, `norte_m` metros da origem."""
    lat = LAT0 + norte_m / A.M_POR_GRAU
    lon = LON0 + leste_m / (A.M_POR_GRAU * math.cos(math.radians(LAT0)))
    return lat, lon


def linha_v3(segundo: int, leste_m: float, norte_m: float, kmh: float,
             rumo: float | None = None) -> str:
    """Uma linha de viagem v3 como o firmware grava, sem colunas de radar."""
    lat, lon = deslocado(leste_m, norte_m)
    hh, resto = divmod(segundo, 3600)
    mm, ss = divmod(resto, 60)
    campo_rumo = "" if rumo is None else f"{rumo:.1f}"
    return (f"2026-10-07T{12 + hh:02d}:{mm:02d}:{ss:02d}Z;{lat:.7f};"
            f"{lon:.7f};{kmh:.1f};0.00;;;;;{campo_rumo};1;0\n")


def radar(leste_m: float, norte_m: float, limite: int) -> F.RegistroLido:
    lat, lon = deslocado(leste_m, norte_m)
    return F.RegistroLido(lat, lon, limite, 0, F.TipoPonto.RADAR_FIXO,
                          F.Sentido.OMNIDIRECIONAL)


def indo_ao_norte(n: int, kmh: float = 80.0, periodo_s: int = 6,
                  inicio_s: int = 0, norte0_m: float = 0.0,
                  rumo: float | None = 0.0) -> list[str]:
    """`n` amostras de quem anda ao norte pela pista em leste = 0."""
    passo = kmh / 3.6 * periodo_s
    return [linha_v3(inicio_s + k * periodo_s, 0.0, norte0_m + k * passo,
                     kmh, rumo) for k in range(n)]


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


class LeituraDaViagem(unittest.TestCase):
    """O que entra pelo arquivo, conferido uma vez, na entrada."""

    def test_v3_e_lida_com_versao_e_tempo(self):
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(indo_ao_norte(3)))
        self.assertEqual(v.versao, "v3")
        self.assertEqual(len(v.pontos), 3)
        self.assertEqual(v.invalidas, 0)
        # O tempo vem do carimbo, em segundos: e ele que mede as janelas.
        self.assertEqual(v.pontos[1]["t"] - v.pontos[0]["t"], 6.0)

    def test_arquivo_sem_assinatura_e_recusado(self):
        # Um CSV qualquer com tres colunas numericas passaria por viagem, e
        # a analise mediria a coisa errada sem dizer.
        with self.assertRaises(ValueError) as e:
            A.interpreta_viagem("utc;lat;lon\n" + "".join(indo_ao_norte(3)))
        self.assertIn("coruja_gps viagem", str(e.exception))

    def test_versao_desconhecida_e_recusada(self):
        # Ler uma v4 por indice como se fosse v3 trocaria o significado das
        # colunas em silencio.
        texto = CABECALHO_V3.replace("v3", "v4") + "".join(indo_ao_norte(3))
        with self.assertRaises(ValueError) as e:
            A.interpreta_viagem(texto)
        self.assertIn("v4", str(e.exception))

    def test_linhas_invalidas_sao_contadas_e_nao_somem(self):
        texto = (CABECALHO_V3 + "".join(indo_ao_norte(2))
                 + "lixo;sem;numero;aqui;0\n"
                 + "2026-10-07T12:00:30Z;-15.8\n"
                 + "carimbo-ruim;-15.8;-47.9;80.0;0.0\n")
        v = A.interpreta_viagem(texto)
        self.assertEqual(len(v.pontos), 2)
        self.assertEqual(v.invalidas, 3)

    def test_coordenada_nao_finita_ou_fora_da_faixa_e_invalida(self):
        # `float()` aceita "nan" e "inf". Um nan na latitude passa por toda
        # comparacao como falso e some da conta sem erro nenhum.
        texto = (CABECALHO_V3 + "".join(indo_ao_norte(1))
                 + "2026-10-07T12:00:30Z;nan;-47.9;80.0;0.0\n"
                 + "2026-10-07T12:00:36Z;-15.8;inf;80.0;0.0\n"
                 + "2026-10-07T12:00:42Z;-95.0;-47.9;80.0;0.0\n"
                 + "2026-10-07T12:00:48Z;-15.8;-47.9;-1.0;0.0\n")
        v = A.interpreta_viagem(texto)
        self.assertEqual(len(v.pontos), 1)
        self.assertEqual(v.invalidas, 4)

    def test_main_reporta_as_linhas_invalidas(self):
        import io
        import tempfile
        from contextlib import redirect_stdout
        with tempfile.TemporaryDirectory() as tmp:
            viagem = Path(tmp) / "viagem.log"
            viagem.write_text(CABECALHO_V3 + "".join(indo_ao_norte(2))
                              + "lixo;a;b;c;d\n", encoding="utf-8")
            base = Path(tmp) / "radares.bin"
            F.escreve([F.Ponto(LAT0, LON0, 60, 0, F.TipoPonto.RADAR_FIXO,
                               F.Sentido.OMNIDIRECIONAL)], base)
            saida = io.StringIO()
            with redirect_stdout(saida):
                codigo = A.main(["analisa_pista.py", str(viagem), str(base)])
        self.assertEqual(codigo, 0)
        self.assertRegex(saida.getvalue(), r"(?m)^linhas invalidas\s*:\s*1$")

    def test_main_recusa_o_que_nao_e_viagem(self):
        import io
        import tempfile
        from contextlib import redirect_stderr
        with tempfile.TemporaryDirectory() as tmp:
            outro = Path(tmp) / "outro.csv"
            outro.write_text("utc;lat;lon\n", encoding="utf-8")
            erro = io.StringIO()
            with redirect_stderr(erro):
                codigo = A.main(["analisa_pista.py", str(outro),
                                 str(Path(tmp) / "nao-existe.bin")])
        self.assertEqual(codigo, 1)
        self.assertIn("nao e log de viagem", erro.getvalue())


class AproximacaoMinima(unittest.TestCase):
    """A distancia ate a TRAJETORIA, e nao ate a amostra mais proxima."""

    def test_radar_sobre_a_pista_entre_duas_amostras_distantes(self):
        # A 80 km/h e 6 s por fatia, as amostras ficam a 133 m. Um radar no
        # meio delas, EM CIMA da pista, ficava a 67 m da amostra mais
        # proxima -- numero de pista ao lado, quando o Eixao separa as
        # pistas por ~40 m. A medida inventava a separacao que devia medir.
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(indo_ao_norte(2)))
        passo = 80.0 / 3.6 * 6
        por_limite = A.aproximacao_minima(v, [radar(0.0, passo / 2, 60)])
        self.assertEqual(list(por_limite), [60])
        self.assertLess(por_limite[60][0], 0.5)

    def test_pista_ao_lado_mede_a_separacao(self):
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(indo_ao_norte(2)))
        passo = 80.0 / 3.6 * 6
        por_limite = A.aproximacao_minima(v, [radar(40.0, passo / 2, 60)])
        self.assertAlmostEqual(por_limite[60][0], 40.0, delta=0.5)

    def test_antes_da_primeira_amostra_vale_a_ponta(self):
        # Fora do segmento a distancia e ate a ponta, nao ate a reta.
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(indo_ao_norte(2)))
        por_limite = A.aproximacao_minima(v, [radar(30.0, -40.0, 60)])
        self.assertAlmostEqual(por_limite[60][0], 50.0, delta=0.5)

    def test_lacuna_no_tempo_nao_vira_trajetoria(self):
        # Dez minutos sem linha (tunel, aparelho desligado): ligar as duas
        # pontas por uma reta inventaria um caminho que ninguem percorreu.
        linhas = (indo_ao_norte(1)
                  + indo_ao_norte(1, inicio_s=600, norte0_m=400.0))
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(linhas))
        por_limite = A.aproximacao_minima(v, [radar(0.0, 200.0, 60)])
        self.assertAlmostEqual(por_limite[60][0], 200.0, delta=0.5)

    def test_um_valor_por_radar(self):
        # Cada radar entra uma vez, com a SUA menor distancia, por mais
        # amostras que o tenham visto.
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(indo_ao_norte(12)))
        por_limite = A.aproximacao_minima(
            v, [radar(0.0, 300.0, 60), radar(40.0, 500.0, 60),
                radar(0.0, 400.0, 80)])
        self.assertEqual(len(por_limite[60]), 2)
        self.assertEqual(len(por_limite[80]), 1)


class Analisa(unittest.TestCase):
    """A perpendicular, que e o que o firmware pode calcular ANTES do radar."""

    def test_veiculo_parado_nao_entra(self):
        # Parado, o receptor nao sabe a direcao: a RMC vem sem rumo e a
        # base longa mede o ruido do GPS, nao o caminho. A perpendicular
        # calculada sobre esse rumo e numero aleatorio com cara de medida.
        parou_em = 11 * 80.0 / 3.6 * 6           # onde a ultima amostra ficou
        linhas = (indo_ao_norte(12)
                  + [linha_v3(72 + 6 * k, 0.0, parou_em, 0.0, None)
                     for k in range(10)])
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(linhas))
        achados = A.analisa(v, [radar(0.0, 1200.0, 60)])
        self.assertTrue(achados, "o trecho em movimento devia ter achados")
        self.assertTrue(all(a["v_kmh"] >= A.VELOCIDADE_MIN_KMH
                            for a in achados))

    def test_janela_do_rumo_e_por_tempo_e_nao_por_indice(self):
        # Sem rumo registrado (v2, ou RMC sem rumo), a base longa usa as
        # amostras a ate JANELA_RUMO_S de distancia. Por indice, as cinco
        # anteriores a uma lacuna de dez minutos estariam la atras, numa
        # rua que o carro ja deixou -- e o rumo sairia dessa reta.
        antes = [linha_v3(6 * k, -1000.0 + 50.0 * k, 0.0, 30.0, None)
                 for k in range(6)]              # indo ao LESTE
        depois = indo_ao_norte(11, kmh=30.0, inicio_s=600, rumo=None)
        v = A.interpreta_viagem(CABECALHO_V3 + "".join(antes + depois))
        # A terceira depois da lacuna: por indice, a base comecaria tres
        # amostras ANTES da lacuna e o rumo sairia a ~68 graus.
        i = len(antes) + 2
        rumo = A.rumo_suavizado(v, i)
        self.assertIsNotNone(rumo)
        self.assertAlmostEqual(rumo, 0.0, delta=0.5)
        # E a primeira depois da lacuna nao tem base atras: sem rumo.
        self.assertIsNone(A.rumo_suavizado(v, len(antes)))

    def test_rumo_registrado_vence_a_estimativa(self):
        v = A.interpreta_viagem(CABECALHO_V3
                                + "".join(indo_ao_norte(11, rumo=7.0)))
        self.assertEqual(A.rumo_suavizado(v, 5), 7.0)

    def test_a_estatistica_e_por_radar_e_nao_por_amostra(self):
        # Um radar visto por muitas amostras pesava muitas vezes mais que um
        # visto por uma. Lento perto de um e rapido perto do outro, a
        # mediana dizia mais sobre a velocidade que sobre a pista.
        achados = ([{"radar": 0, "limite": 60, "perp_m": 40.0,
                     "dist_m": 100.0, "a_frente": True}] * 9
                   + [{"radar": 1, "limite": 60, "perp_m": 0.0,
                       "dist_m": 80.0, "a_frente": True}])
        por_limite = A.perpendicular_por_radar(achados)
        self.assertEqual(sorted(por_limite[60]), [0.0, 40.0])

    def test_radar_atras_nao_entra_na_estatistica(self):
        achados = [{"radar": 0, "limite": 60, "perp_m": 40.0,
                    "dist_m": 100.0, "a_frente": False}]
        self.assertEqual(A.perpendicular_por_radar(achados), {})


if __name__ == "__main__":
    unittest.main()
