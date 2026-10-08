"""Abre o mapa num navegador de verdade e confere a reprodução.

**Opcional.** Pulado quando o `playwright` ou o `folium` não estiverem
instalados — a suíte principal continua sendo só biblioteca padrão.

    python3 -m venv .venv
    .venv/bin/pip install folium playwright
    .venv/bin/playwright install chromium
    .venv/bin/python -m unittest discover -s servidor/testes

⚠️ Este é o único teste que verifica que os botões de velocidade **fazem
alguma coisa**. Conferir o HTML gerado por substring diria que `setTransitionTime`
está lá; não diria que ele é chamado, nem com que valor, nem que o reprodutor
existe para recebê-lo. Na primeira versão deste código eu procurava o
reprodutor numa variável global que não existe, e o HTML passava em todas as
conferências de texto.
"""

import datetime
import math
import sys
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))


def _falta(modulo: str) -> bool:
    try:
        __import__(modulo)
        return False
    except ImportError:
        return True


@unittest.skipIf(_falta("folium"), "folium nao instalado (teste opcional)")
@unittest.skipIf(_falta("playwright"), "playwright nao instalado (teste opcional)")
class MapaNoNavegador(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import tempfile

        import viagem
        from test_pagina_viagens import viagem_sintetica
        from playwright.sync_api import sync_playwright

        cls.viagem_mod = viagem
        cls._tmp = tempfile.TemporaryDirectory()
        arq = Path(cls._tmp.name) / "20261006_123858.log"
        arq.write_text(viagem_sintetica(pontos=22))
        v = viagem.le_viagem(arq)

        # Radares SOBRE a rota sintetica, para a camada ser exercitada de
        # verdade. Um deles e semaforo (limite 0), que tem pino diferente.
        radares = [
            viagem.RadarNoMapa(lat=v.pontos[3].lat, lon=v.pontos[3].lon,
                               limite=60),
            viagem.RadarNoMapa(lat=v.pontos[10].lat, lon=v.pontos[10].lon,
                               limite=80),
            viagem.RadarNoMapa(lat=v.pontos[16].lat, lon=v.pontos[16].lon,
                               limite=0),
        ]
        cls.radares = radares

        pagina_html = Path(cls._tmp.name) / "mapa.html"
        pagina_html.write_text(viagem.desenha(v, "", radares))

        try:
            cls._pw = sync_playwright().start()
            cls.nav = cls._pw.chromium.launch()
        except Exception as e:                       # noqa: BLE001
            raise unittest.SkipTest(f"sem navegador: {e}") from e

        cls.erros = []
        cls.pag = cls.nav.new_page()
        cls.pag.on("pageerror", lambda e: cls.erros.append(str(e)))
        cls.pag.on("console", lambda m: cls.erros.append(m.text)
                   if m.type == "error" else None)
        cls.pag.goto(f"file://{pagina_html}")
        cls.pag.wait_for_timeout(2500)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "nav"):
            cls.nav.close()
            cls._pw.stop()
            cls._tmp.cleanup()

    def test_a_pagina_carrega_sem_erro_de_javascript(self):
        # Erros de rede ficam de fora: o navegador do teste não alcança os
        # servidores de tile, e isso não é defeito da página.
        reais = [e for e in self.erros
                 if "ERR_" not in e and "net::" not in e
                 and "tile" not in e.lower()]
        self.assertEqual(reais, [])

    def test_os_cinco_botoes_existem(self):
        botoes = self.pag.locator("#cg-vel button")
        rotulos = [botoes.nth(i).inner_text() for i in range(botoes.count())]
        self.assertEqual(rotulos, ["1x", "2x", "4x", "8x", "16x"])

    def test_o_reprodutor_esta_onde_o_codigo_procura(self):
        # `window.timeDimensionControl._player` é acesso a membro privado de
        # biblioteca de terceiro. Se uma versão nova do folium ou do
        # leaflet-timedimension mudar o caminho, os botões param de funcionar
        # EM SILÊNCIO -- e é este teste que grita.
        self.assertTrue(self.pag.evaluate(
            "() => !!(window.timeDimensionControl && "
            "window.timeDimensionControl._player)"))

    def test_cada_botao_poe_a_velocidade_certa(self):
        from viagem import PASSO_S, VELOCIDADES

        botoes = self.pag.locator("#cg-vel button")
        for i, v in enumerate(VELOCIDADES):
            botoes.nth(i).click()
            self.pag.wait_for_timeout(100)
            tt = self.pag.evaluate(
                "() => window.timeDimensionControl._player.getTransitionTime()")
            # `Math.round` do JS arredonda meio PARA CIMA; o `round` do Python
            # arredonda meio para o par. Em 16x o passo dá 312,5 e as duas
            # linguagens discordam -- a conta de referência tem de ser a do
            # lado que executa.
            esperado = math.floor(PASSO_S * 1000 / v + 0.5)
            self.assertEqual(tt, esperado, f"{v}x")

    def test_o_botao_escolhido_fica_marcado(self):
        botoes = self.pag.locator("#cg-vel button")
        botoes.nth(2).click()      # 4x
        self.pag.wait_for_timeout(100)
        marcados = [botoes.nth(i).get_attribute("aria-pressed")
                    for i in range(botoes.count())]
        self.assertEqual(marcados, ["false", "false", "true", "false", "false"])

    def test_1x_e_tempo_real(self):
        # A decisão do autor, afirmada em número: um passo de 5 s de viagem
        # leva 5000 ms de relógio.
        from viagem import PASSO_S

        self.pag.locator("#cg-vel button").first.click()
        self.pag.wait_for_timeout(100)
        self.assertEqual(
            self.pag.evaluate(
                "() => window.timeDimensionControl._player.getTransitionTime()"),
            PASSO_S * 1000)

    def test_dar_play_faz_o_tempo_andar(self):
        self.pag.evaluate(
            "() => window.timeDimensionControl._player.setTransitionTime(40)")
        antes = self.pag.evaluate(
            "() => window.timeDimensionControl._timeDimension"
            ".getCurrentTimeIndex()")
        self.pag.evaluate("() => window.timeDimensionControl._player.start()")
        self.pag.wait_for_timeout(1500)
        depois = self.pag.evaluate(
            "() => window.timeDimensionControl._timeDimension"
            ".getCurrentTimeIndex()")
        self.pag.evaluate("() => window.timeDimensionControl._player.stop()")
        self.assertGreater(depois, antes)

    def test_o_marcador_anda_ENTRE_os_pontos_de_minuto(self):
        # A interpolação é o motivo de a trilha ir como LineString. Sem ela o
        # marcador ficaria um minuto parado e depois saltaria -- o que parece
        # travamento.
        def posicao():
            return self.pag.evaluate("""() => {
                var p = null;
                window.timeDimensionControl._map.eachLayer(function (l) {
                    if (l instanceof L.CircleMarker) { p = l.getLatLng(); }
                });
                return p ? [p.lat, p.lng] : null;
            }""")

        self.pag.evaluate(
            "() => window.timeDimensionControl._timeDimension"
            ".setCurrentTimeIndex(1)")
        self.pag.wait_for_timeout(250)
        a = posicao()
        # Indice 2 e meio minuto depois: com passo de 5 s, ainda esta DENTRO
        # do mesmo trecho entre dois registros.
        self.pag.evaluate(
            "() => window.timeDimensionControl._timeDimension"
            ".setCurrentTimeIndex(2)")
        self.pag.wait_for_timeout(250)
        b = posicao()
        self.assertIsNotNone(a)
        self.assertIsNotNone(b)
        self.assertNotEqual(a, b, "o marcador nao se moveu entre dois passos")

    def test_o_painel_mostra_os_numeros(self):
        texto = self.pag.locator(".cg-painel").inner_text()
        for esperado in ("distancia", "duracao", "media", "maxima", "pontos"):
            self.assertIn(esperado, texto)

    def test_o_nome_do_arquivo_aparece_como_esta_no_cartao(self):
        # Sem maiusculizar: nome de arquivo é identificador, e maiusculizar
        # faz ele deixar de bater com o que está no cartão.
        self.assertIn("20261006_123858.log",
                      self.pag.locator(".cg-painel").inner_text())

    # --- os radares no mapa ---

    def test_os_pinos_de_radar_aparecem_no_mapa(self):
        # Conferir o HTML por substring diria que o `div` esta la. So o
        # navegador diz que o Leaflet o POS no mapa, com tamanho e posicao.
        pinos = self.pag.locator("div.leaflet-marker-icon div")
        self.assertGreaterEqual(pinos.count(), 3)

    def test_o_pino_mostra_o_limite_e_o_semaforo_mostra_S(self):
        textos = self.pag.locator("div.leaflet-marker-icon div").all_text_contents()
        self.assertIn("60", textos)
        self.assertIn("80", textos)
        self.assertIn("S", textos)
        self.assertNotIn("0", textos)

    def test_o_pino_e_redondo_de_verdade(self):
        # `border-radius:50%` no HTML nao prova que o navegador aplicou. Mede
        # a caixa: um circulo tem largura igual a altura.
        caixa = self.pag.locator("div.leaflet-marker-icon div").first.bounding_box()
        self.assertIsNotNone(caixa)
        self.assertAlmostEqual(caixa["width"], caixa["height"], delta=1.0)
        self.assertGreater(caixa["width"], 20)

    # --- a velocidade do ponto exibido ---

    def test_o_indicador_de_velocidade_existe_ao_lado_do_relogio(self):
        self.assertEqual(self.pag.locator(".cg-vel-atual").count(), 1)

    def test_o_indicador_mostra_um_numero_e_nao_o_traco(self):
        # O traco e o estado "nao sei". Se ele persistir, o indice por
        # milissegundo nao casou com o relogio do TimeDimension -- que foi
        # exatamente o risco de indexar por texto de data.
        texto = self.pag.locator(".cg-vel-atual").inner_text()
        self.assertIn("km/h", texto, f"indicador em {texto!r}")

    def test_o_numero_MUDA_ao_longo_da_linha_do_tempo(self):
        """A prova de que ele acompanha, e nao mostra um valor congelado.

        ⚠️ **Duas armadilhas, e cai nas duas quem amostra pouco.**

        1. A velocidade NAO e interpolada: todos os pontos adensados de um
           mesmo registro carregam a velocidade dele. Na viagem sintetica, de
           60 s por registro, os trinta primeiros passos mostram o mesmo
           numero -- avancar dois passos nao prova nada.
        2. A viagem sintetica usa `20 + i % 7`, entao o PRIMEIRO e o ULTIMO
           ponto tem a mesma velocidade (0 % 7 == 21 % 7). Comparar so as
           pontas acusa de congelado um indicador que funciona.

        As duas me deram diagnostico errado antes deste teste ficar assim. Por
        isso ele varre a linha do tempo inteira e exige VARIEDADE, em vez de
        comparar dois instantes escolhidos a dedo.
        """
        vistos = set()
        for fracao in (0.0, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9):
            self.pag.evaluate("""(f) => {
              var td = window.timeDimensionControl._timeDimension;
              var ts = td.getAvailableTimes();
              td.setCurrentTime(ts[Math.floor(f * (ts.length - 1))]);
            }""", fracao)
            self.pag.wait_for_timeout(120)
            vistos.add(self.pag.locator(".cg-vel-atual").inner_text())

        self.assertTrue(all("km/h" in t for t in vistos), vistos)
        self.assertGreater(len(vistos), 1,
                           f"o indicador ficou preso em {vistos}")

if __name__ == "__main__":
    unittest.main()
