"""`versao_imagem.cmake`: o numero que o bootrom A/B compara.

A versao da imagem e `A*100+B . C*100+N`, com N os commits depois da tag. Com
N >= 100 o numero colidiria com o da tag seguinte, e o guarda recusa
configurar. Isso e certo para uma imagem que vai ser PUBLICADA.

Mas o `libera_fw.sh` compila o Pico ANTES de criar a tag (etapa 6), so para
ver se compila, e a etapa 8 reconstroi na tag. Com 100 ou mais commits desde a
ultima tag, o guarda impedia a propria liberacao que existe para criar a tag
que o resolve. Aqui se confere os dois lados: o guarda segue valendo, e a
compilacao pre-tag, declarada, sai com versao 0.0 -- a que a liberacao ja
recusa.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
CMAKE_DIR = RAIZ / "firmware"

PROJETO = """cmake_minimum_required(VERSION 3.18)
project(teste_versao NONE)
list(APPEND CMAKE_MODULE_PATH "{firmware}")
include("{firmware}/cmake/versao_imagem.cmake")
coruja_versao_imagem("{repo}" "{saida}")
"""


class VersaoDaImagem(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.raiz = Path(tmp.name)
        self.repo = self.raiz / "repo"
        self.repo.mkdir()
        gitconfig = self.raiz / "gitconfig"
        gitconfig.write_text(
            "[user]\n\tname = Teste\n\temail = teste@exemplo.invalido\n"
            "[init]\n\tdefaultBranch = main\n"
            "[commit]\n\tgpgSign = false\n[tag]\n\tgpgSign = false\n")
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("CORUJA_", "GIT_"))}
        self.env.update({"GIT_CONFIG_GLOBAL": str(gitconfig),
                         "GIT_CONFIG_NOSYSTEM": "1"})
        self.git("init", "-q")
        self.commit("inicial")
        self.git("tag", "-a", "-m", "v", "v0.2.25")
        self.proj = self.raiz / "proj"
        self.proj.mkdir()
        self.saida = self.raiz / "gerado" / "VersaoImagem.h"
        (self.proj / "CMakeLists.txt").write_text(PROJETO.format(
            firmware=CMAKE_DIR, repo=self.repo, saida=self.saida))

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.repo, env=self.env,
                       capture_output=True, text=True, check=True)

    def commit(self, msg):
        (self.repo / "f.txt").write_text(msg + "\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", msg)

    def depois_da_tag(self, n):
        for i in range(n):
            self.commit(f"c{i}")

    def configura(self, **env_extra):
        env = {**self.env, **env_extra}
        return subprocess.run(
            ["cmake", "-S", str(self.proj), "-B", str(self.raiz / "b")],
            capture_output=True, text=True, check=False, env=env, timeout=120)

    def versao(self):
        h = self.saida.read_text()
        maior = int(h.split("kVersaoImagemMaior = ")[1].split(";")[0])
        menor = int(h.split("kVersaoImagemMenor = ")[1].split(";")[0])
        return maior, menor

    def test_a_versao_e_a_conta_documentada(self):
        self.depois_da_tag(3)
        r = self.configura()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.versao(), (2, 2503))   # v0.2.25 + 3 commits

    def test_cem_commits_depois_da_tag_recusam_configurar(self):
        # O guarda e certo para uma imagem que vai ser publicada.
        self.depois_da_tag(100)
        r = self.configura()
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("100 commits depois da tag", r.stdout + r.stderr)
        self.assertFalse(self.saida.exists())

    def test_compilacao_pre_tag_declarada_configura_e_sai_com_versao_zero(self):
        # A etapa 6 do libera_fw.sh: compila so para ver se compila, e a
        # etapa 8 reconstroi na tag. A imagem sai 0.0 -- a que a liberacao
        # ja recusa --, e nunca com um numero que colida com o da tag seguinte.
        self.depois_da_tag(100)
        r = self.configura(CORUJA_BUILD_PRE_TAG="1")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.versao(), (0, 0))
        self.assertIn("pre-tag", r.stdout + r.stderr)

    def test_a_declaracao_nao_muda_o_caminho_normal(self):
        # Quem esquece a variavel ligada num shell nao ganha versao zero
        # num build comum: com poucos commits o numero continua o de sempre.
        self.depois_da_tag(3)
        r = self.configura(CORUJA_BUILD_PRE_TAG="1")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.versao(), (2, 2503))

    def test_so_o_valor_1_conta_como_declaracao(self):
        self.depois_da_tag(100)
        for valor in ("", "0", "sim", "true"):
            with self.subTest(valor=valor):
                r = self.configura(CORUJA_BUILD_PRE_TAG=valor)
                self.assertNotEqual(r.returncode, 0, valor)


if __name__ == "__main__":
    unittest.main()
