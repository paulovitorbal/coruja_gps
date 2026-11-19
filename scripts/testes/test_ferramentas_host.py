"""As ferramentas de bancada, pelos argumentos que o usuário erra.

    python3 -m unittest discover -s scripts/testes

Elas rodam na máquina, à mão, com caminhos digitados — e era exatamente aí
que falhavam feio: um diretório no lugar do arquivo matava o programa no
alocador, sem dizer qual argumento estava errado, e um `--baud` que não é
número era ignorado em silêncio.

Os testes se pulam quando o `firmware/build-host` não foi compilado. Eles
não abrem serial nenhuma: tudo para antes disso.
"""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
BIN = RAIZ / "firmware" / "build-host" / "ferramentas"


def ferramenta(nome: str) -> Path:
    caminho = BIN / nome
    if not caminho.exists():
        raise unittest.SkipTest(
            f"{caminho} nao existe: compile com "
            "cmake --build firmware/build-host")
    return caminho


class ArgumentosRuins(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        self.pasta = self.tmp / "uma_pasta"
        self.pasta.mkdir()
        self.log = self.tmp / "viagem.log"
        self.log.write_text("utc;lat;lon\n")

    def roda(self, *args) -> subprocess.CompletedProcess:
        return subprocess.run([str(a) for a in args], capture_output=True,
                              text=True, check=False, timeout=60)

    def assertNaoMorreu(self, r: subprocess.CompletedProcess):
        # Código negativo é sinal: o estouro do alocador chegava como -6.
        self.assertGreater(r.returncode, 0, r.stdout + r.stderr)
        self.assertLess(r.returncode, 128, r.stdout + r.stderr)

    def test_monitor_com_pasta_no_lugar_da_base_explica_em_vez_de_morrer(self):
        r = self.roda(ferramenta("monitor_alertas"), "--base", self.pasta)
        self.assertNaoMorreu(r)
        self.assertIn("nao e um arquivo comum", r.stderr)
        self.assertIn(str(self.pasta), r.stderr)

    def test_confere_viagem_com_pasta_no_lugar_da_base(self):
        r = self.roda(ferramenta("confere_viagem"), self.log, self.pasta)
        self.assertNaoMorreu(r)
        self.assertIn("nao e um arquivo comum", r.stderr)

    def test_previa_com_pasta_no_lugar_da_base(self):
        r = self.roda(ferramenta("previa_produto"), "--base", self.pasta)
        self.assertNaoMorreu(r)
        self.assertIn("nao e um arquivo comum", r.stderr)

    def test_base_num_CANO_nao_estoura_o_alocador(self):
        # ⚠️ **O caso que de fato quebrava.** Num FIFO -- `--base <(zcat
        # base.gz)`, que é como se alimenta uma ferramenta de bancada sem
        # arquivo temporário -- o `fseek` falha e o `ftell` devolve −1. O
        # `static_cast<std::size_t>` disso é 18 exabytes, e o programa
        # morria em `std::length_error: vector`, sem dizer qual argumento
        # estava errado. Conferido em 19/11/2026 com o binário anterior.
        cano = self.tmp / "cano"
        os.mkfifo(cano, 0o600)
        escritor = subprocess.Popen(
            [sys.executable, "-I", "-c",
             f"open({str(cano)!r}, 'wb').write(b'nao e uma base')"])
        self.addCleanup(escritor.wait)
        r = self.roda(ferramenta("monitor_alertas"), "--base", cano)
        self.assertNaoMorreu(r)
        self.assertNotIn("length_error", r.stderr)
        self.assertIn("nao e um arquivo comum", r.stderr)

    def test_base_que_nao_existe_diz_o_nome_e_o_motivo(self):
        ausente = self.tmp / "nao_existe.bin"
        r = self.roda(ferramenta("monitor_alertas"), "--base", ausente)
        self.assertNaoMorreu(r)
        self.assertIn(str(ausente), r.stderr)
        self.assertIn("No such file", r.stderr)

    def test_baud_que_nao_e_numero_e_recusado_em_vez_de_ignorado(self):
        # Era `strtol(..., nullptr, 10)`: `abc` virava 0, que quer dizer
        # "nao configure a porta". O monitor seguia na velocidade errada.
        r = self.roda(ferramenta("monitor_alertas"), "--baud", "abc")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("--baud", r.stderr)

    def test_baud_com_lixo_grudado_tambem_e_recusado(self):
        r = self.roda(ferramenta("monitor_alertas"), "--baud", "9600x")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("--baud", r.stderr)

    def test_baud_valido_passa_da_conferencia_de_argumentos(self):
        # Chega a tentar a serial, que no caminho inventado não abre: é o
        # erro SEGUINTE, e é o que prova que o número foi aceito.
        r = self.roda(ferramenta("monitor_alertas"), "--baud", "9600",
                      "--base", self.tmp / "nao_existe.bin")
        self.assertNaoMorreu(r)
        self.assertNotIn("--baud", r.stderr)


if __name__ == "__main__":
    unittest.main()
