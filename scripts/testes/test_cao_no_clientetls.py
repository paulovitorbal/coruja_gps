"""As refeicoes do cao dentro de `ClienteTls::conversa` -- por TEXTO.

⚠️ Este teste e fragil DE PROPOSITO, e existe porque nao ha como ser melhor:
o `ClienteTls` usa cyw43, lwIP e mbedTLS, e so compila para o Pico -- nenhum
teste do host o executa. A revisao independente do watchdog achou que a
correcao do defeito mais grave (o cao mordendo um Wi-Fi fraco e vivo) NAO tinha
protecao nenhuma: quem apagasse uma destas linhas trazia o defeito de volta com
todos os testes verdes.

O defeito era este: DNS, handshake TLS e espera da resposta tem ate 15 s de
inatividade cada, e elas se somam; o cao morde com 16,5 s. Nada alimentava o
cao por dentro, e um enlace a -88 dBm (o de 09/10/2026) levava 17 s sem
nenhum travamento.

Aqui se confere, no texto do fonte sem os comentarios, que as tres refeicoes
estao no lugar certo:

1. depois da chamada de `cyw43_arch_poll()` no laco principal -- DEPOIS,
   porque o que o cao pega e um travamento DENTRO dela;
2. antes do primeiro `log.error` do ramo de tempo esgotado, que forca a
   descarga no cartao;
3. depois que a resolucao de DNS termina.

Quando um teste de verdade for possivel (a espera extraida para um template,
como a do DNS), este deve morrer.
"""
import re
import unittest
from pathlib import Path

FONTE = (Path(__file__).resolve().parents[2]
         / "firmware" / "src" / "rede" / "ClienteTls.cpp")


def sem_comentarios(texto: str) -> str:
    texto = re.sub(r"/\*.*?\*/", "", texto, flags=re.S)
    return re.sub(r"//[^\n]*", "", texto)


def bloco(texto: str, abertura: str) -> str:
    """O corpo entre as chaves que seguem `abertura`."""
    ini = texto.index(abertura)
    i = texto.index("{", ini)
    prof = 0
    for j in range(i, len(texto)):
        prof += texto[j] == "{"
        prof -= texto[j] == "}"
        if prof == 0:
            return texto[i + 1:j]
    raise AssertionError(f"chave aberta em {abertura!r}")


class RefeicoesDoCaoNoClienteTls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.codigo = sem_comentarios(FONTE.read_text(encoding="utf-8"))
        cls.laco = bloco(cls.codigo, "while (!pedido.terminou)")

    def test_o_laco_alimenta_o_cao_DEPOIS_do_poll(self):
        poll = self.laco.index("cyw43_arch_poll();")
        depois = self.laco[poll:]
        self.assertIn(
            "cao->alimenta()", depois,
            "o laco de conversa nao alimenta o cao depois de "
            "cyw43_arch_poll(): DNS + handshake + resposta, cada fase dentro "
            "dos seus 15 s, voltam a somar mais que os 16,5 s do cao")

    def test_a_refeicao_vem_depois_do_poll_e_nao_antes(self):
        # Alimentar ANTES do poll deixaria o cao saciado mesmo com a propria
        # chamada travada: ela e justamente o que o cao existe para pegar.
        poll = self.laco.index("cyw43_arch_poll();")
        antes = self.laco[:poll]
        # Fora do ramo de tempo esgotado (que alimenta de proposito), nao
        # pode haver refeicao entre o inicio do laco e o poll.
        antes_sem_expiracao = antes.replace(
            bloco(antes, "if (prazo.expirou(agora))"), "")
        self.assertNotIn("cao->alimenta()", antes_sem_expiracao)

    def test_o_tempo_esgotado_alimenta_antes_de_registrar(self):
        ramo = bloco(self.laco, "if (prazo.expirou(agora))")
        self.assertIn("cao->alimenta()", ramo)
        self.assertLess(
            ramo.index("cao->alimenta()"), ramo.index("log.error("),
            "o log.error do tempo esgotado forca a descarga no cartao: sem "
            "refeicao antes, um cartao lento morde no registro da desistencia")

    def test_a_resolucao_do_dns_alimenta_ao_terminar(self):
        # A refeicao logo depois da fase de DNS, antes do connect e do
        # handshake: a fase seguinte nao herda o tempo da anterior.
        marca = self.codigo.index("cao->alimenta()")
        antes_do_laco = self.codigo[:self.codigo.index("while (!pedido.terminou)")]
        self.assertGreaterEqual(
            antes_do_laco.count("cao->alimenta()"), 1,
            "nenhuma refeicao entre o fim do DNS e o laco principal")
        self.assertGreater(marca, 0)


if __name__ == "__main__":
    unittest.main()
