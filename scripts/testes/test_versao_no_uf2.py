"""O portão que confere a versão dentro do `.uf2`.

    python3 -m unittest discover -s scripts/testes

⚠️ **Este arquivo existe por causa de um falso negativo.** Em 16/10/2026 a
release da v0.2.16 foi barrada por um firmware correto: o `strings` no `.uf2`
mostrava só `v0.2`, porque o formato embala 256 bytes de dado em blocos de
512 com 32 de cabeçalho, e a string cruzava a fronteira.

O caso central aqui é justamente esse — uma versão **partida entre dois
blocos**, que o `strings` não acha e o desembalador acha.
"""

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
FERRAMENTA = RAIZ / "versao_no_uf2.py"

MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
MAGIC_FINAL = 0x0AB16F30
TAM_DADO = 256


def monta_uf2(carga: bytes) -> bytes:
    """Empacota `carga` em blocos UF2 de 512 bytes, como o `elf2uf2` faz."""
    blocos = [carga[i:i + TAM_DADO] for i in range(0, len(carga), TAM_DADO)]
    if not blocos:
        blocos = [b""]
    saida = bytearray()
    for n, pedaco in enumerate(blocos):
        saida += struct.pack(
            "<8I", MAGIC0, MAGIC1, 0x00002000, 0x10000000 + n * TAM_DADO,
            len(pedaco), n, len(blocos), 0xE48BFF59)
        saida += pedaco.ljust(476, b"\x00")
        saida += struct.pack("<I", MAGIC_FINAL)
    return bytes(saida)


def roda(caminho: Path, tag: str):
    return subprocess.run(
        [sys.executable, str(FERRAMENTA), str(caminho), tag],
        capture_output=True, text=True, check=False)


class VersaoNoUf2(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.arq = Path(self.tmp.name) / "fw.uf2"

    def escreve(self, carga: bytes):
        self.arq.write_bytes(monta_uf2(carga))

    def test_acha_a_versao_no_meio_de_um_bloco(self):
        self.escreve(b"\x00" * 100 + b"v0.2.16\x00" + b"\x00" * 400)
        self.assertEqual(roda(self.arq, "v0.2.16").returncode, 0)

    def test_acha_a_versao_PARTIDA_ENTRE_DOIS_BLOCOS(self):
        # ⚠️ **O teste que importa**, e o caso de 16/10/2026: a string começa
        # no fim de um bloco e termina no começo do seguinte. No arquivo em
        # disco há 32 bytes de cabeçalho e 220 de enchimento no meio dela.
        carga = b"\x00" * (TAM_DADO - 4) + b"v0.2.16\x00" + b"\x00" * 200
        self.escreve(carga)
        bruto = self.arq.read_bytes()
        self.assertNotIn(b"v0.2.16", bruto,
                         "a fixture nao reproduziu o corte entre blocos")
        self.assertEqual(roda(self.arq, "v0.2.16").returncode, 0)

    def test_recusa_quando_a_versao_nao_esta_la(self):
        self.escreve(b"\x00" * 50 + b"v0.2.15\x00" + b"\x00" * 400)
        r = roda(self.arq, "v0.2.16")
        self.assertEqual(r.returncode, 1)

    def test_o_erro_diz_que_versao_HA_no_lugar(self):
        # Negar sem dizer o que há manda procurar. A mensagem aponta.
        self.escreve(b"\x00" * 50 + b"v0.2.15\x00" + b"\x00" * 400)
        self.assertIn("v0.2.15", roda(self.arq, "v0.2.16").stderr)

    def test_sem_nenhuma_versao_tambem_explica(self):
        self.escreve(b"\x00" * 600)
        r = roda(self.arq, "v0.2.16")
        self.assertEqual(r.returncode, 1)
        self.assertIn("nenhuma string de versao", r.stderr)

    def test_versao_mais_longa_nao_casa_com_prefixo(self):
        # `v0.2.1` não pode dar por boa uma release `v0.2.16`: o inverso é que
        # seria aceitável, e nem isso interessa aqui.
        self.escreve(b"v0.2.1\x00" + b"\x00" * 400)
        self.assertEqual(roda(self.arq, "v0.2.16").returncode, 1)

    def test_arquivo_sem_o_magic_do_uf2_e_recusado(self):
        # Um `.elf` renomeado passaria pelo `strings` e daria por verificada
        # uma release que ninguém empacotou.
        # Do tamanho certo de propósito: assim quem recusa é o magic, e não
        # a conferência de múltiplo de bloco.
        corpo = b"\x7fELF" + b"v0.2.16"
        self.arq.write_bytes(corpo + b"\x00" * (1024 - len(corpo)))
        r = roda(self.arq, "v0.2.16")
        self.assertEqual(r.returncode, 1)
        self.assertIn("magic", r.stderr.lower())

    def test_tamanho_que_nao_e_multiplo_de_bloco_e_recusado(self):
        self.arq.write_bytes(monta_uf2(b"v0.2.16")[:-8])
        self.assertEqual(roda(self.arq, "v0.2.16").returncode, 1)

    def test_sem_argumentos_explica_em_vez_de_estourar(self):
        r = subprocess.run([sys.executable, str(FERRAMENTA)],
                           capture_output=True, text=True, check=False)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
