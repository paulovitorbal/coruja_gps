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

import io
import os
import re
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))

import gera_config as g  # noqa: E402


class OQueOFirmwareLe(unittest.TestCase):
    """O valor tem de chegar ao aparelho como foi digitado.

    O firmware mede em BYTES (`copia` no LeitorConfig.cpp) e apara espaco,
    tabulacao e quebra de linha nas duas pontas do valor (`apara` no
    LinhaConfig.cpp). Um valor que passa aqui e muda la e uma rede que
    nunca conecta, com o arquivo parecendo certo.
    """

    def test_ssid_com_acento_e_medido_em_bytes(self):
        # 32 caracteres, 33 bytes: o firmware recusa.
        erro = g.valida_rede("ç" + "a" * (g.MAX_SSID - 1), "12345678")
        self.assertTrue(erro)
        self.assertIn("bytes", erro)

    def test_senha_com_acento_e_medida_em_bytes(self):
        erro = g.valida_rede("casa", "é" * 32)       # 32 caracteres, 64 bytes
        self.assertTrue(erro)
        self.assertIn("senha", erro)

    def test_url_e_medida_em_bytes(self):
        url = "https://" + "ã" * ((g.MAX_URL - 8) // 2 + 1)
        self.assertLessEqual(len(url), g.MAX_URL)
        self.assertIsNotNone(g.valida_url(url))

    def test_nome_e_medido_em_bytes(self):
        nome = "ã" * (g.MAX_NOME // 2 + 1)
        self.assertLessEqual(len(nome), g.MAX_NOME)
        self.assertIsNotNone(g.valida_ajustes(g.Ajustes(nome=nome)))

    def test_senha_com_espaco_nas_pontas_e_recusada(self):
        # getpass nao apara; o firmware apara. "minha senha " viraria
        # "minha senha" no aparelho, e a rede recusaria.
        for senha in (" 12345678", "12345678 ", "12345678\t"):
            erro = g.valida_rede("casa", senha)
            self.assertTrue(erro, repr(senha))
            self.assertIn("pontas", erro)

    def test_espaco_no_meio_da_senha_vale(self):
        self.assertIsNone(g.valida_rede("casa", "minha senha boa"))

    def test_nome_com_quebra_de_linha_e_recusado(self):
        # "--nome $'fusca\\nled=false'" escreveria uma chave a mais no
        # arquivo, e o aparelho a obedeceria.
        erro = g.valida_ajustes(g.Ajustes(nome="fusca\nled=false"))
        self.assertTrue(erro)
        self.assertIn("nome", erro)

    def test_main_recusa_nome_com_quebra_de_linha_antes_de_perguntar(self):
        def nao_pergunte(_rotulo=""):
            raise AssertionError("perguntou a rede antes de recusar o nome")
        with redirect_stderr(io.StringIO()) as erro, \
                mock.patch("builtins.input", nao_pergunte):
            self.assertEqual(1, g.main(["--nome", "a\nb"]))
        self.assertIn("nome", erro.getvalue())


class Grava(unittest.TestCase):
    def test_grava_o_conteudo_com_permissao_600(self):
        with tempfile.TemporaryDirectory() as tmp, \
                redirect_stdout(io.StringIO()):
            alvo = Path(tmp) / "cartao" / g.CFG
            g.grava(alvo, "wifi_senha_1=S3nh4Secreta\n", modo=0o600)
            self.assertEqual(alvo.read_text(encoding="utf-8"),
                             "wifi_senha_1=S3nh4Secreta\n")
            self.assertEqual(stat.S_IMODE(alvo.stat().st_mode), 0o600)

    def test_regravar_um_arquivo_aberto_a_todos_fecha_a_permissao(self):
        # `os.open` com modo so vale na CRIACAO. Um coruja.cfg que ja
        # existia com 644 continuaria legivel por todos com a senha nova.
        with tempfile.TemporaryDirectory() as tmp, \
                redirect_stdout(io.StringIO()):
            alvo = Path(tmp) / g.CFG
            alvo.write_text("velho, e bem mais comprido que o novo\n")
            os.chmod(alvo, 0o644)
            g.grava(alvo, "novo\n", modo=0o600)
            self.assertEqual(alvo.read_text(encoding="utf-8"), "novo\n")
            self.assertEqual(stat.S_IMODE(alvo.stat().st_mode), 0o600)

    def test_o_segredo_nunca_existe_com_permissao_aberta(self):
        # Escrever e DEPOIS dar chmod deixava uma janela em que a senha
        # estava no disco legivel por todos. Confere-se a permissao no
        # instante em que o conteudo e escrito.
        vistos = []
        escreve_de_verdade = os.write

        def espia(fd, dados):
            vistos.append(stat.S_IMODE(os.fstat(fd).st_mode))
            return escreve_de_verdade(fd, dados)

        with tempfile.TemporaryDirectory() as tmp, \
                redirect_stdout(io.StringIO()), \
                mock.patch.object(g.os, "write", espia):
            g.grava(Path(tmp) / g.CFG, "wifi_senha_1=S3nh4Secreta\n",
                    modo=0o600)
        self.assertTrue(vistos, "a gravacao nao passou por os.write")
        self.assertEqual(set(vistos), {0o600})

    def test_main_grava_o_cartao_enxuto_e_o_exemplo(self):
        # O caminho feliz inteiro, como quem roda: uma rede, duas URLs.
        respostas = iter(["casa", "", "https://x/v", "https://x/b"])
        with tempfile.TemporaryDirectory() as raiz, \
                tempfile.TemporaryDirectory() as cartao, \
                mock.patch.object(g, "RAIZ", Path(raiz)), \
                mock.patch("builtins.input", lambda _r="": next(respostas)), \
                mock.patch.object(g.getpass, "getpass",
                                  lambda _r="": "S3nh4Secreta"), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(0, g.main(["--destino", cartao]))
            cfg = Path(cartao) / g.CFG
            texto = cfg.read_text(encoding="utf-8")
            modo = stat.S_IMODE(cfg.stat().st_mode)
            exemplo = (Path(raiz) / g.EXEMPLO).read_text(encoding="utf-8")
        self.assertEqual(modo, 0o600)
        self.assertIn("wifi_ssid_1=casa\n", texto)
        self.assertIn("wifi_senha_1=S3nh4Secreta\n", texto)
        self.assertIn("url_versao=https://x/v\n", texto)
        self.assertNotIn("#", texto)
        self.assertNotIn("S3nh4Secreta", exemplo)
        self.assertEqual(exemplo, g.corpo_cfg([], "", "", com_segredo=False))


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
        # A chave do arquivo e `led`; o campo do struct e `led_ligado`. Os
        # dois nomes diferem de proposito -- o arquivo e para quem edita a
        # mao -- e e justamente por isso que o par precisa de teste.
        self.assertEqual(padrao.led, campo("led_ligado", r"bool"))
        self.assertEqual(str(padrao.led_dia), campo("led_dia"))
        self.assertEqual(str(padrao.led_noite), campo("led_noite"))
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
                                              led="false", led_dia=50,
                                              led_noite=5))
        for linha in ("nome=fusca", "brilho_dia=75", "brilho_noite=15",
                      "modo_noturno=noite", "led=false", "led_dia=50",
                      "led_noite=5"):
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
        # Num diretorio temporario, e nao no repositorio: regravar o
        # exemplo versionado esconderia uma defasagem que
        # test_o_exemplo_versionado_esta_em_dia deveria pegar -- antes, so
        # a ordem alfabetica dos testes evitava isso.
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(g, "RAIZ", Path(tmp)), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(0, g.main(["--so-exemplo"]))
            escrito = (Path(tmp) / g.EXEMPLO).read_text(encoding="utf-8")
        self.assertEqual(escrito, g.corpo_cfg([], "", "", com_segredo=False))

    def test_so_exemplo_respeita_o_destino(self):
        # Antes o --destino era ignorado em silencio e o exemplo ia para o
        # repositorio, por cima do versionado.
        with tempfile.TemporaryDirectory() as raiz, \
                tempfile.TemporaryDirectory() as cartao, \
                mock.patch.object(g, "RAIZ", Path(raiz)), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(0, g.main(["--so-exemplo", "--destino", cartao]))
            self.assertTrue((Path(cartao) / g.EXEMPLO).exists())
            self.assertFalse((Path(raiz) / g.EXEMPLO).exists())

    def test_o_volume_do_buzzer_nao_volta(self):
        """Saiu do firmware em 19/10/2026; o gerador escreveu por mais 23 dias.

        O parser **ainda le** a chave, de proposito, para nao reclamar dos
        cartoes que ja existem -- e e isso que torna a volta dela invisivel:
        um cartao com `volume_buzzer` carrega limpo e nada acusa. Por isso o
        teste olha a LINHA, e nao a palavra: a nota de despedida cita o nome
        dela no texto, e tem de continuar citando.
        """
        corpo = g.corpo_cfg([], "", "", com_segredo=False)
        ativas = [l for l in corpo.splitlines()
                  if l.strip() and not l.lstrip().startswith("#")]
        self.assertEqual([], [l for l in ativas
                              if l.startswith("volume_buzzer")])
        self.assertIn("volume_buzzer", corpo)

    def test_os_limites_dos_ajustes_batem_com_o_firmware(self):
        """Mesma amarra do test_limites_batem_com_o_cabecalho_cpp.

        Estes tres moram em arquivos diferentes do `Configuracao.h`, e um
        numero repetido a mao aqui divergiria pela mesma razao que o
        `volume_buzzer` divergiu.
        """
        def const(caminho, nome, tipo):
            texto = (RAIZ / caminho).read_text(encoding="utf-8")
            m = re.search(rf"constexpr {tipo} {nome} = (\d+);", texto)
            self.assertIsNotNone(m, f"{nome} sumiu de {caminho}")
            return int(m.group(1))

        self.assertEqual(
            g.MAX_NOME,
            const("firmware/src/nucleo/Configuracao.h", "kMaxNome",
                  r"std::size_t"))
        self.assertEqual(
            g.LED_MIN,
            const("firmware/src/led/LedRegulado.h", "kIntensidadeLedMinPct",
                  r"std::uint8_t"))
        self.assertEqual(
            g.BRILHO_MIN,
            const("firmware/src/display/Brilho.h", "kBrilhoMinimoPct",
                  r"std::uint8_t"))
        # 21 passos cobrindo 0..100 so fecham com passo 5.
        passos = const("firmware/src/display/Brilho.h", "kPassosBrilho",
                       r"std::size_t")
        self.assertEqual(g.PASSO_AJUSTE, g.BRILHO_MAX // (passos - 1))

    def test_os_ajustes_padrao_passam_na_validacao(self):
        self.assertIsNone(g.valida_ajustes(g.Ajustes()))

    def test_brilho_zero_e_aceito(self):
        # Zero nao apaga a tela: e o mais escuro que o painel mostra.
        self.assertIsNone(g.valida_ajustes(g.Ajustes(brilho_noite=0)))

    def test_brilho_fora_do_passo_de_cinco_e_recusado(self):
        # O firmware RECUSA, nao arredonda: a chave e descartada e o padrao
        # fica valendo, com o cartao parecendo certo.
        erro = g.valida_ajustes(g.Ajustes(brilho_dia=73))
        self.assertTrue(erro)
        self.assertIn("brilho_dia", erro)
        self.assertIn("5", erro)

    def test_brilho_acima_do_teto_e_recusado(self):
        self.assertIsNotNone(g.valida_ajustes(g.Ajustes(brilho_dia=105)))

    def test_led_no_piso_e_aceito(self):
        self.assertIsNone(g.valida_ajustes(g.Ajustes(led_noite=g.LED_MIN)))

    def test_led_abaixo_do_piso_e_recusado(self):
        # Apagar o LED e trabalho da chave `led`. Dois ajustes capazes de
        # apagar disputariam o mesmo resultado.
        erro = g.valida_ajustes(g.Ajustes(led_noite=0))
        self.assertTrue(erro)
        self.assertIn("led_noite", erro)

    def test_nome_no_limite_passa(self):
        self.assertIsNone(g.valida_ajustes(g.Ajustes(nome="z" * g.MAX_NOME)))

    def test_nome_longo_demais_e_recusado(self):
        # O parser recusa o valor longo em vez de truncar, e o aparelho fica
        # sem nome na tela -- sintoma que nao lembra a causa.
        erro = g.valida_ajustes(g.Ajustes(nome="z" * (g.MAX_NOME + 1)))
        self.assertTrue(erro)
        self.assertIn("nome", erro)

    def test_nome_vazio_e_recusado(self):
        self.assertIsNotNone(g.valida_ajustes(g.Ajustes(nome="")))

    def test_main_recusa_ajuste_invalido_antes_de_perguntar(self):
        # Se a validacao viesse depois, este teste travaria no input() --
        # e quem rodasse digitaria a rede inteira para so entao ser recusado.
        self.assertEqual(1, g.main(["--led-dia", "3"]))

    def test_o_exemplo_versionado_esta_em_dia(self):
        # O parser em C++ lê este arquivo. Se o gerador mudar de formato e o
        # exemplo nao for regerado, os dois lados divergem em silencio.
        atual = (RAIZ / "coruja.cfg.exemplo").read_text(encoding="utf-8")
        self.assertEqual(atual, g.corpo_cfg([], "", "", com_segredo=False))


class CabeNoAparelho(unittest.TestCase):
    """O arquivo gerado tem de caber no buffer que o firmware reserva.

    ⚠️ **Este teste existe por causa de um defeito real**, de 2026-10-06: o
    leitor do `main.cpp` reservava 2048 bytes e o gravador, 8192. O gerador
    foi passando dos 2048 a cada funcionalidade nova -- cada uma acrescentava
    um bloco de comentário -- e nada ligava os dois lados.

    O sintoma não parecia com a causa: `sem configuração utilizável` no OTA e
    o aparelho sem nome na tela de informação, com um arquivo que lia
    perfeitamente em qualquer outra ferramenta.

    O número é lido do CÓDIGO, não repetido aqui. Repetido, ele divergiria
    pela mesma razão que os dois buffers divergiram.
    """

    def limite(self) -> int:
        texto = (RAIZ / "firmware/src/nucleo/GravadorConfig.h").read_text()
        m = re.search(r"kMaxTextoCfg\s*=\s*(\d+)", texto)
        self.assertIsNotNone(m, "kMaxTextoCfg sumiu do GravadorConfig.h")
        return int(m.group(1))

    def test_o_leitor_e_o_gravador_usam_a_MESMA_constante(self):
        # A correção estrutural: enquanto forem dois números, voltam a
        # divergir.
        main = (RAIZ / "firmware/src/main.cpp").read_text()
        self.assertIn("kTamBufferConfig = coruja::kMaxTextoCfg", main)

    def test_o_arquivo_do_cartao_cabe_com_folga(self):
        corpo = g.enxuga(g.corpo_cfg(
            [g.Rede("x" * g.MAX_SSID, "y" * 63)] * g.MAX_REDES,
            "https://" + "u" * 150, "https://" + "b" * 150,
            com_segredo=True))
        # Pior caso: todas as redes, SSID e senha no limite, URLs no limite.
        self.assertLess(len(corpo.encode()), self.limite(),
                        "o que vai ao cartao nao cabe no buffer do firmware")

    def test_o_exemplo_versionado_tambem_cabe(self):
        # Alguém pode copiá-lo para o cartão e editar à mão -- é o que o
        # próprio README sugere.
        exemplo = (RAIZ / "coruja.cfg.exemplo").read_bytes()
        self.assertLess(len(exemplo), self.limite())

    def test_enxugar_tira_comentario_e_preserva_chave(self):
        cheio = g.corpo_cfg([g.Rede("casa", "12345678")],
                            "https://x/v", "https://x/b", com_segredo=True)
        magro = g.enxuga(cheio)
        self.assertNotIn("#", magro)
        self.assertLess(len(magro), len(cheio) // 4,
                        "enxugar mal compensa se nao corta de verdade")
        for chave in ("wifi_ssid_1=casa", "wifi_senha_1=12345678",
                      "url_versao=https://x/v", "url_base=https://x/b",
                      "nome=", "brilho_dia=", "led=", "led_dia="):
            self.assertIn(chave, magro)

    def test_enxugar_nao_deixa_linha_em_branco(self):
        magro = g.enxuga(g.corpo_cfg([], "", "", com_segredo=False))
        self.assertTrue(all(l.strip() for l in magro.splitlines()))


if __name__ == "__main__":
    unittest.main()