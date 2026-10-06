"""Testes da página de consulta de viagens.

    python3 -m unittest discover -s servidor/testes

O que está em jogo: os registros dizem **onde o carro esteve e quando**. Uma
sessão que vaze, ou um nome de arquivo que escape da pasta, entrega isso a
quem não deveria ter.

⚠️ As coordenadas usadas aqui são **sintéticas**. Os arquivos reais do cartão
começam em casa — pôr um deles num repositório público publicaria o endereço
de quem usa o aparelho.
"""

import datetime
import http.client
import re
import sys
import tempfile
import threading
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import servidor as s  # noqa: E402
import viagem  # noqa: E402

TOKEN = "segredo-do-carro-para-teste"
TOKEN_OUTRO = "segredo-da-bancada-para-teste"
APARELHOS = {TOKEN: "carro", TOKEN_OUTRO: "bancada"}


def viagem_sintetica(inicio="2026-10-06T12:38:00Z", pontos=22,
                     lat0=-10.0, lon0=-40.0, passo_s=60, versao=1) -> str:
    """Uma viagem crível, longe de qualquer lugar onde alguém more.

    ⚠️ As coordenadas são **sintéticas**. Os arquivos reais do cartão começam
    em casa — pôr um deles num repositório público publicaria o endereço de
    quem usa o aparelho.

    `passo_s=60` é o formato v1, de uma linha por minuto; `passo_s=6` é a v2.
    As duas existem no mundo: um aparelho não atualizado continua gravando
    v1, e as viagens que ele já mandou continuam no servidor.
    """
    t0 = datetime.datetime.strptime(inicio, "%Y-%m-%dT%H:%M:%SZ")
    linhas = [f"# coruja_gps viagem v{versao}", "utc;lat;lon;v_media;dist_km"]
    for i in range(pontos):
        q = t0 + datetime.timedelta(seconds=i * passo_s)
        linhas.append(
            f"{q.strftime('%Y-%m-%dT%H:%M:%SZ')};"
            f"{lat0 + i * 0.001:.5f};{lon0 + i * 0.001:.5f};"
            f"{20 + i % 7}.0;{i * 0.11:.2f}")
    return "\n".join(linhas) + "\n"


class PaginaEmTeste(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dados = Path(self.tmp.name)
        self.recebidos = self.dados / "recebidos"
        (self.recebidos / "carro").mkdir(parents=True)
        (self.recebidos / "bancada").mkdir(parents=True)

        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1", APARELHOS,
                                   self.recebidos)
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()
        self.addCleanup(self.t.join, 5)
        self.addCleanup(self.srv.server_close)
        self.addCleanup(self.srv.shutdown)

    # -- auxiliares --

    def poe_viagem(self, aparelho, nome, texto=None):
        alvo = self.recebidos / aparelho / nome
        alvo.write_text(texto if texto is not None else viagem_sintetica())
        return alvo

    def pede(self, metodo, rota, corpo=None, cookie=None):
        cab = {"Connection": "close"}
        if cookie:
            cab["Cookie"] = cookie
        if corpo is not None:
            cab["Content-Type"] = "application/x-www-form-urlencoded"
            cab["Content-Length"] = str(len(corpo))
        c = http.client.HTTPConnection("127.0.0.1", self.porta, timeout=10)
        try:
            c.request(metodo, rota, body=corpo, headers=cab)
            r = c.getresponse()
            return r.status, r.read().decode("utf-8", "replace"), dict(
                r.getheaders())
        finally:
            c.close()

    def entra(self, token=TOKEN):
        st, _corpo, cab = self.pede(
            "POST", s.ROTA_VIAGENS, f"token={token}")
        bruto = cab.get("Set-Cookie", "")
        m = re.search(rf"{s.COOKIE_SESSAO}=([^;]+)", bruto)
        return st, (f"{s.COOKIE_SESSAO}={m.group(1)}" if m else "")

    # ===================================================== sem sessão

    def test_sem_sessao_mostra_o_formulario(self):
        st, corpo, _ = self.pede("GET", s.ROTA_VIAGENS)
        self.assertEqual(st, 200)
        self.assertIn('name="token"', corpo)
        self.assertIn('type="password"', corpo)

    def test_token_errado_nao_abre_sessao(self):
        st, cookie = self.entra("nao-esta-na-lista")
        self.assertEqual(st, 401)
        self.assertEqual(cookie, "")

    def test_token_vazio_nao_abre_sessao(self):
        st, cookie = self.entra("")
        self.assertEqual(st, 401)
        self.assertEqual(cookie, "")

    def test_mapa_sem_sessao_e_recusado(self):
        self.poe_viagem("carro", "20261006_123858.log")
        st, _corpo, _ = self.pede("GET", s.ROTA_VIAGENS + "/20261006_123858.log")
        self.assertEqual(st, 401)

    # ===================================================== com sessão

    def test_entrar_devolve_cookie_de_sessao(self):
        st, cookie = self.entra()
        self.assertEqual(st, 200)
        self.assertTrue(cookie)

    def test_o_cookie_NAO_contem_o_token(self):
        # O ponto inteiro de usar identificador aleatório: o token também
        # SERVE PARA ENVIAR, e um computador compartilhado passaria a poder
        # subir viagem falsa.
        _st, cookie = self.entra()
        self.assertNotIn(TOKEN, cookie)

    def test_o_cookie_e_httponly_e_samesite(self):
        _st, _corpo, cab = self.pede("POST", s.ROTA_VIAGENS, f"token={TOKEN}")
        bruto = cab.get("Set-Cookie", "")
        self.assertIn("HttpOnly", bruto)
        self.assertIn("SameSite=Strict", bruto)
        self.assertIn("Path=/viagens", bruto)

    def test_lista_as_viagens_do_aparelho(self):
        self.poe_viagem("carro", "20261006_123858.log")
        self.poe_viagem("carro", "20261004_225323.log")
        _st, cookie = self.entra()
        st, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertEqual(st, 200)
        self.assertIn("20261006_123858.log", corpo)
        self.assertIn("20261004_225323.log", corpo)
        self.assertIn("carro", corpo)

    def test_NAO_lista_as_viagens_de_outro_aparelho(self):
        # O que a separação por pasta existe para garantir, visto de fora.
        self.poe_viagem("bancada", "20261006_999999.log")
        _st, cookie = self.entra(TOKEN)
        _st2, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertNotIn("20261006_999999.log", corpo)

    def test_nao_abre_o_mapa_de_viagem_de_outro(self):
        self.poe_viagem("bancada", "20261006_999999.log")
        _st, cookie = self.entra(TOKEN)
        st, _corpo, _ = self.pede(
            "GET", s.ROTA_VIAGENS + "/20261006_999999.log", cookie=cookie)
        self.assertEqual(st, 404)

    def test_a_listagem_mostra_os_numeros_da_viagem(self):
        self.poe_viagem("carro", "20261006_123858.log")
        _st, cookie = self.entra()
        _st2, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertIn("km", corpo)
        self.assertIn("min", corpo)

    def test_sem_viagens_a_pagina_explica_em_vez_de_vir_vazia(self):
        _st, cookie = self.entra()
        _st2, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertIn("enviar dados", corpo)

    def test_sair_derruba_a_sessao(self):
        self.poe_viagem("carro", "20261006_123858.log")
        _st, cookie = self.entra()
        self.pede("POST", s.ROTA_VIAGENS + "/sair", "", cookie=cookie)
        st, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertIn('name="token"', corpo, "voltou ao formulario")

    def test_cookie_inventado_nao_vale(self):
        st, corpo, _ = self.pede(
            "GET", s.ROTA_VIAGENS,
            cookie=f"{s.COOKIE_SESSAO}=inventado-na-mao")
        self.assertIn('name="token"', corpo)

    # ===================================================== nomes e caminhos

    def test_travessia_de_caminho_e_recusada(self):
        # O nome vira caminho em disco. A lista branca é a defesa, igual à do
        # envio.
        _st, cookie = self.entra()
        for nome in ("../../etc/passwd", "..%2f..%2fservidor.py",
                     "coruja.cfg", "coruja.log", "infracoes.log",
                     "radares.bin", "20261006_123858.log.bak"):
            st, _corpo, _ = self.pede(
                "GET", f"{s.ROTA_VIAGENS}/{nome}", cookie=cookie)
            self.assertEqual(st, 404, nome)

    def test_coruja_log_e_infracoes_nao_sao_viagens(self):
        # Eles chegam na mesma pasta, pelo envio, e não são trajetos.
        (self.recebidos / "carro" / "coruja.log").write_text("qualquer coisa")
        (self.recebidos / "carro" / "infracoes.log").write_text("outra")
        _st, cookie = self.entra()
        _st2, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertNotIn("coruja.log", corpo)
        self.assertNotIn("infracoes.log", corpo)

    def test_arquivo_ilegivel_nao_derruba_a_listagem(self):
        # Cartão puxado no meio da gravação, ou envio truncado. As outras
        # viagens continuam valendo.
        self.poe_viagem("carro", "20261006_123858.log")
        (self.recebidos / "carro" / "20261005_000000.log").write_text("lixo\n")
        _st, cookie = self.entra()
        st, corpo, _ = self.pede("GET", s.ROTA_VIAGENS, cookie=cookie)
        self.assertEqual(st, 200)
        self.assertIn("20261006_123858.log", corpo)
        self.assertNotIn("20261005_000000.log", corpo)

    # ===================================================== as outras rotas

    def test_a_pagina_nao_interfere_na_rota_da_base(self):
        st, _corpo, _ = self.pede("GET", s.ROTA_VERSAO)
        self.assertEqual(st, 401, "a base continua exigindo token no cabecalho")

    def test_post_em_rota_desconhecida_e_404(self):
        st, _corpo, _ = self.pede("POST", "/qualquer", "x=1")
        self.assertEqual(st, 404)


class LeituraDeViagem(unittest.TestCase):
    """O analisador do arquivo, sem servidor no meio."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pasta = Path(self.tmp.name)

    def escreve(self, texto, nome="20261006_123858.log"):
        alvo = self.pasta / nome
        alvo.write_text(texto)
        return alvo

    def test_le_a_viagem_inteira(self):
        v = viagem.le_viagem(self.escreve(viagem_sintetica(pontos=10)))
        self.assertEqual(len(v.pontos), 10)
        self.assertEqual(v.duracao, datetime.timedelta(minutes=9))

    def test_a_media_e_do_PERCURSO_nao_das_medias(self):
        # Distância sobre tempo. A média das `v_media` de cada minuto daria
        # outro número, e o errado: cada minuto pesaria igual mesmo tendo
        # percorrido distâncias diferentes.
        texto = ("# coruja_gps viagem v1\n"
                 "utc;lat;lon;v_media;dist_km\n"
                 "2026-10-06T12:00:00Z;-10.0;-40.0;100.0;0.00\n"
                 "2026-10-06T13:00:00Z;-10.1;-40.0;1.0;50.00\n")
        v = viagem.le_viagem(self.escreve(texto))
        self.assertAlmostEqual(v.v_media_kmh, 50.0, places=1)
        # A média das médias daria 50,5 -- parecido por acaso, e errado por
        # construção. O máximo mostra que os dados são mesmo díspares.
        self.assertAlmostEqual(v.v_maxima_kmh, 100.0, places=1)

    def test_arquivo_sem_assinatura_e_recusado(self):
        with self.assertRaises(viagem.ErroDeViagem):
            viagem.le_viagem(self.escreve("lat;lon\n-10;-40\n"))

    def test_arquivo_com_um_ponto_so_nao_vira_trajeto(self):
        with self.assertRaises(viagem.ErroDeViagem):
            viagem.le_viagem(self.escreve(viagem_sintetica(pontos=1)))

    def test_linha_torta_e_pulada_sem_derrubar_o_resto(self):
        # Cartão puxado no meio de uma gravação.
        texto = viagem_sintetica(pontos=5) + "2026-10-06T12:4\n"
        v = viagem.le_viagem(self.escreve(texto))
        self.assertEqual(len(v.pontos), 5)

    def test_pontos_fora_de_ordem_sao_ordenados(self):
        # Uma retomada de viagem, ou um arquivo concatenado à mão.
        linhas = viagem_sintetica(pontos=4).splitlines()
        embaralhado = "\n".join(linhas[:2] + [linhas[4], linhas[2], linhas[3],
                                              linhas[5]]) + "\n"
        v = viagem.le_viagem(self.escreve(embaralhado))
        carimbos = [p.quando for p in v.pontos]
        self.assertEqual(carimbos, sorted(carimbos))

    def test_le_o_formato_v2_de_dez_linhas_por_minuto(self):
        # A taxa subiu em 2026-10-06. A conta que condenou a anterior: a
        # 120 km/h, um ponto por minuto deixa 2 km entre vertices -- numa via
        # expressa o tracado vira uma reta que ignora as curvas.
        v = viagem.le_viagem(self.escreve(
            viagem_sintetica(pontos=30, passo_s=6, versao=2)))
        self.assertEqual(len(v.pontos), 30)
        self.assertEqual(v.duracao, datetime.timedelta(seconds=174))

    def test_le_o_formato_v1_antigo(self):
        # Um aparelho nao atualizado continua mandando v1, e as viagens que
        # ele ja mandou continuam no servidor. Recusa-las apagaria da pagina
        # historico que existe.
        v = viagem.le_viagem(self.escreve(
            viagem_sintetica(pontos=10, passo_s=60, versao=1)))
        self.assertEqual(len(v.pontos), 10)

    def test_assinatura_de_versao_futura_e_recusada(self):
        # Melhor recusar do que desenhar um formato que ninguem conferiu.
        with self.assertRaises(viagem.ErroDeViagem):
            viagem.le_viagem(self.escreve(
                viagem_sintetica(pontos=5, versao=9)))

    def test_o_adensamento_funciona_nas_duas_taxas(self):
        # O passo de interpolacao e fixo; quem muda e o intervalo do dado.
        for passo_s, versao in ((60, 1), (6, 2)):
            with self.subTest(passo_s=passo_s):
                v = viagem.le_viagem(self.escreve(
                    viagem_sintetica(pontos=5, passo_s=passo_s,
                                     versao=versao)))
                denso = viagem._densifica(v)
                esperado = 4 * (passo_s // viagem.PASSO_S) + 1
                self.assertEqual(len(denso), esperado)

    def test_resumo_devolve_none_para_arquivo_ruim(self):
        self.assertIsNone(viagem.resumo(self.escreve("lixo\n")))

    def test_o_geojson_e_uma_LineString_adensada(self):
        # 6 registros de minuto viram 5 trechos de 60 s, a 5 s por passo:
        # 5*12 + 1 = 61 vertices.
        #
        # Este teste ja afirmou `== 6`, quando eu supunha que a biblioteca de
        # animacao interpolava. Ela nao interpola -- ela FATIA a lista de
        # coordenadas --, e o marcador ficava 60 s parado. Quem mostrou foi o
        # teste de navegador.
        v = viagem.le_viagem(self.escreve(viagem_sintetica(pontos=6)))
        g = viagem._trilha_geojson(v)
        feicao = g["features"][0]
        self.assertEqual(feicao["geometry"]["type"], "LineString")
        esperado = 5 * (60 // viagem.PASSO_S) + 1  # o sintetico padrao e v1
        self.assertEqual(len(feicao["geometry"]["coordinates"]), esperado)
        self.assertEqual(len(feicao["properties"]["times"]), esperado)

    def test_o_adensamento_comeca_e_termina_nos_pontos_REAIS(self):
        # Interpolar no meio é aceitável; mexer nos extremos não, porque eles
        # são medição.
        v = viagem.le_viagem(self.escreve(viagem_sintetica(pontos=5)))
        denso = viagem._densifica(v)
        self.assertEqual(denso[0].lat, v.pontos[0].lat)
        self.assertEqual(denso[0].lon, v.pontos[0].lon)
        self.assertEqual(denso[0].quando, v.pontos[0].quando)
        self.assertEqual(denso[-1].lat, v.pontos[-1].lat)
        self.assertEqual(denso[-1].quando, v.pontos[-1].quando)

    def test_o_adensamento_nao_anda_para_tras_no_tempo(self):
        v = viagem.le_viagem(self.escreve(viagem_sintetica(pontos=8)))
        carimbos = [p.quando for p in viagem._densifica(v)]
        self.assertEqual(carimbos, sorted(carimbos))
        self.assertEqual(len(carimbos), len(set(carimbos)), "carimbo repetido")

    def test_o_ponto_do_meio_fica_no_MEIO(self):
        # Interpolação linear: metade do caminho, metade do tempo.
        texto = ("# coruja_gps viagem v1\n"
                 "utc;lat;lon;v_media;dist_km\n"
                 "2026-10-06T12:00:00Z;-10.0;-40.0;30.0;0.00\n"
                 "2026-10-06T12:01:00Z;-10.2;-40.4;30.0;1.00\n")
        denso = viagem._densifica(viagem.le_viagem(self.escreve(texto)))
        meio = denso[len(denso) // 2]
        self.assertAlmostEqual(meio.lat, -10.1, places=3)
        self.assertAlmostEqual(meio.lon, -40.2, places=3)

    def test_velocidade_e_distancia_NAO_sao_interpoladas(self):
        # Posição interpolada é uma animação legível. Velocidade interpolada
        # seria um número que ninguém mediu, com cara de medição.
        texto = ("# coruja_gps viagem v1\n"
                 "utc;lat;lon;v_media;dist_km\n"
                 "2026-10-06T12:00:00Z;-10.0;-40.0;20.0;0.00\n"
                 "2026-10-06T12:01:00Z;-10.1;-40.1;80.0;1.00\n")
        denso = viagem._densifica(viagem.le_viagem(self.escreve(texto)))
        velocidades = {p.v_media_kmh for p in denso}
        self.assertEqual(velocidades, {20.0, 80.0},
                         "apareceu velocidade que ninguem mediu")

    def test_o_geojson_usa_lon_lat_e_nao_lat_lon(self):
        # A ordem do GeoJSON é a inversa da do Leaflet, e trocar põe a viagem
        # do outro lado do planeta sem erro nenhum.
        v = viagem.le_viagem(self.escreve(
            viagem_sintetica(pontos=3, lat0=-10.0, lon0=-40.0)))
        primeiro = viagem._trilha_geojson(v)["features"][0]["geometry"][
            "coordinates"][0]
        self.assertAlmostEqual(primeiro[0], -40.0, places=4)
        self.assertAlmostEqual(primeiro[1], -10.0, places=4)


if __name__ == "__main__":
    unittest.main()
