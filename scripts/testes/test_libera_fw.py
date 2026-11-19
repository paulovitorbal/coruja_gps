"""O pipeline de liberação, rodado de verdade contra um repositório de mentira.

    python3 -m unittest discover -s scripts/testes

O `libera_fw.sh` não tinha teste nenhum, e a ordem das etapas é exatamente o
que já falhou em produção (o incidente de 2026-10-04). Aqui ele roda inteiro,
num repositório temporário com um remoto `--bare` local, e com `cmake`,
`ctest`, `gh`, `picotool` e `docker` FALSOS no PATH (ver `falsos/`). O `git`
e o `python3` são os de verdade: é com eles que o script decide.

Nada sai da máquina. O `gh` falso só anota, e o push vai para o remoto local.

O que os testes olham é o que o usuário veria: o código de saída, as tags
que ficaram, os arquivos gerados e a ordem das chamadas no diário dos
falsos. O cmake falso grava no `.uf2` o `git describe` do momento do build,
como o de verdade, e é assim que se vê se a tag veio antes da reconstrução.
"""

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

TESTES = Path(__file__).resolve().parent
SCRIPTS = TESTES.parent
DISPOSITIVO = SCRIPTS.parent
FALSOS = TESTES / "falsos"

TAG = "v0.1.0"

#: Os avisos que vão anexos à release. O script recusa começar sem eles.
AVISOS = ("THIRD_PARTY_NOTICES.md", "LICENSE.md", "NOTICE",
          "PERMISSOES-ADICIONAIS.md")

#: O ambiente que o script herdaria e que mudaria o que ele faz.
PREFIXOS_DE_FORA = ("CORUJA_", "FALSO_", "UBSAN_", "ASAN_", "GIT_")

TESTE_TRIVIAL = '''import unittest


class Trivial(unittest.TestCase):
    def test_passa(self):
        self.assertTrue(True)
'''


class Liberacao(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        raiz = Path(tmp.name)
        self.repo = raiz / "repo"
        self.remoto = raiz / "remoto.git"
        self.diario = raiz / "diario"
        self.logs = raiz / "logs"
        self.logs.mkdir()
        gitconfig = raiz / "gitconfig"
        gitconfig.write_text(
            "[user]\n\tname = Teste\n\temail = teste@exemplo.invalido\n"
            "[init]\n\tdefaultBranch = main\n"
            "[commit]\n\tgpgSign = false\n[tag]\n\tgpgSign = false\n")

        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(PREFIXOS_DE_FORA)}
        self.env.update({
            "PATH": f"{FALSOS}{os.pathsep}{os.environ['PATH']}",
            "GIT_CONFIG_GLOBAL": str(gitconfig),
            "GIT_CONFIG_NOSYSTEM": "1",
            "FALSO_DIARIO": str(self.diario),
            "CORUJA_LOGS": str(self.logs),
            "CORUJA_PICOTOOL": str(FALSOS / "picotool"),
        })

        self.git("init", "-q", "--bare", str(self.remoto), cwd=raiz)
        self.git("init", "-q", str(self.repo), cwd=raiz)
        (self.repo / "scripts" / "testes").mkdir(parents=True)
        (self.repo / "firmware").mkdir()
        shutil.copy2(SCRIPTS / "libera_fw.sh", self.repo / "scripts")
        shutil.copy2(SCRIPTS / "versao_no_uf2.py", self.repo / "scripts")
        shutil.copy2(DISPOSITIVO / "firmware" / "particoes.json",
                     self.repo / "firmware")
        for aviso in AVISOS:
            if (DISPOSITIVO / aviso).exists():
                shutil.copy2(DISPOSITIVO / aviso, self.repo)
        (self.repo / "scripts" / "testes" / "test_trivial.py").write_text(
            TESTE_TRIVIAL)
        (self.repo / "LEIAME.txt").write_text("um arquivo versionado\n")
        (self.repo / ".gitignore").write_text(
            "firmware/build*/\n__pycache__/\n")
        for build in ("build-host", "build-san", "build-pico",
                      "build-bancada", "build-ota"):
            (self.repo / "firmware" / build).mkdir()
            (self.repo / "firmware" / build / "CMakeCache.txt").write_text(
                "CORUJA_SANITIZER:BOOL=ON\n" if build == "build-san" else "")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "inicial")
        self.git("remote", "add", "origin", str(self.remoto))
        self.git("push", "-q", "-u", "origin", "main")
        self.diario.write_text("")

    # --- apoio ---------------------------------------------------------------

    def git(self, *args, cwd=None) -> str:
        r = subprocess.run(["git", *args], cwd=cwd or self.repo, env=self.env,
                           capture_output=True, text=True, check=True)
        return r.stdout.strip()

    def libera(self, *args, **extra) -> subprocess.CompletedProcess:
        env = {**self.env, **extra}
        # Pelo shebang, como o usuario roda: no macOS e o bash 3.2.
        return subprocess.run(
            [str(self.repo / "scripts" / "libera_fw.sh"), *args],
            cwd=self.repo, env=env, capture_output=True, text=True,
            check=False, timeout=120)

    def tags_locais(self) -> list[str]:
        return self.git("tag", "--list").split()

    def chamadas(self, prefixo: str) -> list[str]:
        return [linha for linha in self.diario.read_text().splitlines()
                if linha.startswith(prefixo)]

    def compilacoes_do_pico(self) -> list[str]:
        return [c for c in self.chamadas("cmake --build")
                if c.split()[2].endswith("/build-pico")]

    def assertSemTag(self, r: subprocess.CompletedProcess):
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.tags_locais(), [], r.stdout + r.stderr)

    # --- o caminho feliz -----------------------------------------------------

    def test_libera_marca_o_commit_e_reconstroi_NA_tag(self):
        sha = self.git("rev-parse", "HEAD")
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.git("rev-parse", f"{TAG}^{{commit}}"), sha)
        self.assertEqual(self.git("cat-file", "-t", TAG), "tag")  # anotada
        # A primeira compilação do Pico é anterior à tag; a segunda já a vê.
        pico = self.compilacoes_do_pico()
        self.assertEqual(len(pico), 2, pico)
        self.assertNotIn(f"[describe={TAG}]", pico[0])
        self.assertIn(f"[describe={TAG}]", pico[1])
        # E nada foi para a rede.
        self.assertEqual(self.chamadas("gh "), [])
        self.assertNotIn(TAG, self.git("ls-remote", "--tags", "origin"))

    def test_a_soma_publicada_e_a_do_artefato(self):
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        build = self.repo / "firmware" / "build-pico"
        artefato = build / f"coruja_gps-{TAG}.uf2"
        soma = hashlib.sha256(artefato.read_bytes()).hexdigest()
        self.assertEqual((build / f"coruja_gps-{TAG}.uf2.sha256").read_text(),
                         f"{soma}  coruja_gps-{TAG}.uf2\n")

    # --- B1: o commit tagueado é o conferido -------------------------------

    def test_HEAD_que_anda_durante_o_build_e_recusado_sem_tag(self):
        # Um commit feito em outro terminal enquanto a suíte compila: a tag
        # ia para ele, que ninguém conferiu.
        r = self.libera(TAG, "primeira", FALSO_CMAKE_build_host_1=(
            "git commit -q --allow-empty -m 'feito durante o build'"))
        self.assertSemTag(r)
        self.assertIn("HEAD", r.stderr)
        self.assertEqual(len(self.compilacoes_do_pico()), 1)

    def test_arvore_que_suja_durante_o_build_e_recusada_antes_da_tag(self):
        r = self.libera(TAG, "primeira",
                        FALSO_CMAKE_build_pico_1="echo sujo >> LEIAME.txt")
        self.assertSemTag(r)
        self.assertIn("arvore suja", r.stderr)
        self.assertEqual(len(self.compilacoes_do_pico()), 1)

    # --- B2: o sanitizer --------------------------------------------------

    def test_sanitizer_configurado_pela_receita_e_parando_no_primeiro_ub(self):
        (self.repo / "firmware" / "build-san" / "CMakeCache.txt").unlink()
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        configura = [c for c in self.chamadas("cmake -S")
                     if "/build-san" in c]
        self.assertEqual(len(configura), 1, self.chamadas("cmake"))
        self.assertIn("-DCORUJA_SANITIZER=ON", configura[0])
        self.assertIn("-DCORUJA_TESTES=ON", configura[0])
        san = [c for c in self.chamadas("ctest") if "/build-san" in c]
        self.assertEqual(len(san), 1)
        self.assertIn("halt_on_error=1", san[0])
        self.assertIn("--output-on-failure", san[0])

    def test_suite_vermelha_no_sanitizer_para_antes_da_tag(self):
        r = self.libera(TAG, "primeira", FALSO_CTEST_ACAO=(
            '[ "$(basename "$dir")" != build-san ]'))
        self.assertSemTag(r)
        self.assertIn("sanitizer", r.stderr)

    # --- a tag não fica para trás ------------------------------------------

    def test_ctrl_c_na_reconstrucao_remove_a_tag(self):
        r = self.libera(TAG, "primeira",
                        FALSO_CMAKE_build_pico_2="kill -INT $PPID")
        self.assertEqual(r.returncode, 130, r.stdout + r.stderr)
        self.assertEqual(self.tags_locais(), [])

    def test_falha_depois_da_tag_a_remove_e_a_proxima_execucao_passa(self):
        r = self.libera(TAG, "primeira", FALSO_PICOTOOL_ACAO=(
            '[ "$1" != partition ]'))
        self.assertSemTag(r)
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.tags_locais(), [TAG])




if __name__ == "__main__":
    unittest.main()
