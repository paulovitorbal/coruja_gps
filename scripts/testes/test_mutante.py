"""O arnês de mutação, rodado contra um fonte de mentira.

    python3 -m unittest discover -s scripts/testes

O `mutante.sh` reescreve um fonte de verdade e depende de devolvê-lo
intacto. Aqui ele roda num diretório temporário, com `cmake` e `ctest`
FALSOS no PATH (ver `falsos/`): o ctest falso "mata" o mutante quando acha
o texto mutado no arquivo, como uma suíte que pega a mudança. O `src` do
projeto não é tocado.

O que importa em cada caso é o que o usuário veria depois: o fonte igual,
byte a byte, ao de antes; nenhum `.mut-bak` esquecido; e um veredito que
não minta — nada de MORTO para um mutante que ninguém testou.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

TESTES = Path(__file__).resolve().parent
SCRIPTS = TESTES.parent
FALSOS = TESTES / "falsos"

ORIGINAL = "int dobra(int x) {\n    return x * 2;  // a regra\n}\n"
DE = "x * 2"
PARA = "x * 3 /*MUTADO*/"

PREFIXOS_DE_FORA = ("CORUJA_", "FALSO_")


class Mutante(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.raiz = Path(tmp.name)
        (self.raiz / "scripts").mkdir()
        shutil.copy2(SCRIPTS / "mutante.sh", self.raiz / "scripts")
        self.alvo = self.raiz / "src" / "dobra.cpp"
        self.alvo.parent.mkdir()
        self.alvo.write_text(ORIGINAL)
        self.bak = Path(f"{self.alvo}.mut-bak")
        build = self.raiz / "firmware" / "build-host"
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("")
        self.diario = self.raiz / "diario"
        self.diario.write_text("")
        self.logs = self.raiz / "logs"
        self.logs.mkdir()
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(PREFIXOS_DE_FORA)}
        self.env.update({
            "PATH": f"{FALSOS}{os.pathsep}{os.environ['PATH']}",
            "FALSO_DIARIO": str(self.diario),
            "CORUJA_LOGS": str(self.logs),
            # A suíte falsa pega o mutante: falha se achar o texto mutado.
            "FALSO_CTEST_ACAO": f"! grep -q MUTADO '{self.alvo}'",
        })

    def muta(self, de=DE, para=PARA, **extra) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(self.raiz / "scripts" / "mutante.sh"), "src/dobra.cpp",
             de, para, "rotulo"],
            env={**self.env, **extra}, capture_output=True, text=True,
            check=False, timeout=60)

    def assertIntacto(self, r: subprocess.CompletedProcess):
        self.assertEqual(self.alvo.read_text(), ORIGINAL, r.stdout + r.stderr)
        self.assertFalse(self.bak.exists(), r.stdout + r.stderr)

    def chamadas(self) -> list[str]:
        return self.diario.read_text().splitlines()

    # --- os vereditos --------------------------------------------------------

    def test_mutante_morto_e_o_fonte_volta_byte_a_byte(self):
        r = self.muta()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("morto por: Falso.Teste", r.stdout)
        self.assertIntacto(r)
        # Restaura E recompila: o build não pode ficar com o objeto mutante.
        self.assertTrue(self.chamadas()[-1].startswith("cmake --build"))

    def test_sobrevivente_e_acusado_e_o_fonte_volta(self):
        r = self.muta(FALSO_CTEST_ACAO="true")
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("SOBREVIVEU", r.stdout)
        self.assertIntacto(r)

    def test_mutante_que_nao_compila_e_morto(self):
        nao_compila = f"grep -q MUTADO '{self.alvo}' && exit 1"
        r = self.muta(FALSO_CTEST_ACAO="true",
                      FALSO_CMAKE_build_host_1=nao_compila,
                      FALSO_CMAKE_build_host_2=nao_compila)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("MORTO (nao compila)", r.stdout)
        self.assertIntacto(r)

    def test_ancora_ambigua_nao_muta(self):
        r = self.muta(de="x")
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("ANCORA RUIM", r.stdout)
        self.assertIntacto(r)

    # --- B3: o que mentia ou destruía ----------------------------------------

    def test_recusa_comecar_com_o_backup_de_uma_execucao_que_caiu(self):
        # Uma execução morta no meio deixa o MUTANTE no fonte e o ORIGINAL no
        # .mut-bak. Copiar por cima do backup apagaria a única cópia boa.
        self.bak.write_text(ORIGINAL)
        self.alvo.write_text(ORIGINAL.replace(DE, PARA))
        r = self.muta()
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertNotIn("morto", r.stdout.lower())
        self.assertEqual(self.bak.read_text(), ORIGINAL)
        self.assertEqual(self.alvo.read_text(), ORIGINAL.replace(DE, PARA))
        self.assertIn(".mut-bak", r.stderr)
        self.assertEqual(self.chamadas(), [])

    def test_suite_vermelha_sem_mutante_aborta_antes_de_mutar(self):
        # Com a suíte já vermelha, todo mutante sairia "morto".
        r = self.muta(FALSO_CTEST_ACAO="false")
        self.assertNotIn(r.returncode, (0, 2), r.stdout + r.stderr)
        self.assertNotIn("morto", r.stdout.lower())
        self.assertIn("linha de base", r.stderr)
        self.assertIntacto(r)
        self.assertEqual(sum(c.startswith("ctest") for c in self.chamadas()),
                         1)

    def test_ctrl_c_no_ctest_sai_com_130_sem_falso_morto(self):
        # O ctest morto pelo Ctrl-C devolve falha; seguir adiante imprimia
        # MORTO para um mutante que ninguém terminou de testar.
        r = self.muta(FALSO_CTEST_ACAO=(
            f"if grep -q MUTADO '{self.alvo}'; then kill -INT $PPID; false; fi"))
        self.assertEqual(r.returncode, 130, r.stdout + r.stderr)
        self.assertNotIn("morto", r.stdout.lower())
        self.assertIntacto(r)

    def test_restauracao_que_falha_nao_sai_com_o_veredito(self):
        # Pasta sem escrita: o `mv` de volta falha. O "morto" já impresso não
        # pode virar código 0, que uma passada em lote leria como sucesso.
        pasta = self.alvo.parent
        self.addCleanup(pasta.chmod, 0o755)
        r = self.muta(FALSO_CMAKE_build_host_2=f"chmod a-w '{pasta}'")
        self.assertEqual(r.returncode, 74, r.stdout + r.stderr)
        self.assertIn("NAO RESTAUREI", r.stderr)
        self.assertEqual(self.bak.read_text(), ORIGINAL)

    def test_term_no_build_restaura_o_fonte(self):
        r = self.muta(FALSO_CMAKE_build_host_2="kill -TERM $PPID")
        self.assertEqual(r.returncode, 143, r.stdout + r.stderr)
        self.assertNotIn("morto", r.stdout.lower())
        self.assertIntacto(r)


if __name__ == "__main__":
    unittest.main()
