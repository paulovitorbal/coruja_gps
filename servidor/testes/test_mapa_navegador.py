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

        pagina_html = Path(cls._tmp.name) / "mapa.html"
        pagina_html.write_text(viagem.desenha(v))

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


if __name__ == "__main__":
    unittest.main()
