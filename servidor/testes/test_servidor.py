"""Testes do servidor de referência (regra 1).

    python3 -m unittest discover -s servidor/testes

Sobe o servidor de verdade numa porta efêmera e fala HTTP com ele. Testar o
manipulador isolado deixaria de fora o que mais importa aqui: os cabeçalhos,
os códigos de status e o comportamento do HEAD — que é o que o firmware
realmente consome.
"""

import http.client
import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zlib
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

import servidor as s  # noqa: E402


def base_valida(n_pontos: int = 3, crc: int = 0xDEADBEEF, versao: int = 2,
                data=(2026, 9, 30)) -> bytes:
    """Um radares.bin mínimo, só com cabeçalho e registros zerados.

    `versao=1` monta o formato ANTIGO, de 16 bytes e sem data, que o servidor
    continua tendo de servir: um arquivo v1 parado numa pasta de dados não
    pode derrubar a rota de versão.
    """
    cab = s.CABECALHO_BASE.pack(s.MAGIC, versao, 5, 12, n_pontos, crc)
    if versao != s.VERSAO_SEM_DATA:
        cab += struct.pack("<HBB", *data)
    return cab + bytes(n_pontos * 12)


TOKEN = "segredo-de-teste-do-carro"
TOKEN_OUTRO = "segredo-de-teste-da-bancada"

#: A pasta de cada aparelho é a IMPRESSÃO do token, derivada. Os testes a
#: calculam em vez de fixar a string: fixar faria um teste de hash disfarçado
#: de teste de servidor, e mudar `DIGITOS_IMPRESSAO` quebraria tudo por um
#: motivo que não é o que se está verificando.
PASTA = s.impressao_do_token(TOKEN)
PASTA_OUTRO = s.impressao_do_token(TOKEN_OUTRO)
APARELHOS = {TOKEN: PASTA, TOKEN_OUTRO: PASTA_OUTRO}


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
        # Compara com o tamanho REAL do arquivo, e não com uma conta refeita
        # aqui: o cabeçalho mudou de tamanho uma vez e vai mudar de novo.
        conteudo = base_valida(n_pontos=100)
        self.publica(conteudo)
        r = self.get(s.ROTA_BASE, metodo="HEAD")
        self.assertEqual(r.status, 200)
        self.assertEqual(r.read(), b"")
        self.assertEqual(int(r.headers["Content-Length"]), len(conteudo))

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


class VersaoComData(unittest.TestCase):
    """A linha de versão é o que uma pessoa lê ao depurar uma atualização.

    O firmware a compara como texto opaco e não interpreta campo nenhum, então
    o que está em jogo aqui é legibilidade — e o risco é o oposto do óbvio:
    inventar uma data para um arquivo v1, que não tem nenhuma.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.caminho = Path(self.tmp.name) / s.NOME_BASE

    def tearDown(self):
        self.tmp.cleanup()

    def versao(self, conteudo):
        self.caminho.write_bytes(conteudo)
        return s.versao_de(self.caminho)

    def test_v2_anuncia_a_data_da_base(self):
        linha = self.versao(base_valida(versao=2, data=(2026, 9, 30)))
        self.assertIn("formato:2", linha)
        self.assertIn("data:2026-09-30", linha)

    def test_v1_nao_finge_ter_data(self):
        linha = self.versao(base_valida(versao=1))
        self.assertIn("formato:1", linha)
        self.assertNotIn("data:", linha)

    def test_v2_truncada_antes_da_data_nao_inventa_uma(self):
        # 18 bytes: o ano chegou, o dia não. Servir "data:2026-09-00" seria
        # pior do que não servir data nenhuma.
        linha = self.versao(base_valida(versao=2)[:18])
        self.assertNotIn("data:", linha)

    def test_a_data_entra_na_comparacao_de_mudanca(self):
        # Mesma base, data diferente: é uma publicação nova e o aparelho
        # precisa enxergar isso, senão uma recarga não chega nunca.
        a = self.versao(base_valida(data=(2026, 9, 29)))
        b = self.versao(base_valida(data=(2026, 9, 30)))
        self.assertNotEqual(a, b)


class Recepcao(unittest.TestCase):
    """A rota que recebe log, viagem e infração do aparelho.

    O contrato que o firmware consome é mínimo e todo ele está aqui:

        PUT  /envio/<nome>  + X-Coruja-Token  -> 201 e o CRC32 do que chegou
        GET  /envio/<nome>  + X-Coruja-Token  -> 200 e o CRC32 do que está salvo

    O aparelho envia, pergunta o CRC e só então apaga o arquivo do cartão.
    Por isso o CRC é o que mais importa testar: ele é a autorização para
    destruir o único exemplar do dado.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dados = Path(self.tmp.name)
        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1", APARELHOS)
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.t.join(timeout=5)
        self.tmp.cleanup()

    # -- auxiliares --

    def conexao(self):
        return http.client.HTTPConnection("127.0.0.1", self.porta, timeout=5)

    def pede(self, metodo, rota, corpo=None, token=TOKEN, tamanho=None):
        """Devolve (status, corpo). `tamanho` mente no Content-Length."""
        cab = {"Connection": "close"}
        if token is not None:
            cab[s.CABECALHO_TOKEN] = token
        if corpo is not None:
            cab["Content-Length"] = str(len(corpo) if tamanho is None
                                        else tamanho)
        c = self.conexao()
        try:
            c.putrequest(metodo, rota, skip_accept_encoding=True)
            for k, v in cab.items():
                c.putheader(k, v)
            c.endheaders()
            if corpo:
                c.send(corpo)
            r = c.getresponse()
            return r.status, r.read()
        finally:
            c.close()

    def envia(self, nome, corpo, **kw):
        return self.pede("PUT", s.ROTA_ENVIO + nome, corpo, **kw)

    def recebidos(self, aparelho=None):
        aparelho = aparelho if aparelho is not None else PASTA
        pasta = self.dados / s.SUBDIR_ENVIO / aparelho
        return sorted(p.name for p in pasta.iterdir()) if pasta.is_dir() else []

    # -- autorização --

    def test_sem_token_recusa(self):
        st, _ = self.envia("infracoes.log", b"dado", token=None)
        self.assertEqual(st, 401)
        self.assertEqual(self.recebidos(), [])

    def test_token_errado_recusa(self):
        st, _ = self.envia("infracoes.log", b"dado", token=TOKEN + "x")
        self.assertEqual(st, 401)
        self.assertEqual(self.recebidos(), [])

    def test_token_quase_certo_recusa(self):
        # Prefixo correto. `hmac.compare_digest` não encurta a comparação,
        # mas o teste existe para que trocar por `==` não passe despercebido
        # como "funciona igual".
        st, _ = self.envia("infracoes.log", b"dado", token=TOKEN[:-1])
        self.assertEqual(st, 401)

    def test_sem_token_a_resposta_nao_revela_se_o_nome_seria_aceito(self):
        # Nome válido e nome inválido devolvem a MESMA coisa para quem não
        # tem token. Senão a rota vira um oráculo de nomes aceitos.
        a = self.envia("infracoes.log", b"x", token=None)
        b = self.envia("passwd", b"x", token=None)
        self.assertEqual(a, b)

    def test_consulta_de_crc_tambem_exige_token(self):
        self.envia("infracoes.log", b"dado")
        st, _ = self.pede("GET", s.ROTA_ENVIO + "infracoes.log", token=None)
        self.assertEqual(st, 401)

    # -- recepção desligada --

    def test_servidor_sem_lista_de_aparelhos_nao_recebe_nada(self):
        # Sem lista não há a quem atribuir o arquivo, e gravar sem saber de
        # quem veio derrota o propósito da funcionalidade. Quem sobe o
        # servidor só para distribuir a base não ganha um depósito aberto.
        self.srv.shutdown()
        self.t.join(timeout=5)
        self.srv.server_close()
        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1")
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()
        for token in (None, "", TOKEN):
            st, _ = self.envia("infracoes.log", b"dado", token=token)
            self.assertEqual(st, 401, repr(token))
        self.assertEqual(self.recebidos(), [])

    # -- lista branca de nomes --

    def test_aceita_os_tres_nomes_que_o_firmware_gera(self):
        for nome in ("coruja.log", "infracoes.log", "20261006_143000.log"):
            st, _ = self.envia(nome, b"conteudo")
            self.assertEqual(st, 201, nome)
        self.assertEqual(self.recebidos(),
                         ["20261006_143000.log", "coruja.log",
                          "infracoes.log"])

    def test_recusa_nome_fora_da_lista(self):
        for nome in ("coruja.cfg", "radares.bin", "infracoes.log.bak",
                     "2026100_143000.log", "20261006_1430.log",
                     "INFRACOES.LOG", ""):
            st, _ = self.envia(nome, b"x")
            self.assertEqual(st, 404, repr(nome))
        self.assertEqual(self.recebidos(), [])

    def test_travessia_de_caminho_nao_escreve_fora(self):
        # A lista branca já barra tudo isto. O teste confirma o EFEITO, não a
        # regra: o que importa é que nenhum arquivo apareceu fora da pasta.
        antes = sorted(p.name for p in self.dados.iterdir())
        for nome in ("../servidor.py", "..%2fservidor.py", "a/../../fora.log",
                     "/etc/passwd", "sub/infracoes.log"):
            self.envia(nome, b"invasao")
        self.assertEqual(sorted(p.name for p in self.dados.iterdir()), antes)

    # -- o caminho feliz --

    def test_envio_valido_devolve_201_e_o_crc_do_corpo(self):
        corpo = b"2026-10-06 12:00:00 radar 60 km/h\n" * 40
        st, resp = self.envia("infracoes.log", corpo)
        self.assertEqual(st, 201)
        self.assertEqual(resp.decode().strip(), f"{zlib.crc32(corpo):08x}")

    def test_o_arquivo_salvo_e_identico_ao_enviado(self):
        corpo = bytes(range(256)) * 13
        self.envia("coruja.log", corpo)
        self.assertEqual((self.dados / s.SUBDIR_ENVIO / PASTA / "coruja.log")
                         .read_bytes(), corpo)

    def test_salva_na_subpasta_e_nao_junto_da_base(self):
        # `recebidos/` separa o que o aparelho mandou do que o servidor
        # publica. Misturar deixaria um log do aparelho a um nome de
        # distância de ser servido como dado oficial.
        self.envia("infracoes.log", b"dado")
        self.assertFalse((self.dados / "infracoes.log").exists())
        self.assertTrue((self.dados / s.SUBDIR_ENVIO / PASTA
                         / "infracoes.log").is_file())

    def test_crc_consultado_bate_com_o_do_envio(self):
        corpo = b"viagem de teste"
        _, do_envio = self.envia("20261006_143000.log", corpo)
        st, consultado = self.pede(
            "GET", s.ROTA_ENVIO + "20261006_143000.log")
        self.assertEqual(st, 200)
        self.assertEqual(consultado, do_envio)
        self.assertEqual(consultado.decode().strip(),
                         f"{zlib.crc32(corpo):08x}")

    def test_crc_de_arquivo_nunca_enviado_e_404(self):
        # 404 e não 200-com-crc-de-vazio: o aparelho não pode confundir
        # "não chegou" com "chegou vazio" e apagar o original.
        st, _ = self.pede("GET", s.ROTA_ENVIO + "infracoes.log")
        self.assertEqual(st, 404)

    def test_crc_muda_quando_o_conteudo_muda(self):
        _, a = self.envia("coruja.log", b"primeira versao")
        _, b = self.envia("coruja.log", b"segunda versao, maior")
        self.assertNotEqual(a, b)

    def test_reenvio_sobrescreve(self):
        self.envia("coruja.log", b"velho")
        self.envia("coruja.log", b"novo")
        self.assertEqual((self.dados / s.SUBDIR_ENVIO / PASTA / "coruja.log")
                         .read_bytes(), b"novo")
        self.assertEqual(self.recebidos(), ["coruja.log"])

    def test_envio_grande_atravessa_inteiro(self):
        # Maior que o pedaço de 64 KB da leitura: o laço tem de dar mais de
        # uma volta, que é onde erro de contagem aparece.
        corpo = b"x" * (200 * 1024)
        st, resp = self.envia("coruja.log", corpo)
        self.assertEqual(st, 201)
        self.assertEqual(resp.decode().strip(), f"{zlib.crc32(corpo):08x}")
        self.assertEqual((self.dados / s.SUBDIR_ENVIO / PASTA / "coruja.log")
                         .stat().st_size, len(corpo))

    # -- corpo malformado --

    def test_corpo_vazio_e_recusado(self):
        st, _ = self.envia("coruja.log", b"")
        self.assertEqual(st, 400)
        self.assertEqual(self.recebidos(), [])

    def test_acima_do_teto_e_recusado_sem_ler_o_corpo(self):
        # Content-Length mentiroso e enorme, corpo minúsculo: se o servidor
        # tentasse ler antes de conferir o teto, travaria aqui.
        st, _ = self.envia("coruja.log", b"x",
                           tamanho=s.TAMANHO_MAXIMO + 1)
        self.assertEqual(st, 413)
        self.assertEqual(self.recebidos(), [])

    def test_corpo_truncado_nao_vira_arquivo(self):
        # O caso que a gravação em temporário existe para cobrir: declarou
        # 500 bytes, mandou 10 e fechou. Nada pode ficar com o nome final,
        # senão o aparelho pergunta o CRC de um pedaço, não bate, e o lixo
        # fica no servidor para sempre.
        c = self.conexao()
        try:
            c.putrequest("PUT", s.ROTA_ENVIO + "coruja.log",
                         skip_accept_encoding=True)
            c.putheader(s.CABECALHO_TOKEN, TOKEN)
            c.putheader("Connection", "close")
            c.putheader("Content-Length", "500")
            c.endheaders()
            c.send(b"so dez byt")
            c.sock.shutdown(1)
            self.assertEqual(c.getresponse().status, 400)
        finally:
            c.close()
        self.assertEqual(self.recebidos(), [])

    def test_reenvio_que_falha_nao_destroi_a_copia_boa(self):
        # O que o arquivo temporário existe para proteger, e o que o teste
        # anterior NÃO via: abrir o nome final em "wb" trunca o bom antes de
        # saber se o novo vai chegar inteiro. Um reenvio que cai no meio
        # apagaria o único exemplar — e o aparelho já pode ter apagado o
        # dele, porque o primeiro envio deu certo.
        bom = b"registro completo da viagem" * 20
        self.envia("20261006_143000.log", bom)

        c = self.conexao()
        try:
            c.putrequest("PUT", s.ROTA_ENVIO + "20261006_143000.log",
                         skip_accept_encoding=True)
            c.putheader(s.CABECALHO_TOKEN, TOKEN)
            c.putheader("Connection", "close")
            c.putheader("Content-Length", "9999")
            c.endheaders()
            c.send(b"comeco so")
            c.sock.shutdown(1)
            self.assertEqual(c.getresponse().status, 400)
        finally:
            c.close()

        self.assertEqual(
            (self.dados / s.SUBDIR_ENVIO / PASTA / "20261006_143000.log")
            .read_bytes(), bom)

    def test_sucesso_nao_deixa_parcial_para_tras(self):
        self.envia("coruja.log", b"conteudo qualquer")
        self.assertEqual(self.recebidos(), ["coruja.log"])

    def test_content_length_nao_numerico_e_400(self):
        c = self.conexao()
        try:
            c.putrequest("PUT", s.ROTA_ENVIO + "coruja.log",
                         skip_accept_encoding=True)
            c.putheader(s.CABECALHO_TOKEN, TOKEN)
            c.putheader("Connection", "close")
            c.putheader("Content-Length", "muitos")
            c.endheaders()
            self.assertEqual(c.getresponse().status, 400)
        finally:
            c.close()

    # -- convivência com a rota da base --

    def test_recebidos_pode_ficar_fora_do_volume_da_base(self):
        # O volume da base é montado `:ro` e o container é `read_only`, para
        # que um servidor comprometido não consiga trocar o `radares.bin` que
        # os aparelhos vão baixar. Receber arquivo exige escrita; gravar
        # dentro do volume da base jogaria essa garantia fora.
        outro = tempfile.TemporaryDirectory()
        self.addCleanup(outro.cleanup)
        caixa = Path(outro.name)

        self.srv.shutdown()
        self.t.join(timeout=5)
        self.srv.server_close()
        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1", APARELHOS,
                                   caixa)
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()

        st, _ = self.envia("infracoes.log", b"dado")
        self.assertEqual(st, 201)
        self.assertTrue((caixa / PASTA / "infracoes.log").is_file())
        # Nada foi escrito no diretório da base.
        self.assertEqual(sorted(p.name for p in self.dados.iterdir()), [])

        st, _ = self.pede("GET", s.ROTA_ENVIO + "infracoes.log")
        self.assertEqual(st, 200)

    def test_com_lista_de_aparelhos_a_base_tambem_exige_token(self):
        # Mudança de contrato deliberada: com a lista preenchida, o servidor
        # deixa de responder qualquer coisa a quem não se identifica.
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        for rota in (s.ROTA_VERSAO, s.ROTA_BASE):
            self.assertEqual(self.pede("GET", rota, token=None)[0], 401, rota)
            self.assertEqual(self.pede("GET", rota)[0], 200, rota)

    def test_o_que_foi_recebido_nao_fica_exposto_na_raiz(self):
        # `recebidos/carro/infracoes.log` não pode ser baixável nem por quem
        # TEM token: o servidor serve duas rotas e mais nada.
        self.envia("infracoes.log", b"dado privado")
        for rota in ("/infracoes.log", "/recebidos/infracoes.log",
                     "/recebidos/carro/infracoes.log", "/recebidos/"):
            self.assertEqual(self.pede("GET", rota)[0], 404, rota)
            # E sem token nem chega a existir a resposta de "não existe".
            self.assertEqual(self.pede("GET", rota, token=None)[0], 401, rota)

    # -- o corpo nao lido nao pode contaminar o pedido seguinte --

    def test_put_recusado_nao_contamina_o_pedido_seguinte(self):
        r"""O defeito que só a produção mostrou, em 2026-10-06.

        Um `PUT` de um byte recusado com 401, e o `GET` logo depois voltando
        `501 Unsupported method ('xGET')` -- o `x` era o corpo do PUT, que
        ficou na conexão porque a recusa acontece ANTES de ler o corpo.

        Os testes não pegavam: cada caso abria conexão nova. O Cloudflare
        reaproveita.
        """
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        c = self.conexao()
        try:
            # 1) PUT recusado, com corpo que o servidor nao le
            c.putrequest("PUT", s.ROTA_ENVIO + "coruja.log",
                         skip_accept_encoding=True)
            c.putheader("Content-Length", "1")
            c.endheaders()
            c.send(b"x")
            r1 = c.getresponse()
            r1.read()
            self.assertEqual(r1.status, 401)
            # O servidor TEM de fechar: o byte `x` continua no fluxo.
            self.assertEqual(r1.getheader("Connection", "").lower(), "close")
        finally:
            c.close()

    def test_a_conexao_e_reaproveitada_quando_o_corpo_FOI_lido(self):
        # Fechar sempre seria caro: o aparelho manda varios arquivos em
        # sequencia, e cada fechamento custa um aperto de mao novo.
        c = self.conexao()
        try:
            for i in range(3):
                corpo = f"envio {i}".encode()
                c.putrequest("PUT", s.ROTA_ENVIO + "coruja.log",
                             skip_accept_encoding=True)
                c.putheader(s.CABECALHO_TOKEN, TOKEN)
                c.putheader("Content-Length", str(len(corpo)))
                c.endheaders()
                c.send(corpo)
                r = c.getresponse()
                lido = r.read()
                self.assertEqual(r.status, 201, f"volta {i}")
                self.assertNotEqual(r.getheader("Connection", "").lower(),
                                    "close", f"fechou na volta {i}")
                self.assertEqual(lido.decode().strip(),
                                 f"{zlib.crc32(corpo):08x}")
        finally:
            c.close()

    def test_content_length_malformado_tambem_fecha(self):
        # Nao da para saber quantos bytes sobraram; reaproveitar seria apostar.
        c = self.conexao()
        try:
            c.putrequest("PUT", s.ROTA_ENVIO + "coruja.log",
                         skip_accept_encoding=True)
            c.putheader(s.CABECALHO_TOKEN, TOKEN)
            c.putheader("Content-Length", "muitos")
            c.endheaders()
            r = c.getresponse()
            r.read()
            self.assertEqual(r.status, 400)
            self.assertEqual(r.getheader("Connection", "").lower(), "close")
        finally:
            c.close()

    # -- a validação vale para TODAS as rotas --

    def test_a_raiz_tambem_exige_token(self):
        # Sem token, nem o 404 sai: dizer que uma rota não existe já é
        # informação sobre o servidor.
        self.assertEqual(self.pede("GET", "/", token=None)[0], 401)
        self.assertEqual(self.pede("GET", "/", token=TOKEN)[0], 404)

    def test_rota_desconhecida_tambem_exige_token(self):
        for rota in ("/qualquer", "/admin", "/radares.versao.bak"):
            self.assertEqual(self.pede("GET", rota, token=None)[0], 401, rota)
            self.assertEqual(self.pede("GET", rota, token=TOKEN)[0], 404, rota)

    def test_head_tambem_exige_token(self):
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        self.assertEqual(self.pede("HEAD", s.ROTA_BASE, token=None)[0], 401)
        self.assertEqual(self.pede("HEAD", s.ROTA_BASE)[0], 200)

    def test_token_desconhecido_e_401_e_nao_403(self):
        # 403 significa "sei quem é você e não pode". Aqui não se sabe quem é,
        # e a distinção é o que diz ao cliente se vale tentar outra
        # credencial ou desistir.
        st, corpo = self.pede("GET", s.ROTA_VERSAO, token="nao-esta-na-lista")
        self.assertEqual(st, 401)
        self.assertIn(b"desconhecido", corpo)

    def test_travessia_de_caminho_continua_404_para_quem_tem_token(self):
        for tentativa in ("/../servidor.py", "/%2e%2e/servidor.py",
                          "/radares.bin/../../servidor.py"):
            self.assertEqual(self.pede("GET", tentativa)[0], 404, tentativa)

    # -- o token vai SÓ no cabeçalho --

    def test_token_na_url_nao_autentica(self):
        # Houve uma versão que aceitava `?t=`, por limitação do cliente de
        # download do aparelho. Com TLS o transporte foi reescrito e o gancho
        # de cabeçalho passou a existir; aceitar a URL só manteria o segredo
        # indo parar no log de acesso do servidor e de qualquer proxy.
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        for rota in (f"{s.ROTA_VERSAO}?t={TOKEN}",
                     f"{s.ROTA_VERSAO}?token={TOKEN}",
                     f"{s.ROTA_BASE}?t={TOKEN}"):
            self.assertEqual(self.pede("GET", rota, token=None)[0], 401, rota)

    def test_envio_com_token_so_na_url_e_recusado(self):
        rota = f"{s.ROTA_ENVIO}coruja.log?t={TOKEN}"
        st, _ = self.pede("PUT", rota, b"dado", token=None)
        self.assertEqual(st, 401)
        self.assertEqual(self.recebidos(), [])

    def test_a_consulta_nao_atrapalha_a_rota(self):
        # O `?` continua sendo ignorado para efeito de ROTA: `/radares.versao`
        # com query ainda é `/radares.versao`, só que a credencial tem de vir
        # no cabeçalho.
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        st, corpo = self.pede("GET", s.ROTA_VERSAO + "?v=2", token=TOKEN)
        self.assertEqual(st, 200)
        self.assertIn(b"crc32:", corpo)

    def test_envio_com_consulta_na_url_funciona_pelo_cabecalho(self):
        st, _ = self.pede("PUT", s.ROTA_ENVIO + "coruja.log?v=1", b"dado",
                          token=TOKEN)
        self.assertEqual(st, 201)
        self.assertEqual(self.recebidos(), ["coruja.log"])

    # -- um aparelho não enxerga o outro --

    def test_cada_aparelho_tem_a_propria_pasta(self):
        self.envia("infracoes.log", b"do carro", token=TOKEN)
        self.envia("infracoes.log", b"da bancada", token=TOKEN_OUTRO)

        raiz = self.dados / s.SUBDIR_ENVIO
        self.assertEqual((raiz / PASTA / "infracoes.log").read_bytes(),
                         b"do carro")
        self.assertEqual((raiz / PASTA_OUTRO / "infracoes.log").read_bytes(),
                         b"da bancada")

    def test_um_aparelho_nao_le_o_crc_do_outro(self):
        # O caso que a separação existe para impedir: dois carros gravam
        # `infracoes.log`. Sem pasta por aparelho, o segundo envio faria o
        # CRC bater para o primeiro -- que apagaria do cartão um registro
        # que o servidor nunca recebeu.
        self.envia("infracoes.log", b"do carro", token=TOKEN)
        st, _ = self.pede("GET", s.ROTA_ENVIO + "infracoes.log",
                          token=TOKEN_OUTRO)
        self.assertEqual(st, 404)

    def test_o_mesmo_arquivo_de_dois_aparelhos_nao_se_sobrescreve(self):
        corpo_a, corpo_b = b"viagem do carro", b"viagem da bancada"
        _, crc_a = self.envia("20261006_143000.log", corpo_a, token=TOKEN)
        _, crc_b = self.envia("20261006_143000.log", corpo_b,
                              token=TOKEN_OUTRO)
        self.assertNotEqual(crc_a, crc_b)
        self.assertEqual(crc_a.decode().strip(), f"{zlib.crc32(corpo_a):08x}")

        st, consultado = self.pede("GET", s.ROTA_ENVIO + "20261006_143000.log",
                                   token=TOKEN)
        self.assertEqual(st, 200)
        self.assertEqual(consultado, crc_a)

    def test_o_token_nao_vira_nome_de_pasta(self):
        # O ponto inteiro da impressão: o segredo também SERVE PARA ENVIAR, e
        # escrito no disco apareceria em `ls`, em backup e no log.
        self.envia("coruja.log", b"dado")
        tudo = [p.name for p in (self.dados / s.SUBDIR_ENVIO).iterdir()]
        self.assertNotIn(TOKEN, tudo)
        self.assertIn(PASTA, tudo)

    def test_a_impressao_nao_revela_o_token(self):
        # Hexadecimal de 16 dígitos, e nada do segredo dentro.
        self.assertRegex(PASTA, r"^[0-9a-f]{16}$")
        self.assertNotIn(PASTA, TOKEN)

    def test_o_aparelho_nao_vaza_na_resposta(self):
        # A resposta é só o CRC. Devolver a pasta não serviria a ninguém e
        # contaria a quem tem UM token quantos aparelhos existem.
        _, corpo = self.envia("coruja.log", b"dado")
        self.assertNotIn(PASTA.encode(), corpo)


class AparelhoDeTokenVazio(unittest.TestCase):
    """Uma entrada de token vazio não pode virar um passe para todos.

    O `le_aparelhos` recusa isso no arquivo. Mas a lista também chega por
    código — é assim que estes testes montam servidores —, e aí ninguém a
    validou. Sem a guarda, `compare_digest("", "")` casa e toda requisição
    anônima vira aquele aparelho.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dados = Path(self.tmp.name)
        self.srv = s.cria_servidor(self.dados, 0, "127.0.0.1",
                                   {"": "fantasma", TOKEN: "carro"})
        self.porta = self.srv.server_address[1]
        self.t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        self.t.start()
        self.addCleanup(self.t.join, 5)
        self.addCleanup(self.srv.server_close)
        self.addCleanup(self.srv.shutdown)

    def pede(self, metodo, rota, token=None):
        cab = {"Connection": "close"}
        if token is not None:
            cab[s.CABECALHO_TOKEN] = token
        c = http.client.HTTPConnection("127.0.0.1", self.porta, timeout=5)
        try:
            c.request(metodo, rota, headers=cab)
            r = c.getresponse()
            r.read()
            return r.status
        finally:
            c.close()

    def test_requisicao_sem_token_nao_vira_o_aparelho_fantasma(self):
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        self.assertEqual(self.pede("GET", s.ROTA_VERSAO), 401)
        self.assertEqual(self.pede("GET", s.ROTA_VERSAO, token=""), 401)
        self.assertEqual(self.pede("GET", "/"), 401)

    def test_o_aparelho_legitimo_continua_entrando(self):
        (self.dados / s.NOME_BASE).write_bytes(base_valida())
        self.assertEqual(self.pede("GET", s.ROTA_VERSAO, token=TOKEN), 200)

    def test_envio_anonimo_nao_grava_como_fantasma(self):
        c = http.client.HTTPConnection("127.0.0.1", self.porta, timeout=5)
        try:
            c.request("PUT", s.ROTA_ENVIO + "coruja.log", body=b"dado",
                      headers={"Connection": "close"})
            r = c.getresponse()
            r.read()
            self.assertEqual(r.status, 401)
        finally:
            c.close()
        self.assertFalse((self.dados / s.SUBDIR_ENVIO).exists())


class LeAparelhos(unittest.TestCase):
    """A lista `nome=token` que diz quem pode falar com o servidor.

    É o único lugar onde um erro de digitação vira servidor aberto ou
    aparelho mudo, então ela recusa em voz alta em vez de adivinhar.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.caminho = Path(self.tmp.name) / "aparelhos.cfg"

    def le(self, texto):
        """Só os aparelhos. A seção `[mapa]` tem testes próprios."""
        self.caminho.write_text(texto, encoding="utf-8")
        return s.le_aparelhos(self.caminho).por_token

    def le_tudo(self, texto):
        self.caminho.write_text(texto, encoding="utf-8")
        return s.le_aparelhos(self.caminho)

    def test_um_token_por_linha(self):
        r = self.le("abc\ndef\n")
        self.assertEqual(set(r), {"abc", "def"})

    def test_a_pasta_e_a_impressao_do_token(self):
        # Derivada, não configurada: não há nome para manter em lugar nenhum.
        r = self.le("abc\n")
        self.assertEqual(r["abc"], s.impressao_do_token("abc"))

    def test_indexa_por_token_porque_e_assim_que_se_pergunta(self):
        # Chega um segredo e a pergunta é "de quem é este". O caminho inverso
        # nunca é percorrido.
        self.assertIn("abc", self.le("abc\n"))

    def test_ignora_comentario_e_linha_em_branco(self):
        self.assertEqual(
            set(self.le("# os aparelhos\n\nabc\n\n   # outro\n")), {"abc"})

    def test_apara_espacos(self):
        self.assertEqual(set(self.le("   abc   \n")), {"abc"})

    def test_aceita_ultima_linha_sem_quebra(self):
        self.assertEqual(set(self.le("abc")), {"abc"})

    def test_o_formato_antigo_e_recusado_com_explicacao(self):
        # `nome=token` existiu por algumas horas em 2026-10-06. Aceitar
        # calado gravaria na pasta errada; a mensagem diz o que fazer.
        with self.assertRaises(s.ErroDeAparelhos) as e:
            self.le("carro=abc\n")
        self.assertIn("um token por linha", str(e.exception))

    def test_arquivo_ausente_e_lista_vazia_e_nao_erro(self):
        # É o caso de quem só distribui a base. Exigir o arquivo obrigaria a
        # criar um vazio para nada.
        self.assertEqual(s.le_aparelhos(self.caminho).por_token, {})
        self.assertEqual(s.le_aparelhos(self.caminho).mapa, {})

    def test_arquivo_so_de_comentario_e_lista_vazia(self):
        self.assertEqual(self.le("# nada aqui ainda\n"), {})

    def test_linha_sem_igual_dentro_de_mapa_e_erro(self):
        # Fora da seção, uma linha sem `=` é um token -- é o formato. Dentro
        # dela, é ajuste pela metade.
        with self.assertRaises(s.ErroDeAparelhos):
            self.le_tudo("[mapa]\nthunderforest\n")

    def test_token_repetido_e_erro(self):
        # O que mais importa recusar: dois aparelhos com o mesmo segredo
        # tornam a atribuição ambígua, e saber de quem veio o arquivo é o
        # propósito inteiro da lista.
        with self.assertRaises(s.ErroDeAparelhos) as e:
            self.le("abc\nabc\n")
        self.assertIn("qual aparelho", str(e.exception))

    def test_a_impressao_nunca_vira_caminho_para_fora(self):
        # O nome da pasta é derivado, não escrito por alguém: hexadecimal
        # puro não tem `..`, nem barra, nem espaço. É o que substituiu a
        # lista branca de nomes que existia quando a pasta era configurada.
        for token in ("../fora", "a/b", "..", "com espaco", "/abs",
                      "x" * 500, "ç€emoji😀"):
            with self.subTest(token=token):
                self.assertRegex(s.impressao_do_token(token),
                                 r"^[0-9a-f]{16}$")

    def test_tokens_diferentes_dao_pastas_diferentes(self):
        pastas = {s.impressao_do_token(f"token-{i}") for i in range(200)}
        self.assertEqual(len(pastas), 200)

    def test_a_impressao_e_estavel_entre_execucoes(self):
        # Se mudasse, as viagens já recebidas sumiriam da página -- elas
        # estariam numa pasta que ninguém mais procura.
        self.assertEqual(s.impressao_do_token("abc"),
                         s.impressao_do_token("abc"))

    # -- a secao [mapa] --

    def test_le_a_chave_do_mapa(self):
        r = self.le_tudo("abc\n\n[mapa]\nthunderforest=xyz123\n")
        self.assertEqual(set(r.por_token), {"abc"})
        self.assertEqual(r.mapa, {"thunderforest": "xyz123"})

    def test_sem_secao_de_mapa_o_dicionario_vem_vazio(self):
        self.assertEqual(self.le_tudo("abc\n").mapa, {})

    def test_a_secao_separa_tokens_de_ajustes(self):
        # O risco de juntar os dois num arquivo só: uma chave de serviço
        # externo virar token de aparelho, e passar a autenticar envios.
        r = self.le_tudo("abc\n[mapa]\nthunderforest=xyz\n")
        self.assertNotIn("xyz", r.por_token)
        self.assertNotIn("thunderforest=xyz", r.por_token)
        self.assertEqual(len(r.por_token), 1)

    def test_secao_desconhecida_e_erro(self):
        # `[mapas]` em vez de `[mapa]` deixaria a chave cair num balde
        # ignorado, e o sintoma seria um mapa em branco sem explicação.
        with self.assertRaises(s.ErroDeAparelhos) as e:
            self.le_tudo("abc\n[mapas]\nthunderforest=xyz\n")
        self.assertIn("desconhecida", str(e.exception))

    def test_a_secao_nao_diferencia_maiuscula(self):
        r = self.le_tudo("abc\n[MAPA]\nThunderforest=xyz\n")
        self.assertEqual(r.mapa, {"thunderforest": "xyz"})

    def test_valor_do_mapa_preserva_maiuscula(self):
        # A chave da API diferencia maiúscula; só o NOME do ajuste é
        # normalizado.
        r = self.le_tudo("[mapa]\nthunderforest=AbCdEf123\n")
        self.assertEqual(r.mapa["thunderforest"], "AbCdEf123")

    def test_a_mensagem_de_erro_diz_a_linha(self):
        # O arquivo é editado à mão e o erro impede o servidor de subir;
        # dizer "erro no arquivo" mandaria procurar.
        with self.assertRaises(s.ErroDeAparelhos) as e:
            self.le("abc\ndef\ncarro=ghi\n")
        self.assertIn(":3", str(e.exception))


if __name__ == "__main__":
    unittest.main()
