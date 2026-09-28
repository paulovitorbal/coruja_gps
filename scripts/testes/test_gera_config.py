"""Testes do gerador de configuração (regra 1).

Rode com:  python3 -m unittest discover -s scripts/testes -v

Usa `unittest` e não pytest, ao contrário da regra global de Python. O motivo
é local: todo o código Python deste projeto é **stdlib-only** por escolha, e
exigir uma dependência só para testar contrariaria a razão original. A troca custa pouco: `unittest` roda em
qualquer Python.

A concordância entre este gerador e o parser em C++ é verificada do outro
lado, em `firmware/test/nucleo/LeitorConfigTest.cpp`, que lê o
`coruja.cfg.exemplo` versionado. Gerador e leitor são escritos em linguagens
diferentes, e é esse par de testes que impede que divirjam.
"""

import re
import sys
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))

import gera_config as g  # noqa: E402




class GeraConfig(unittest.TestCase):
    def test_ssid_no_limite_passa(self):
            self.assertIsNone(g.valida_rede("a" * g.MAX_SSID, "12345678"))
    def test_ssid_longo_demais_falha(self):
        erro = g.valida_rede("a" * (g.MAX_SSID + 1), "12345678")
        self.assertTrue(erro)
        self.assertTrue("SSID" in erro)
    def test_senha_curta_demais_falha(self):
        # WPA2 exige 8 caracteres; aceitar menos gera um .cfg que o aparelho
        # carrega e que nunca conecta.
        erro = g.valida_rede("casa", "1234567")
        self.assertTrue(erro)
        self.assertTrue("8 caracteres" in erro)
    def test_senha_longa_demais_falha(self):
        erro = g.valida_rede("casa", "z" * (g.MAX_SENHA + 1))
        self.assertTrue(erro)
        self.assertTrue("senha" in erro)
    def test_rede_aberta_e_valida(self):
            self.assertIsNone(g.valida_rede("aberta", ""))
    def test_https_passa(self):
            self.assertIsNone(g.valida_url("https://exemplo/b.bin"))
    def test_http_simples_e_recusado(self):
        # RF05.2: em HTTP, quem estiver na mesma rede substitui a base de radares.
        erro = g.valida_url("http://exemplo/b.bin")
        self.assertTrue(erro)
        self.assertTrue("HTTPS" in erro)
        # A mensagem precisa dizer qual é a saída, senão quem está testando em
        # rede local não tem como descobrir que ela existe.
        self.assertIn("--permitir-http", erro)

    def test_http_e_aceito_com_permitir_http(self):
        self.assertIsNone(g.valida_url("http://192.168.1.142:8080/b.bin",
                                       permitir_http=True))

    def test_permitir_http_nao_afrouxa_as_outras_regras(self):
        # O escape hatch libera só o esquema. Comprimento e ausência de
        # esquema continuam valendo — senão ele viraria um "aceita tudo".
        self.assertIsNotNone(g.valida_url("exemplo/b.bin", permitir_http=True))
        self.assertIsNotNone(
            g.valida_url("http://" + "x" * g.MAX_URL, permitir_http=True))

    def test_cfg_com_url_http_traz_aviso(self):
        corpo = g.corpo_cfg([g.Rede("iot", "12345678")],
                            "http://192.168.1.142:8080/radares.versao",
                            "http://192.168.1.142:8080/radares.bin",
                            com_segredo=True)
        self.assertIn("SEM TLS", corpo)

    def test_cfg_com_url_https_nao_traz_aviso(self):
        corpo = g.corpo_cfg([g.Rede("iot", "12345678")],
                            "https://exemplo/radares.versao",
                            "https://exemplo/radares.bin",
                            com_segredo=True)
        self.assertNotIn("SEM TLS", corpo)
    def test_sem_esquema_e_recusado(self):
            self.assertIsNotNone(g.valida_url("exemplo/b.bin"))
    def test_url_longa_demais_e_recusada(self):
            self.assertIsNotNone(g.valida_url("https://" + "x" * g.MAX_URL))
    def test_exemplo_nao_contem_segredo(self):
        corpo = g.corpo_cfg([g.Rede("minha-casa", "S3nh4Secreta")], "https://a",
                            "https://b", com_segredo=False)
        self.assertTrue("S3nh4Secreta" not in corpo)
        self.assertTrue("minha-casa" not in corpo)
    def test_com_segredo_escreve_os_valores(self):
        corpo = g.corpo_cfg([g.Rede("casa", "S3nh4Secreta")], "https://v",
                            "https://b", com_segredo=True)
        self.assertTrue("wifi_ssid_1=casa" in corpo)
        self.assertTrue("wifi_senha_1=S3nh4Secreta" in corpo)
        self.assertTrue("url_versao=https://v" in corpo)
        self.assertTrue("url_base=https://b" in corpo)
    def test_a_numeracao_segue_a_ordem_da_lista(self):
        # A ordem é a prioridade; embaralhar aqui inverteria a preferência.
        corpo = g.corpo_cfg([g.Rede("um", "12345678"), g.Rede("dois", "12345678")],
                            "https://v", "https://b", com_segredo=True)
        self.assertTrue(corpo.index("wifi_ssid_1=um") < corpo.index("wifi_ssid_2=dois"))
    def test_limites_batem_com_o_cabecalho_cpp(self):
        h = (RAIZ / "firmware/src/nucleo/Configuracao.h").read_text(
            encoding="utf-8")

        def const(nome):
            m = re.search(rf"constexpr std::size_t {nome} = (\d+);", h)
            self.assertIsNotNone(m, f"{nome} nao encontrado em Configuracao.h")
            return int(m.group(1))

        self.assertEqual(g.MAX_REDES, const("kMaxRedes"))
        self.assertEqual(g.MAX_SSID, const("kMaxSsid"))
        self.assertEqual(g.MAX_SENHA, const("kMaxSenha"))
        self.assertEqual(g.MAX_URL, const("kMaxUrl"))

    def test_os_ajustes_padrao_batem_com_o_cabecalho_cpp(self):
        # Os dois lados escrevem o mesmo padrao em lugares diferentes: o
        # gerador no arquivo, o firmware no struct antes de ler o arquivo.
        # Divergir faria um cartao sem a chave se comportar diferente de um
        # cartao com a chave no valor "padrao".
        h = (RAIZ / "firmware/src/nucleo/Configuracao.h").read_text(
            encoding="utf-8")
        padrao = g.Ajustes()

        def campo(nome, tipo=r"std::uint8_t"):
            m = re.search(rf"{tipo} {nome} = ([\w:]+);", h)
            self.assertIsNotNone(m, f"{nome} nao encontrado em Configuracao.h")
            return m.group(1)

        self.assertEqual(str(padrao.brilho_dia), campo("brilho_dia"))
        self.assertEqual(str(padrao.brilho_noite), campo("brilho_noite"))
        # volume_buzzer nasce no maximo, pela constante.
        self.assertEqual("kVolumeMaximo", campo("volume_buzzer"))
        m = re.search(r"constexpr std::uint8_t kVolumeMaximo = (\d+);", h)
        self.assertEqual(padrao.volume_buzzer, int(m.group(1)))
        self.assertIn("ModoNoturno::Automatico",
                      campo("modo_noturno", r"ModoNoturno"))
        self.assertEqual("auto", padrao.modo_noturno)

    def test_o_nome_padrao_cabe_no_buffer_do_firmware(self):
        h = (RAIZ / "firmware/src/nucleo/Configuracao.h").read_text(
            encoding="utf-8")
        m = re.search(r"constexpr std::size_t kMaxNome = (\d+);", h)
        self.assertLessEqual(len(g.Ajustes().nome), int(m.group(1)))

    def test_os_ajustes_aparecem_no_corpo(self):
        corpo = g.corpo_cfg([], "", "", com_segredo=False,
                            ajustes=g.Ajustes(nome="fusca", brilho_dia=75,
                                              brilho_noite=15,
                                              modo_noturno="noite",
                                              volume_buzzer=50))
        for linha in ("nome=fusca", "brilho_dia=75", "brilho_noite=15",
                      "modo_noturno=noite", "volume_buzzer=50"):
            self.assertIn(linha, corpo)

    def test_os_ajustes_vem_depois_das_urls(self):
        # A gravacao pelo menu e cirurgica, mas o bloco so faz sentido
        # como bloco: o aviso de que o aparelho escreve ali precisa vir
        # antes das chaves que ele escreve.
        corpo = g.corpo_cfg([], "", "", com_segredo=False)
        self.assertLess(corpo.index("url_base="), corpo.index("nome="))
        self.assertLess(corpo.index("# --- ajustes do aparelho"),
                        corpo.index("nome="))

    def test_so_exemplo_recusa_ajustes(self):
        # Sem isto os ajustes seriam ignorados em silencio, e o gabarito
        # versionado sairia com o carro de quem rodou.
        self.assertEqual(1, g.main(["--so-exemplo", "--nome", "fusca"]))

    def test_so_exemplo_sem_ajustes_funciona(self):
        # Restaura o arquivo: se este teste o deixasse regerado, ele
        # esconderia uma defasagem que test_o_exemplo_versionado_esta_em_dia
        # deveria pegar -- hoje so a ordem alfabetica evita isso.
        alvo = RAIZ / "coruja.cfg.exemplo"
        antes = alvo.read_text(encoding="utf-8")
        try:
            self.assertEqual(0, g.main(["--so-exemplo"]))
        finally:
            alvo.write_text(antes, encoding="utf-8")

    def test_o_exemplo_versionado_esta_em_dia(self):
        # O parser em C++ lê este arquivo. Se o gerador mudar de formato e o
        # exemplo nao for regerado, os dois lados divergem em silencio.
        atual = (RAIZ / "coruja.cfg.exemplo").read_text(encoding="utf-8")
        self.assertEqual(atual, g.corpo_cfg([], "", "", com_segredo=False))


if __name__ == "__main__":
    unittest.main()
