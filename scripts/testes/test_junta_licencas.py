"""`junta_licencas.py` -- o pacote de textos que vai anexo a release.

BSD-3-Clause, MIT e Apache-2.0 pedem o aviso de copyright e o texto da
licenca junto do binario redistribuido. As bibliotecas nao estao no
repositorio, entao quem baixa so a release nao as tem: o texto de cada uma
tem de ir NA release. O pacote e gerado a partir das copias reais que o build
usou, e a liberacao recusa sair sem ele.
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "junta_licencas.py"

#: caminho relativo ao SDK, ao FatFs-SD e a docs/licencas -> marca no texto
ARQUIVOS = {
    "sdk": {
        "LICENSE.TXT": "TEXTO-PICO-SDK",
        "lib/lwip/COPYING": "TEXTO-LWIP",
        "lib/mbedtls/LICENSE": "TEXTO-MBEDTLS",
        "lib/tinyusb/LICENSE": "TEXTO-TINYUSB",
        "lib/cyw43-driver/LICENSE": "TEXTO-CYW43",
        "lib/cyw43-driver/LICENSE.RP": "TEXTO-CYW43-RP",
    },
    "fatfs": {
        "LICENSE": "TEXTO-FATFS-SD",
        "src/ff15/LICENSE.txt": "TEXTO-FATFS-CHAN",
    },
    "licencas": {
        "OFL.txt": "TEXTO-OFL",
        "CC-BY-4.0-twemoji.txt": "TEXTO-TWEMOJI",
    },
}


class JuntaLicencas(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.raiz = Path(tmp.name)
        for pasta, arquivos in ARQUIVOS.items():
            for rel, texto in arquivos.items():
                alvo = self.raiz / pasta / rel
                alvo.parent.mkdir(parents=True, exist_ok=True)
                alvo.write_text(texto + "\n", encoding="utf-8")
        self.saida = self.raiz / "saida.txt"

    def roda(self):
        return subprocess.run(
            [sys.executable, "-I", str(SCRIPT),
             "--sdk", str(self.raiz / "sdk"),
             "--fatfs", str(self.raiz / "fatfs"),
             "--licencas", str(self.raiz / "licencas"),
             "--saida", str(self.saida)],
            capture_output=True, text=True, check=False, timeout=30)

    def test_junta_o_texto_de_cada_componente(self):
        r = self.roda()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        texto = self.saida.read_text(encoding="utf-8")
        for pasta in ARQUIVOS.values():
            for marca in pasta.values():
                self.assertIn(marca, texto)

    def test_cada_texto_vem_sob_o_nome_do_componente(self):
        self.roda()
        texto = self.saida.read_text(encoding="utf-8")
        for nome in ("Raspberry Pi Pico SDK", "lwIP", "Mbed TLS", "TinyUSB",
                     "cyw43-driver", "FatFs", "JetBrains Mono", "Twemoji"):
            self.assertIn(nome, texto)

    def test_diz_qual_licenca_do_mbedtls_o_projeto_toma(self):
        # O LICENSE do Mbed TLS e dual (Apache-2.0 OU GPL-2.0-or-later): quem
        # le o pacote precisa saber qual das duas vale aqui.
        self.roda()
        texto = self.saida.read_text(encoding="utf-8")
        self.assertIn("Apache-2.0", texto)
        self.assertRegex(texto, r"(?i)toma a Apache-2\.0|escolh\w+ a Apache-2\.0")

    def test_arquivo_que_falta_recusa_e_nao_grava_pacote_incompleto(self):
        (self.raiz / "sdk" / "lib" / "lwip" / "COPYING").unlink()
        r = self.roda()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("lib/lwip/COPYING", r.stderr)
        self.assertFalse(self.saida.exists(),
                         "gravou um pacote sem o texto do lwIP")

    def test_pacote_velho_nao_sobrevive_a_uma_falha(self):
        # Se a geracao falha, o pacote de uma execucao anterior nao pode ficar
        # la passando por novo: a liberacao anexaria o texto errado.
        self.saida.write_text("PACOTE VELHO\n", encoding="utf-8")
        (self.raiz / "fatfs" / "LICENSE").unlink()
        r = self.roda()
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(self.saida.exists())

    def test_arquivo_vazio_conta_como_faltando(self):
        (self.raiz / "sdk" / "lib" / "tinyusb" / "LICENSE").write_text("")
        r = self.roda()
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("tinyusb", r.stderr)

    def test_o_cyw43_leva_as_duas_licencas(self):
        self.roda()
        texto = self.saida.read_text(encoding="utf-8")
        self.assertIn("TEXTO-CYW43\n", texto)
        self.assertIn("TEXTO-CYW43-RP", texto)


if __name__ == "__main__":
    unittest.main()
