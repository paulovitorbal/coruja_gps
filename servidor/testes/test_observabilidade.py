"""A pilha de observabilidade, conferida sem subir container nenhum.

    python3 -m unittest discover -s servidor/testes

Estes testes não medem o Loki: medem as DECISÕES que a configuração carrega e
que, erradas, só aparecem em produção — e em duas delas aparecem como silêncio,
que é o pior jeito de um sistema de log falhar.

Cada uma foi achada subindo a pilha de verdade em 14/10/2026, e está aqui para
não voltar:

* a expressão que lê o diário do aparelho precisa casar com as **quatro**
  formas de linha que o firmware já produziu, inclusive a que tem `idexec`
  começando por dígito;
* a data do `schema_config` é um **piso**: tudo anterior a ela é recusado com
  `500`, e o diário chega com carimbos antigos, de dias atrás;
* `reject_old_samples` tem de estar desligado, pela mesma razão;
* a senha do Grafana não pode estar no arquivo versionado — ele vai para um
  repositório público.
"""

import re
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
PASTA = RAIZ / "observabilidade"
ALLOY = PASTA / "alloy.alloy"
LOKI = PASTA / "loki.yml"
COMPOSE = PASTA / "docker-compose.yml"


def expressao_do_diario() -> re.Pattern:
    """A mesma expressão que o Alloy usa, tirada do arquivo dele.

    Copiá-la para cá faria o teste passar enquanto o coletor falha — que é
    exatamente o defeito que ele existe para pegar.
    """
    texto = ALLOY.read_text(encoding="utf-8")
    achado = re.search(r'expression = "(\^\(\?:\(\?P<carimbo>.*?)"\n', texto)
    assert achado is not None, "não achei a expressão do diário no alloy.alloy"
    # No arquivo do Alloy as contrabarras estão escapadas para a string dele.
    return re.compile(achado.group(1).replace("\\\\", "\\"))


class ExpressaoDoDiario(unittest.TestCase):
    """As formas estão em formato_logs.md §3, e todas ainda existem em cartão."""

    def test_a_forma_corrente_sai_inteira(self):
        m = expressao_do_diario().match(
            "2026-10-14T11:42:07Z EGMHMD4E [INFO] vigia: base 18322 pts | fix sim"
        )
        self.assertIsNotNone(m)
        self.assertEqual(m.group("carimbo"), "2026-10-14T11:42:07Z")
        self.assertEqual(m.group("idexec"), "EGMHMD4E")
        self.assertEqual(m.group("nivel"), "INFO")
        self.assertEqual(m.group("origem"), "vigia")

    def test_antes_de_o_relogio_ser_acertado(self):
        # O RP2350 não tem bateria no relógio: TODA ligação começa assim.
        m = expressao_do_diario().match(
            "+00003412 EGMHMD4E [INFO] sd: driver do cartao iniciado"
        )
        self.assertIsNotNone(m)
        self.assertEqual(m.group("carimbo"), "+00003412")
        self.assertEqual(m.group("idexec"), "EGMHMD4E")

    def test_idexec_que_comeca_por_digito_nao_vira_carimbo(self):
        # ⚠️ O alfabeto do idexec inclui 2 a 9. Uma expressão que dissesse
        # apenas "o primeiro campo é o carimbo" quebraria em ~25% das
        # ligações -- e quebraria rotulando errado, não falhando.
        m = expressao_do_diario().match("2026-10-14T11:47:07Z 4EGMHMD4 [INFO] temp: 47.3")
        self.assertIsNotNone(m)
        self.assertEqual(m.group("idexec"), "4EGMHMD4")
        self.assertEqual(m.group("origem"), "temp")

    def test_as_formas_antigas_continuam_legiveis(self):
        # v0.2.2 a v0.2.9: sem carimbo. Até v0.2.1: sem idexec também. Há
        # cartão com as duas, e o histórico do servidor é append-only.
        sem_carimbo = expressao_do_diario().match("9XKLMNPQ [WARN] http: 401")
        self.assertIsNotNone(sem_carimbo)
        self.assertIsNone(sem_carimbo.group("carimbo"))
        self.assertEqual(sem_carimbo.group("idexec"), "9XKLMNPQ")

        crua = expressao_do_diario().match("[ERRO] sd: cartao ausente")
        self.assertIsNotNone(crua)
        self.assertIsNone(crua.group("idexec"))
        self.assertEqual(crua.group("nivel"), "ERRO")

    def test_os_quatro_niveis_do_firmware_sao_reconhecidos(self):
        for nivel in ("DEBUG", "INFO", "WARN", "ERRO"):
            with self.subTest(nivel=nivel):
                m = expressao_do_diario().match(f"[{nivel}] app: x")
                self.assertIsNotNone(m, f"nivel {nivel} nao casou")

    def test_linha_que_nao_e_do_diario_nao_ganha_rotulo(self):
        # Rotular errado é pior que não rotular: a consulta por `nivel`
        # passaria a devolver linha que ninguém classificou.
        self.assertIsNone(expressao_do_diario().match("linha solta qualquer"))
        self.assertIsNone(expressao_do_diario().match("[INFOS] sd: nivel que nao existe"))


class ConfiguracaoDoLoki(unittest.TestCase):

    def test_o_piso_do_esquema_e_anterior_ao_projeto(self):
        # 🔴 Medido: com o piso em 2026-10-01, o log do container da API, de
        # 20/09, voltou catorze vezes com `no schema config found for time`.
        # O diário do aparelho é pior: ele acumula no cartão e sobe dias
        # depois, com os carimbos do dia em que aconteceu.
        texto = LOKI.read_text(encoding="utf-8")
        datas = re.findall(r"^\s*- from: (\d{4})-\d{2}-\d{2}", texto, re.M)
        self.assertTrue(datas, "nenhum `from:` no schema_config")
        for ano in datas:
            self.assertLessEqual(
                int(ano), 2025,
                "o piso do esquema tem de ser anterior a qualquer dado deste "
                "projeto: o que for mais antigo que ele e RECUSADO com 500")

    def test_amostra_antiga_nao_pode_ser_recusada(self):
        texto = LOKI.read_text(encoding="utf-8")
        self.assertTrue(
            re.search(r"^\s*reject_old_samples:\s*false\s*$", texto, re.M),
            "com a recusa ligada, uma remessa de viagens antigas e descartada "
            "na ingestao -- sem erro em lugar nenhum")

    def test_a_janela_de_atraso_cobre_semanas(self):
        """🔴 O descarte mais traiçoeiro dos dois, e as vezes SILENCIOSO.

        A janela de aceitacao de linha atrasada e METADE do `max_chunk_age`,
        e o padrao de 2 h dava uma hora -- o que descarta praticamente todo o
        diario do aparelho, que chega dias depois do que descreve. Medido em
        14/10/2026: uma linha de nove dias atras nao chegava, e nem sempre
        havia erro no coletor.
        """
        texto = LOKI.read_text(encoding="utf-8")
        achado = re.search(r"^\s*max_chunk_age:\s*(\d+)h\s*$", texto, re.M)
        self.assertIsNotNone(
            achado, "sem `max_chunk_age`, o Loki so aceita a ultima hora")
        # Metade disso e a janela. Uma semana de atraso e rotina: o aparelho
        # so manda quando o motorista lembra.
        self.assertGreaterEqual(
            int(achado.group(1)) // 2, 24 * 14,
            "a janela (metade do max_chunk_age) tem de cobrir semanas")

    def test_o_idexec_nao_vira_rotulo(self):
        # São 8 caracteres sorteados a cada ligação. Como rótulo, cada boot
        # abriria fluxos próprios no índice.
        self.assertIn("allow_structured_metadata: true",
                      LOKI.read_text(encoding="utf-8"))
        alloy = ALLOY.read_text(encoding="utf-8")
        metadado = alloy.index("stage.structured_metadata")
        self.assertIn("idexec", alloy[metadado:metadado + 200])

    def test_nao_manda_telemetria_para_fora(self):
        # O que esta base guarda e onde alguem esteve e quando.
        self.assertIn("reporting_enabled: false", LOKI.read_text(encoding="utf-8"))


class Compose(unittest.TestCase):

    def test_nenhum_servico_fica_sem_teto_de_memoria(self):
        # O z800 tem ~3 GB livres e a API precisa deles. Sem teto, um pico de
        # ingestao empurra para a troca justamente o que se quer observar.
        texto = COMPOSE.read_text(encoding="utf-8")
        # Só o bloco `services:`; abaixo dele vem `volumes:`, com nomes na
        # mesma indentação -- contá-los daria um total que fecha por acaso.
        bloco = texto[texto.index("services:"):texto.index("\nvolumes:")]
        servicos = re.findall(r"^  (\w[\w-]*):$", bloco, re.M)
        self.assertEqual(sorted(servicos), ["alloy", "grafana", "loki"])
        self.assertEqual(len(re.findall(r"^\s+mem_limit:", bloco, re.M)),
                         len(servicos),
                         "todo servico desta pilha precisa de mem_limit")

    def test_a_senha_do_grafana_nao_esta_versionada(self):
        # Este arquivo vive num repositorio PUBLICO.
        texto = COMPOSE.read_text(encoding="utf-8")
        self.assertIn("GRAFANA_SENHA", texto)
        self.assertNotRegex(
            texto, r"GF_SECURITY_ADMIN_PASSWORD:\s*\"?[A-Za-z0-9]",
            "senha embutida no compose")
        # `:?` faz o compose FALHAR sem a variavel, em vez de subir com um
        # padrao que ninguem troca depois.
        self.assertIn("${GRAFANA_SENHA:?", texto)

    def test_o_loki_nao_e_publicado_na_rede(self):
        # Sem senha, quem alcancasse a porta leria tudo que o aparelho
        # registrou. Quem consulta e o Grafana, pela rede interna.
        texto = COMPOSE.read_text(encoding="utf-8")
        bloco = texto[texto.index("  loki:"):texto.index("  alloy:")]
        self.assertNotIn("ports:", bloco)

    def test_so_os_containers_do_coruja_sao_coletados(self):
        """A maquina que hospeda a API roda uma duzia de coisas alheias.

        Medido na implantacao: sem o filtro entraram linhas de 2022 do
        `mysql` e do `portainer` -- ruido, e ainda por cima recusadas pelo
        piso do esquema e reenviadas em laco.
        """
        alloy = ALLOY.read_text(encoding="utf-8")
        bloco = alloy[alloy.index('discovery.relabel "containers"'):]
        bloco = bloco[:bloco.index("loki.source.docker")]
        self.assertIn('action        = "keep"', bloco,
                      "sem um `keep`, o coletor leva TODO container da maquina")
        self.assertIn('regex         = "/coruja-.*"', bloco)

    def test_o_que_o_aparelho_mandou_entra_somente_leitura(self):
        texto = COMPOSE.read_text(encoding="utf-8")
        self.assertIn("../recebidos:/recebidos:ro", texto)

    def test_todo_arquivo_montado_existe(self):
        # Mesmo espirito do test_imagem: um caminho que envelhece em silencio
        # vira container em laco de reinicio, e depois do push.
        texto = COMPOSE.read_text(encoding="utf-8")
        for origem in re.findall(r"^\s+- (\./[^:]+):", texto, re.M):
            with self.subTest(origem=origem):
                self.assertTrue((PASTA / origem).exists(),
                                f"{origem} montado no compose e nao existe")


if __name__ == "__main__":
    unittest.main()
