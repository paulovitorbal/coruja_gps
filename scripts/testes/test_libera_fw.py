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
        shutil.copy2(SCRIPTS / "junta_licencas.py", self.repo / "scripts")
        (self.repo / "docs").mkdir()
        shutil.copytree(DISPOSITIVO / "docs" / "licencas",
                        self.repo / "docs" / "licencas")
        # SDK e FatFs-SD FALSOS, so com os textos que o pacote de licencas le.
        self.sdk = raiz / "sdk"
        self.fatfs = raiz / "fatfs"
        for base, rels in ((self.sdk, ("LICENSE.TXT", "lib/lwip/COPYING",
                                       "lib/mbedtls/LICENSE",
                                       "lib/tinyusb/LICENSE",
                                       "lib/cyw43-driver/LICENSE",
                                       "lib/cyw43-driver/LICENSE.RP")),
                           (self.fatfs, ("LICENSE", "src/ff15/LICENSE.txt"))):
            for rel in rels:
                alvo = base / rel
                alvo.parent.mkdir(parents=True, exist_ok=True)
                alvo.write_text(f"TEXTO {rel}\n", encoding="utf-8")
        self.env["CORUJA_SDK"] = str(self.sdk)
        self.env["CORUJA_FATFS"] = str(self.fatfs)
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

    # --- recusas de começo ---------------------------------------------------

    def test_ramo_que_nao_e_a_main_e_recusado_antes_de_compilar(self):
        self.git("checkout", "-q", "-b", "experimento")
        r = self.libera(TAG, "primeira")
        self.assertSemTag(r)
        self.assertIn("main", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [])

    def test_sem_um_dos_avisos_de_licenca_nao_libera(self):
        # A release entrega o .uf2 a quem nunca vai ao repositorio: sem os
        # termos junto, a distribuicao sai sem a licenca que ela exige.
        alvo = self.repo / "NOTICE"
        if not alvo.exists():
            self.skipTest("o repositorio ainda nao tem NOTICE")
        alvo.unlink()
        self.git("commit", "-q", "-am", "sem o NOTICE")
        r = self.libera(TAG, "primeira")
        self.assertSemTag(r)
        self.assertIn("NOTICE", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [])

    def test_publicar_recusa_cedo_tag_que_ja_esta_no_remoto(self):
        self.git("tag", TAG)
        self.git("push", "-q", "origin", TAG)
        self.git("tag", "-d", TAG)
        r = self.libera("--publicar", TAG, "primeira")
        self.assertSemTag(r)
        self.assertIn("remoto", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [])

    def test_publicar_recusa_cedo_gh_que_nao_esta_autenticado(self):
        """O `gh` so era usado DEPOIS do push. Deslogado, todo o build e as
        suites passavam, o `git push --atomic` publicava a `main` e a tag, e o
        `gh release create` falhava: tag publica, sem release, e o texto de
        recuperacao mandava terminar a mao com comandos que tambem dependem do
        `gh`. Por isso a checagem fica ANTES de qualquer coisa cara."""
        antes = self.git("ls-remote", "origin", "refs/heads/main")
        r = self.libera("--publicar", TAG, "primeira",
                        FALSO_GH_ACAO='[ "$1 $2" != "auth status" ]')
        self.assertSemTag(r)
        self.assertIn("gh", r.stderr)
        self.assertIn("auth", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [], "gastou build antes")
        self.assertEqual(self.git("ls-remote", "origin", "refs/heads/main"),
                         antes)
        self.assertNotIn(TAG, self.git("ls-remote", "--tags", "origin"))

    def test_publicar_recusa_cedo_se_nao_ha_gh(self):
        sem_gh = {**self.env, "PATH": f"{self._pasta_sem_gh()}"}
        r = subprocess.run(
            [str(self.repo / "scripts" / "libera_fw.sh"), "--publicar", TAG,
             "primeira"], cwd=self.repo, env=sem_gh, capture_output=True,
            text=True, check=False, timeout=60)
        self.assertSemTag(r)
        self.assertIn("gh", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [])

    def _pasta_sem_gh(self) -> str:
        """Um PATH com os utilitarios de que o script precisa, e sem `gh`."""
        d = self.repo.parent / "bin-sem-gh"
        d.mkdir(exist_ok=True)
        for nome in ("bash", "git", "python3", "env", "dirname", "sed", "awk",
                     "grep", "tr", "cat", "mkdir", "rm", "head", "sort", "wc",
                     "shasum", "ls", "cut", "date", "mktemp", "basename",
                     "sleep", "uname", "tee", "cp", "mv", "touch", "id"):
            achado = shutil.which(nome)
            if achado and not (d / nome).exists():
                (d / nome).symlink_to(achado)
        for nome in ("cmake", "ctest", "picotool", "docker"):
            (d / nome).symlink_to(FALSOS / nome)
        return str(d)

    # --- a tag nao pode andar para tras ------------------------------------

    def test_tag_menor_que_a_ultima_e_recusada_antes_de_gastar_build(self):
        """O bootrom A/B sobe a imagem de MAIOR versao. Um erro de digitacao,
        `v0.2.6` no lugar de `v0.2.26`, gera uma imagem 2.600 contra 2.2600,
        e todas as conferencias passavam: a versao da imagem batia com a tag.
        O aparelho subiria a copia ANTIGA, e a nova ficaria inerte."""
        self.git("tag", "-a", "-m", "anterior", "v0.2.25")
        r = self.libera("v0.2.6", "primeira")
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.tags_locais(), ["v0.2.25"])
        self.assertIn("v0.2.25", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [], "gastou build antes")

    def test_tag_so_um_numero_acima_da_ultima_passa(self):
        self.git("tag", "-a", "-m", "anterior", "v0.2.25")
        # A tag nova fica num commit NOVO, como na vida real. As duas no mesmo
        # commit e no mesmo segundo faziam o `git describe` desempatar por
        # acaso, e o binario as vezes saia com a tag velha: o teste era
        # instavel por culpa propria, nao do script.
        self.git("commit", "-q", "--allow-empty", "-m", "depois da v0.2.25")
        r = self.libera("v0.2.26", "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("v0.2.26", self.tags_locais())

    def test_ordem_e_numerica_e_nao_de_texto(self):
        # `v0.2.9` vem DEPOIS de `v0.2.10` em ordem de texto.
        self.git("tag", "-a", "-m", "anterior", "v0.2.10")
        r = self.libera("v0.2.9", "primeira")
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("v0.2.10", r.stderr)
        # E recusada CEDO: sem esta linha o teste passava por outro motivo,
        # a conferencia da versao no binario, depois de gastar o build.
        self.assertEqual(self.chamadas("cmake"), [], "gastou build antes")

    def test_publicar_tambem_olha_as_tags_do_remoto(self):
        # A ultima liberacao pode ter sido de outra maquina: esta nao tem a
        # tag local, mas o remoto tem.
        self.git("tag", "-a", "-m", "anterior", "v0.5.0")
        self.git("push", "-q", "origin", "v0.5.0")
        self.git("tag", "-d", "v0.5.0")
        r = self.libera("--publicar", "v0.4.0", "primeira")
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.tags_locais(), [])
        self.assertIn("v0.5.0", r.stderr)
        self.assertEqual(self.chamadas("cmake"), [])

    # --- a compilacao pre-tag nao pode travar a liberacao --------------------

    def test_so_as_compilacoes_pre_tag_declaram_que_sao_pre_tag(self):
        """O guarda de `versao_imagem.cmake` recusa configurar com 100 ou mais
        commits desde a ultima tag, e a etapa 6 compila ANTES de criar a tag:
        a propria liberacao que cria a tag que resolveria o guarda era barrada
        por ele. A etapa 6 declara que e pre-tag (a imagem sai 0.0, que a
        etapa 9 recusa). A etapa 8, que constroi o que se PUBLICA, nao pode
        declarar: senao o guarda nunca a protegeria."""
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        builds = [c for c in self.chamadas("cmake --build")
                  if c.split()[2].rsplit("/", 1)[-1]
                  in ("build-pico", "build-bancada", "build-ota")]
        pre = [c for c in builds if "[pre_tag=1]" in c]
        pos = [c for c in builds if "[pre_tag=]" in c]
        # Etapa 6: as tres. Etapa 8: so o do Pico, reconstruido na tag.
        self.assertEqual(len(pre), 3, builds)
        self.assertEqual(len(pos), 1, builds)
        self.assertIn("/build-pico", pos[0])
        self.assertIn(f"[describe={TAG}]", pos[0])

    def test_a_declaracao_nao_vaza_para_a_construcao_na_tag(self):
        # O mesmo, visto pelo outro lado: a ultima compilacao do Pico, a que
        # vira o artefato publicado, nunca leva a declaracao.
        r = self.libera(TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        pico = self.compilacoes_do_pico()
        self.assertIn("[pre_tag=1]", pico[0])
        self.assertNotIn("[pre_tag=1]", pico[-1])

    # --- os textos das licencas vao na release ------------------------------

    def test_publicar_anexa_o_pacote_de_licencas_de_terceiros(self):
        """BSD-3, MIT e Apache-2.0 pedem o texto da licenca junto do binario.
        As bibliotecas nao estao no repositorio: quem baixa so a release nao
        as tem. O pacote e gerado dos textos que o build usou."""
        r = self.libera("--publicar", TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        sobe = self.chamadas("gh release upload")
        self.assertEqual(len(sobe), 1, sobe)
        self.assertIn("THIRD_PARTY_LICENSES.txt", sobe[0])
        pacote = (self.repo / "firmware" / "build-pico"
                  / "THIRD_PARTY_LICENSES.txt").read_text(encoding="utf-8")
        self.assertIn("TEXTO lib/lwip/COPYING", pacote)
        self.assertIn("TEXTO src/ff15/LICENSE.txt", pacote)

    def test_sem_o_texto_de_uma_licenca_nada_e_publicado(self):
        (self.sdk / "lib" / "tinyusb" / "LICENSE").unlink()
        antes = self.git("ls-remote", "origin", "refs/heads/main")
        r = self.libera("--publicar", TAG, "primeira")
        self.assertSemTag(r)
        self.assertIn("tinyusb", r.stderr)
        # Falhou ANTES do push: o remoto nao mudou e nao houve release.
        self.assertEqual(self.git("ls-remote", "origin", "refs/heads/main"),
                         antes)
        self.assertNotIn(TAG, self.git("ls-remote", "--tags", "origin"))
        self.assertEqual(self.chamadas("gh release"), [])

    def test_sem_publicar_tambem_confere_os_textos(self):
        # Quem so confere (sem --publicar) tem de descobrir a falta agora, e
        # nao so no dia em que for publicar.
        (self.fatfs / "LICENSE").unlink()
        r = self.libera(TAG, "primeira")
        self.assertSemTag(r)
        self.assertIn("LICENSE", r.stderr)

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

    # --- o teto da partição --------------------------------------------------

    def test_sem_o_bin_o_teto_nao_passa_em_silencio(self):
        r = self.libera(TAG, "primeira", FALSO_SEM_BIN="1")
        self.assertSemTag(r)
        self.assertIn("coruja_gps.bin", r.stderr)

    def test_o_teto_e_o_tamanho_da_particao_no_json(self):
        json = self.repo / "firmware" / "particoes.json"
        json.write_text(json.read_text().replace('"1536K"', '"4K"'))
        self.git("commit", "-q", "-am", "particoes de 4K")
        r = self.libera(TAG, "primeira", FALSO_TAM_BIN="4097")
        self.assertSemTag(r)
        self.assertIn("4097 bytes e a particao, 4096", r.stderr)

    # --- a tag não fica para trás ------------------------------------------

    def test_falha_do_shasum_nao_deixa_soma_vazia_nem_tag(self):
        quebrados = self.repo.parent / "quebrados"
        quebrados.mkdir()
        shasum = quebrados / "shasum"
        shasum.write_text("#!/bin/bash\nexit 1\n")
        shasum.chmod(0o755)
        r = self.libera(TAG, "primeira",
                        PATH=f"{quebrados}{os.pathsep}{self.env['PATH']}")
        self.assertSemTag(r)
        self.assertIn("SHA-256", r.stderr)

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

    # --- publicação ----------------------------------------------------------

    def test_publicar_empurra_o_commit_conferido_e_anexa_os_avisos(self):
        sha = self.git("rev-parse", "HEAD")
        r = self.libera("--publicar", TAG, "primeira")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        remoto = self.git("ls-remote", "origin")
        self.assertIn(f"{sha}\trefs/heads/main", remoto)
        self.assertIn(f"refs/tags/{TAG}^{{}}", remoto)
        self.assertEqual(
            self.git("rev-parse", f"{TAG}^{{commit}}", cwd=self.remoto), sha)
        cria = self.chamadas("gh release create")
        self.assertEqual(len(cria), 1)
        self.assertIn("--verify-tag", cria[0])
        sobe = self.chamadas("gh release upload")
        self.assertEqual(len(sobe), 1)
        for anexo in (f"coruja_gps-{TAG}.uf2", f"coruja_gps-{TAG}.uf2.sha256",
                      f"coruja_gps-{TAG}-particoes.uf2", *AVISOS):
            if anexo in AVISOS and not (DISPOSITIVO / anexo).exists():
                continue
            self.assertIn(f"/{anexo}", sobe[0])

    def test_upload_que_falha_manda_terminar_com_upload_e_nao_create(self):
        r = self.libera("--publicar", TAG, "primeira",
                        FALSO_GH_ACAO='[ "$2" != upload ]')
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(f"gh release upload {TAG}", r.stderr)
        self.assertNotIn("gh release create", r.stderr)
        # A tag ja esta no remoto: a local fica, para bater com ela.
        self.assertEqual(self.tags_locais(), [TAG])


if __name__ == "__main__":
    unittest.main()
