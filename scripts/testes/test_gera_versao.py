"""O `VersaoBuild.h` que vai para a tela, gerado num repositório de mentira.

    python3 -m unittest discover -s scripts/testes

O `firmware/cmake/gera_versao.cmake` roda a cada build e carimba no firmware
o `git describe` da árvore. É a linha `boot:` do log e a faixa `fw:` da tela
— o que diz QUAL firmware está no aparelho, e que existe por causa da
gravação que não pegou em 2026-10-04.

Aqui ele roda com `cmake -P`, como o alvo `coruja_versao` o roda, num
repositório temporário. O que se confere é o cabeçalho gerado.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
SCRIPT = RAIZ / "firmware" / "cmake" / "gera_versao.cmake"


class GeraVersao(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.repo = Path(tmp.name) / "repo"
        self.repo.mkdir()
        self.saida = Path(tmp.name) / "VersaoBuild.h"
        gitconfig = Path(tmp.name) / "gitconfig"
        gitconfig.write_text(
            "[user]\n\tname = Teste\n\temail = teste@exemplo.invalido\n"
            "[init]\n\tdefaultBranch = main\n"
            "[commit]\n\tgpgSign = false\n[tag]\n\tgpgSign = false\n")
        self.env = {**os.environ, "GIT_CONFIG_GLOBAL": str(gitconfig),
                    "GIT_CONFIG_NOSYSTEM": "1"}
        self.git("init", "-q")
        (self.repo / "um.txt").write_text("um\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "inicial")

    def git(self, *args) -> str:
        return subprocess.run(["git", *args], cwd=self.repo, env=self.env,
                              capture_output=True, text=True,
                              check=True).stdout.strip()

    def gera(self) -> str:
        r = subprocess.run(
            ["cmake", f"-DFONTE={self.repo}", f"-DSAIDA={self.saida}",
             "-P", str(SCRIPT)],
            capture_output=True, text=True, check=False, env=self.env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return self.saida.read_text()

    def versao(self) -> str:
        linha = [l for l in self.gera().splitlines() if "kVersaoBuild" in l]
        self.assertEqual(len(linha), 1, linha)
        return linha[0].split('"')[1]

    def test_em_cima_da_tag_sai_a_tag(self):
        self.git("tag", "-a", "v0.1.0", "-m", "primeira")
        self.assertEqual(self.versao(), "v0.1.0")

    def test_depois_da_tag_sai_a_contagem_e_o_hash(self):
        self.git("tag", "-a", "v0.1.0", "-m", "primeira")
        self.git("commit", "-q", "--allow-empty", "-m", "depois")
        self.assertRegex(self.versao(), r"^v0\.1\.0-1-g[0-9a-f]+$")

    def test_arvore_suja_ganha_o_asterisco(self):
        self.git("tag", "-a", "v0.1.0", "-m", "primeira")
        (self.repo / "um.txt").write_text("mexido\n")
        self.assertEqual(self.versao(), "v0.1.0*")

    def test_tag_que_nao_e_de_versao_NAO_vira_a_identidade(self):
        # `bancada-ok` não é uma versão, e a versão NUMÉRICA da imagem (ver
        # versao_imagem.cmake, que já filtra) nem a enxerga. Sem o filtro
        # aqui, a tela diria `bancada-ok` e o bloco da imagem, outra coisa.
        self.git("tag", "-a", "bancada-ok", "-m", "anotacao de bancada")
        self.assertRegex(self.versao(), r"^[0-9a-f]+$")

    def test_a_tag_de_versao_vence_a_outra_no_mesmo_commit(self):
        self.git("tag", "-a", "bancada-ok", "-m", "anotacao de bancada")
        self.git("tag", "-a", "v0.1.0", "-m", "primeira")
        self.assertEqual(self.versao(), "v0.1.0")

    def test_sem_git_o_cabecalho_sai_mesmo_assim(self):
        # O zip "Source code" da release cai aqui: sem isto, nada compila.
        sem_git = self.repo.parent / "sem_git"
        sem_git.mkdir()
        self.repo = sem_git
        self.assertEqual(self.versao(), "sem-git")

    def test_nao_reescreve_o_arquivo_quando_nada_mudou(self):
        # Reescrever marcaria o header como novo e recompilaria, a cada
        # build, tudo que o inclui.
        self.git("tag", "-a", "v0.1.0", "-m", "primeira")
        self.gera()
        antes = self.saida.stat().st_mtime_ns
        os.utime(self.saida, ns=(antes - 10**9, antes - 10**9))
        marcado = self.saida.stat().st_mtime_ns
        self.gera()
        self.assertEqual(self.saida.stat().st_mtime_ns, marcado)


if __name__ == "__main__":
    unittest.main()
