"""Testes do gerador de configuração (regra 1).

Rode com:  python3 -m unittest discover -s scripts/testes -v

Usa `unittest` e não pytest, ao contrário da regra global de Python. O motivo
é local: todo o código Python deste projeto é **stdlib-only** por escolha — o
`baixa_maparadar.py` declara isso no topo — e exigir uma dependência só para
testar contrariaria a razão original. A troca custa pouco: `unittest` roda em
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
        h = (RAIZ / "firmware/src/nucleo/Configuracao.h").read_text(encoding="utf-8")

        def const(nome: str) -> int:
            m = re.search(rf"constexpr std::size_t {nome} = (\d+);", h)
            assert m, f"{nome} não encontrado em Configuracao.h"
            return int(m.group(1))

        self.assertTrue(g.MAX_REDES == const("kMaxRedes"))
        self.assertTrue(g.MAX_SSID == const("kMaxSsid"))
        self.assertTrue(g.MAX_SENHA == const("kMaxSenha"))
        self.assertTrue(g.MAX_URL == const("kMaxUrl"))

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

    def test_o_exemplo_versionado_esta_em_dia(self):
        # O parser em C++ lê este arquivo. Se o gerador mudar de formato e o
        # exemplo nao for regerado, os dois lados divergem em silencio.
        atual = (RAIZ / "coruja.cfg.exemplo").read_text(encoding="utf-8")
        self.assertEqual(atual, g.corpo_cfg([], "", "", com_segredo=False))


if __name__ == "__main__":
    unittest.main()
