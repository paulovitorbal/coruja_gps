"""Testes do servidor de referência (regra 1).

    python3 -m unittest discover -s servidor/testes

Sobe o servidor de verdade numa porta efêmera e fala HTTP com ele. Testar o
manipulador isolado deixaria de fora o que mais importa aqui: os cabeçalhos,
os códigos de status e o comportamento do HEAD — que é o que o firmware
realmente consome.
"""

import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import servidor as s  # noqa: E402


def base_valida(n_pontos: int = 3, crc: int = 0xDEADBEEF) -> bytes:
    """Um radares.bin mínimo, só com cabeçalho e registros zerados."""
    cab = s.CABECALHO.pack(s.MAGIC, 1, 5, 12, n_pontos, crc)
    return cab + bytes(n_pontos * 12)


class ServidorEmTeste(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dados = Path(self.tmp.name)
        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1")
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.t.join(timeout=5)
        self.tmp.cleanup()

    def url(self, rota):
        return f"http://127.0.0.1:{self.porta}{rota}"

    def get(self, rota, metodo="GET"):
        req = urllib.request.Request(self.url(rota), method=metodo)
        return urllib.request.urlopen(req, timeout=5)

    def status_de(self, rota, metodo="GET"):
        try:
            return self.get(rota, metodo).status
        except urllib.error.HTTPError as e:
            return e.code

    def publica(self, conteudo=None):
        (self.dados / s.NOME_BASE).write_bytes(
            conteudo if conteudo is not None else base_valida())

    # --- sem base publicada ---

    def test_sem_arquivo_responde_503_e_nao_404(self):
        # 503 e não 404: a rota existe, o dado é que não foi publicado. A
        # distinção poupa tempo de quem depura.
        self.assertEqual(self.status_de(s.ROTA_VERSAO), 503)
        self.assertEqual(self.status_de(s.ROTA_BASE), 503)

    # --- rotas ---

    def test_rota_desconhecida_e_404(self):
        self.assertEqual(self.status_de("/qualquer"), 404)
        self.assertEqual(self.status_de("/"), 404)

    def test_nao_ha_listagem_de_diretorio(self):
        # Herdar de SimpleHTTPRequestHandler exporia o diretório inteiro.
        (self.dados / "segredo.txt").write_text("nao deveria sair daqui")
        self.assertEqual(self.status_de("/segredo.txt"), 404)

    def test_travessia_de_caminho_nao_alcanca_nada(self):
        for tentativa in ("/../servidor.py", "/%2e%2e/servidor.py",
                          "/radares.bin/../../servidor.py"):
            self.assertEqual(self.status_de(tentativa), 404, tentativa)

    def test_query_string_e_ignorada(self):
        self.publica()
        self.assertEqual(self.status_de(s.ROTA_VERSAO + "?v=2"), 200)

    # --- com base publicada ---

    def test_versao_vem_do_crc_do_proprio_arquivo(self):
        # Derivar do arquivo evita a falha clássica de publicar dado novo com
        # versão velha, que o firmware então ignora.
        self.publica(base_valida(n_pontos=18294, crc=0x1A2B3C4D))
        corpo = self.get(s.ROTA_VERSAO).read().decode().strip()
        self.assertIn("crc32:1a2b3c4d", corpo)
        self.assertIn("pontos:18294", corpo)

    def test_a_versao_e_uma_unica_linha(self):
        # O firmware guarda e compara como texto; mais de uma linha
        # complicaria a leitura sem ganho nenhum.
        self.publica()
        self.assertEqual(len(self.get(s.ROTA_VERSAO).read().decode().strip()
                             .splitlines()), 1)

    def test_a_versao_muda_quando_os_dados_mudam(self):
        self.publica(base_valida(crc=0x11111111))
        antes = self.get(s.ROTA_VERSAO).read()
        self.publica(base_valida(crc=0x22222222))
        self.assertNotEqual(antes, self.get(s.ROTA_VERSAO).read())

    def test_a_versao_nao_muda_se_os_dados_forem_os_mesmos(self):
        self.publica()
        a = self.get(s.ROTA_VERSAO).read()
        self.publica()
        self.assertEqual(a, self.get(s.ROTA_VERSAO).read())

    def test_base_vem_byte_a_byte(self):
        conteudo = base_valida(n_pontos=7)
        self.publica(conteudo)
        self.assertEqual(self.get(s.ROTA_BASE).read(), conteudo)

    def test_content_length_presente_nas_duas_rotas(self):
        # O RF05.2 aborta o download se o Content-Length faltar.
        self.publica()
        for rota in (s.ROTA_VERSAO, s.ROTA_BASE):
            r = self.get(rota)
            self.assertIsNotNone(r.headers["Content-Length"], rota)
            self.assertEqual(int(r.headers["Content-Length"]),
                             len(r.read()), rota)

    def test_head_traz_tamanho_sem_corpo(self):
        # Permite conferir disponibilidade sem baixar 214 KB.
        self.publica(base_valida(n_pontos=100))
        r = self.get(s.ROTA_BASE, metodo="HEAD")
        self.assertEqual(r.status, 200)
        self.assertEqual(r.read(), b"")
        self.assertEqual(int(r.headers["Content-Length"]),
                         s.CABECALHO.size + 100 * 12)

    def test_tipo_de_conteudo_correto(self):
        self.publica()
        self.assertIn("text/plain",
                      self.get(s.ROTA_VERSAO).headers["Content-Type"])
        self.assertEqual("application/octet-stream",
                         self.get(s.ROTA_BASE).headers["Content-Type"])

    def test_sem_cache(self):
        # Um proxy guardando a base velha faria o aparelho nunca atualizar.
        self.publica()
        self.assertIn("no-store", self.get(s.ROTA_BASE).headers["Cache-Control"])

    # --- arquivo corrompido ---

    def test_arquivo_sem_cabecalho_valido_ainda_da_versao_estavel(self):
        # Serve versão derivada de hash e registra aviso: melhor que 500, e o
        # firmware ao menos detecta mudança.
        self.publica(b"isto nao e um radares.bin")
        corpo = self.get(s.ROTA_VERSAO).read().decode()
        self.assertIn("sha256:", corpo)

    def test_arquivo_curto_demais_nao_quebra_o_servidor(self):
        self.publica(b"RDR1")
        self.assertEqual(self.status_de(s.ROTA_VERSAO), 200)


class VersaoDe(unittest.TestCase):
    def test_arquivo_ausente_devolve_none(self):
        self.assertIsNone(s.versao_de(Path("/nao/existe/radares.bin")))


if __name__ == "__main__":
    unittest.main()
