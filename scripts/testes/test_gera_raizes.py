"""Testes do pacote de raízes confiáveis.

    python3 -m unittest discover -s scripts/testes

O que está em jogo aqui é de quem o aparelho aceita uma atualização. Uma raiz
trocada não quebra nada visível — ela passa a validar o servidor errado —, e
uma raiz vencida mata o OTA numa data futura sem ninguém ter mexido em nada.
Os dois casos só aparecem se alguém perguntar, e é isso que estes testes fazem.
"""

import contextlib
import datetime
import hashlib
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))

import gera_raizes as g  # noqa: E402

CABECALHO = RAIZ / "firmware/src/rede/RaizesConfiaveis.h"


def certificados_do_cabecalho() -> list[str]:
    """Os PEM que o firmware de fato vai compilar.

    Reconstrói o literal C de volta em texto: é o que o compilador vai ver,
    e não o que o gerador quis escrever. Conferir a intenção em vez do
    resultado deixaria passar um arquivo editado à mão.
    """
    texto = CABECALHO.read_text(encoding="utf-8")
    linhas = re.findall(r'^\s*"(.*?)\\n"$', texto, re.M)
    bloco = "\n".join(linhas) + "\n"
    return re.findall(
        r"-----BEGIN CERTIFICATE-----\n.*?-----END CERTIFICATE-----\n",
        bloco, re.S)


def der_de(pem: str) -> bytes:
    return subprocess.run(["openssl", "x509", "-outform", "DER"],
                          input=pem.encode(), capture_output=True,
                          check=True).stdout


def validade_de(pem: str) -> datetime.datetime:
    saida = subprocess.run(["openssl", "x509", "-noout", "-enddate"],
                           input=pem.encode(), capture_output=True,
                           check=True).stdout.decode()
    return g.data_de(saida.split("=", 1)[1])


class PacoteDeRaizes(unittest.TestCase):
    def setUp(self):
        self.pems = certificados_do_cabecalho()

    def test_o_cabecalho_existe_e_tem_as_tres_raizes(self):
        self.assertEqual(len(self.pems), len(g.RAIZES))

    def test_cada_raiz_bate_com_a_impressao_fixada(self):
        # O teste que pega edição à mão do arquivo gerado. Trocar uma raiz
        # exige trocar também a impressão no script, de propósito.
        esperadas = {imp for _n, _u, imp, _m in g.RAIZES}
        vistas = {hashlib.sha256(der_de(p)).hexdigest() for p in self.pems}
        self.assertEqual(vistas, esperadas)

    def test_toda_raiz_e_auto_assinada(self):
        # Um intermediário embutido por engano expiraria anos antes e levaria
        # o OTA junto, sem aviso.
        for pem in self.pems:
            campos = subprocess.run(
                ["openssl", "x509", "-noout", "-subject", "-issuer"],
                input=pem.encode(), capture_output=True, check=True
            ).stdout.decode().splitlines()
            sujeito = campos[0].split("=", 1)[1]
            emissor = campos[1].split("=", 1)[1]
            self.assertEqual(sujeito, emissor)

    def test_toda_raiz_e_uma_CA(self):
        for pem in self.pems:
            texto = subprocess.run(["openssl", "x509", "-noout", "-text"],
                                   input=pem.encode(), capture_output=True,
                                   check=True).stdout.decode()
            self.assertIn("CA:TRUE", texto)

    def test_nenhuma_raiz_esta_vencida(self):
        # Vence em silêncio: o aparelho só descobre quando um handshake falha
        # em campo, e aí o OTA -- que seria o conserto -- é justamente o que
        # parou.
        agora = datetime.datetime.now(datetime.timezone.utc)
        for pem in self.pems:
            self.assertGreater(validade_de(pem), agora)

    def test_sobra_mais_de_um_ano_na_raiz_mais_curta(self):
        # Aviso antecipado, não constatação de óbito. Um ano é prazo para
        # regerar o pacote e distribuir o firmware novo pelo próprio OTA,
        # enquanto ele ainda funciona.
        agora = datetime.datetime.now(datetime.timezone.utc)
        menor = min(validade_de(p) for p in self.pems)
        self.assertGreater(
            menor, agora + datetime.timedelta(days=365),
            f"a raiz mais curta vence em {menor}; regere o pacote "
            "(python3 scripts/gera_raizes.py) ANTES que o OTA pare")

    def test_as_impressoes_fixadas_nao_se_repetem(self):
        impressoes = [imp for _n, _u, imp, _m in g.RAIZES]
        self.assertEqual(len(impressoes), len(set(impressoes)))

    def test_o_tamanho_declarado_inclui_o_terminador(self):
        # `mbedtls_x509_crt_parse` recusa o PEM inteiro sem ele, e o sintoma
        # é um handshake que falha sem explicar por quê.
        texto = CABECALHO.read_text(encoding="utf-8")
        self.assertIn("sizeof kRaizesConfiaveis", texto)

    def test_o_cabecalho_avisa_que_e_gerado(self):
        self.assertIn("NAO EDITE A MAO",
                      CABECALHO.read_text(encoding="utf-8"))


class ServidorDeVerdade(unittest.TestCase):
    """Confere o pacote embutido contra o servidor que o aparelho vai usar.

    Pulado por padrão, porque depende de rede. Para rodar:

        CORUJA_URL=https://coruja.bpldev.com \
            python3 -m unittest discover -s scripts/testes

    ⚠️ **É o único teste que pega uma rotação de CA do Cloudflare.** Quem
    escolhe a autoridade do certificado é ele, não o dono do aparelho: no
    plano gratuito alterna entre a Google Trust Services e a Let's Encrypt a
    critério dele. O pacote cobre as duas, mas se um dia aparecer uma
    terceira, o OTA morre em campo — e o OTA é justamente como o aparelho se
    conserta. Este teste avisa antes.
    """

    def setUp(self):
        self.url = os.environ.get("CORUJA_URL", "").strip()
        if not self.url:
            self.skipTest("defina CORUJA_URL para conferir contra o servidor")
        self.host = urllib.parse.urlsplit(self.url).hostname
        self.assertTrue(self.host, f"URL sem host: {self.url}")

        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pasta = Path(self.tmp.name)

        # O pacote exatamente como o firmware o compila.
        self.pacote = self.pasta / "pacote.pem"
        self.pacote.write_text("".join(certificados_do_cabecalho()))

        bruto = subprocess.run(
            ["openssl", "s_client", "-connect", f"{self.host}:443",
             "-servername", self.host, "-showcerts"],
            input=b"", capture_output=True, timeout=30).stdout.decode(
                errors="replace")
        self.cadeia = re.findall(
            r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n",
            bruto, re.S)
        if not self.cadeia:
            self.skipTest(f"nao consegui falar com {self.host}")

    def test_a_cadeia_do_servidor_valida_contra_o_pacote_embutido(self):
        # A pergunta que importa: o aparelho, com ESTAS raizes, aceitaria
        # ESTE servidor?
        folha = self.pasta / "folha.pem"
        folha.write_text(self.cadeia[0])
        intermediarias = self.pasta / "meio.pem"
        intermediarias.write_text("".join(self.cadeia[1:]))

        r = subprocess.run(
            ["openssl", "verify", "-CAfile", str(self.pacote),
             "-untrusted", str(intermediarias), str(folha)],
            capture_output=True, timeout=30)
        self.assertEqual(
            r.returncode, 0,
            f"o servidor serve uma cadeia que as raizes embutidas NAO "
            f"validam.\n{r.stdout.decode()}{r.stderr.decode()}\n"
            "Se a autoridade mudou, acrescente a raiz nova em "
            "scripts/gera_raizes.py e regere ANTES de o OTA parar.")

    def test_o_certificado_do_servidor_esta_no_prazo(self):
        # O aparelho julga o prazo com o relogio que o NTP acertou. Um
        # certificado vencido no servidor para o OTA do mesmo jeito que uma
        # raiz vencida no firmware.
        agora = datetime.datetime.now(datetime.timezone.utc)
        self.assertGreater(validade_de(self.cadeia[0]), agora)

    def test_o_servidor_aceita_TLS_1_2(self):
        # O firmware nao fala TLS 1.3: ele exige a camada PSA do mbedTLS, que
        # nao cabe no orcamento de RAM. Se o servidor um dia exigir 1.3, o
        # aparelho para -- e o sintoma seria um handshake recusado sem motivo
        # aparente.
        r = subprocess.run(
            ["openssl", "s_client", "-connect", f"{self.host}:443",
             "-servername", self.host, "-tls1_2"],
            input=b"", capture_output=True, timeout=30)
        self.assertIn("Protocol  : TLSv1.2", r.stdout.decode(errors="replace"),
                      "o servidor recusou TLS 1.2; o firmware so fala isso")

    def test_a_folha_e_de_um_tipo_de_chave_que_o_firmware_suporta(self):
        # O `mbedtls_config.h` liga ECDSA e RSA, e so. Uma folha Ed25519 --
        # que ja existe em algumas CAs -- nao seria verificavel.
        texto = subprocess.run(["openssl", "x509", "-noout", "-text"],
                               input=self.cadeia[0].encode(),
                               capture_output=True, check=True).stdout.decode()
        tipo = re.search(r"Public Key Algorithm: (.*)", texto).group(1).strip()
        self.assertIn(tipo, ("id-ecPublicKey", "rsaEncryption"), tipo)


def certificado_de_teste(pasta: Path, nome: str, *extras: str) -> str:
    """Um certificado EC P-256 gerado na hora, em PEM. Nada sai da maquina.

    `extras` vai direto ao `openssl req -x509`: e por ele que o teste monta
    o certificado errado de cada jeito (nao-CA, emitido por outro, curto).
    """
    chave = pasta / f"{nome}.key"
    saida = pasta / f"{nome}.pem"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "ec",
                    "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
                    "-keyout", str(chave), "-out", str(saida),
                    "-subj", f"/CN={nome}", *extras],
                   capture_output=True, check=True)
    return saida.read_text()


def impressao_de(pem: str) -> str:
    return hashlib.sha256(der_de(pem)).hexdigest()


class ConfereRaiz(unittest.TestCase):
    """A conferência que o gerador faz antes de aceitar um download."""

    def setUp(self):
        self.pem = certificados_do_cabecalho()[0]
        self.impressao = impressao_de(self.pem)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pasta = Path(self.tmp.name)

    def test_impressao_certa_passa(self):
        pem, _expira = g.confere("teste", self.pem.encode(), self.impressao)
        self.assertEqual(pem, self.pem.strip())

    def test_impressao_errada_recusa(self):
        # O caso que importa: alguém respondeu no lugar da autoridade.
        with self.assertRaises(SystemExit) as e:
            g.confere("teste", self.pem.encode(), "0" * 64)
        self.assertIn("sha256", str(e.exception))

    def test_o_der_tambem_e_aceito(self):
        # A GTS serve a raiz em DER (.crt); a ISRG, em PEM.
        pem, _ = g.confere("teste", der_de(self.pem), self.impressao)
        self.assertEqual(pem, self.pem.strip())

    def test_certificado_a_mais_no_pacote_NAO_e_embutido(self):
        # So o PRIMEIRO certificado da resposta e conferido. Antes, o PEM
        # inteiro ia para o firmware: um segundo certificado colado no fim
        # virava raiz de confianca sem impressao nenhuma.
        intruso = certificado_de_teste(self.pasta, "Intruso", "-days", "30")
        pem, _ = g.confere("teste", (self.pem + intruso).encode(),
                           self.impressao)
        self.assertEqual(pem.count("BEGIN CERTIFICATE"), 1)
        self.assertEqual(impressao_de(pem + "\n"), self.impressao)
        self.assertNotIn(intruso.strip().splitlines()[1], pem)

    def test_nao_auto_assinado_e_recusado(self):
        # Um intermediario expiraria anos antes da raiz e levaria o OTA
        # junto, sem aviso.
        certificado_de_teste(self.pasta, "Raiz", "-days", "30")
        meio = certificado_de_teste(
            self.pasta, "Intermediaria", "-days", "30",
            "-CA", str(self.pasta / "Raiz.pem"),
            "-CAkey", str(self.pasta / "Raiz.key"))
        with self.assertRaises(SystemExit) as e:
            g.confere("teste", meio.encode(), impressao_de(meio))
        self.assertIn("nao e auto-assinado", str(e.exception))

    def test_nao_CA_e_recusado(self):
        folha = certificado_de_teste(
            self.pasta, "Folha", "-days", "30",
            "-addext", "basicConstraints=critical,CA:FALSE")
        with self.assertRaises(SystemExit) as e:
            g.confere("teste", folha.encode(), impressao_de(folha))
        self.assertIn("nao e uma CA", str(e.exception))

    def test_raiz_vencida_e_recusada(self):
        # Embutir uma raiz vencida e embutir um OTA que ja nasce parado: o
        # aparelho recusaria todo servidor assinado por ela.
        raiz = certificado_de_teste(self.pasta, "Curta", "-days", "1")
        depois = (datetime.datetime.now(datetime.timezone.utc)
                  + datetime.timedelta(days=2))
        with self.assertRaises(SystemExit) as e:
            g.confere("teste", raiz.encode(), impressao_de(raiz),
                      agora=depois)
        self.assertIn("vencida", str(e.exception))
        # E no prazo, a mesma raiz passa.
        g.confere("teste", raiz.encode(), impressao_de(raiz))


class GeraOCabecalho(unittest.TestCase):
    def test_as_raizes_de_hoje_regeram_o_cabecalho_identico(self):
        # O PEM agora e reemitido do DER conferido. Para as tres raizes de
        # verdade isso nao pode mudar um byte do que o firmware compila --
        # e se prova sem rede, servindo a cada URL o que o cabecalho tem.
        pems = {impressao_de(p): p for p in certificados_do_cabecalho()}
        servido = {}
        for _nome, url, impressao, _motivo in g.RAIZES:
            pem = pems[impressao]
            # Como a autoridade serve: a GTS em DER, a ISRG em PEM.
            servido[url] = der_de(pem) if url.endswith(".crt") \
                else pem.encode()
        with contextlib.redirect_stderr(io.StringIO()):
            gerado = g.gera(servido.__getitem__)
        self.assertEqual(gerado, CABECALHO.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
