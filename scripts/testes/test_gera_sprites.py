"""Testes do conversor de sprites.

    python3 -m unittest discover -s scripts/testes

O que está em jogo é o FUNDO. Sem canal alfa, o sprite carrega o próprio
fundo: compor sobre uma cor e desenhar sobre outra deixa uma moldura de
40×40 em volta do ícone, e foi assim que ele ficou difícil de ler dirigindo.

Os testes que precisam do Pillow pulam sem ele — ele mora no `.venv/` do
checkout, fora do git.
"""
import importlib.util
import re
import sys
import unittest
from collections import Counter
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))

TEM_PILLOW = importlib.util.find_spec("PIL") is not None
if TEM_PILLOW:
    import gera_sprites as S  # noqa: E402

VISOR = RAIZ / "firmware/src/display/Visor.h"
SPRITES = RAIZ / "firmware/src/display/Sprites.h"


def para565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


class FundoDoSprite(unittest.TestCase):
    """O par que precisa andar junto, e que vive em duas linguagens."""

    def cor_da_paleta(self, nome: str) -> int:
        texto = VISOR.read_text(encoding="utf-8")
        m = re.search(rf"constexpr Cor565 {nome}\s*=\s*0x([0-9A-Fa-f]{{4}})",
                      texto)
        self.assertIsNotNone(m, f"{nome} sumiu do Visor.h")
        return int(m.group(1), 16)

    @unittest.skipUnless(TEM_PILLOW, "precisa do Pillow (.venv)")
    def test_o_FUNDO_do_gerador_e_o_kFundoAlerta_do_firmware(self):
        # São dois números em duas linguagens, e mudar um só põe uma
        # moldura creme (ou preta) em volta do ícone.
        self.assertEqual(para565(*S.FUNDO),
                         self.cor_da_paleta("kFundoAlerta"))

    def test_o_fundo_do_sprite_gerado_e_a_cor_do_fundo_de_alerta(self):
        # O arquivo que o firmware COMPILA, e não a intenção do gerador: é
        # o que pega um Sprites.h editado à mão ou gerado com outro fundo.
        #
        # A cor MAIS COMUM é o fundo: num ícone de 40×40 com margem, ela
        # ocupa mais da metade dos pixels. Olhar um canto só não serve --
        # o 🚦 encosta na borda de cima.
        texto = SPRITES.read_text(encoding="utf-8")
        esperado = self.cor_da_paleta("kFundoAlerta")
        for nome in ("kRadar", "kSemaforo", "kSemaforoComRadar"):
            corpo = texto.split(f"{nome}[kLado * kLado] = {{", 1)[1]
            valores = [int(v, 16) for v in
                       re.findall(r"0x([0-9A-F]{4})", corpo.split("};", 1)[0])]
            self.assertEqual(len(valores), 40 * 40, nome)
            comum, quantos = Counter(valores).most_common(1)[0]
            self.assertEqual(comum, esperado,
                             f"{nome}: o fundo e 0x{comum:04X}")
            # Um terço da célula, folgado: o menor dos três (o 🏎) tem 767
            # dos 1600 pixels no fundo, e o antialiasing come os vizinhos.
            self.assertGreater(quantos, 40 * 40 // 3, nome)
            # E nada de preto: era sobre ele que o transparente era
            # composto antes de 2026-10-06, e o cabecalho ainda dizia isso.
            self.assertNotIn(0x0000, valores, f"{nome}: tem pixel preto")

    def test_o_cabecalho_nao_diz_que_o_fundo_e_PRETO(self):
        # O comentário dizia "composto sobre PRETO" desde que o fundo da
        # faixa de alerta virou claro, em 2026-10-06. Quem fosse desenhar o
        # ícone noutro lugar acreditaria nele.
        texto = SPRITES.read_text(encoding="utf-8")
        self.assertNotIn("PRETO", texto)
        self.assertIn("kFundoAlerta", texto)


@unittest.skipUnless(TEM_PILLOW, "precisa do Pillow (.venv)")
class PngQueFalta(unittest.TestCase):
    """O composto não confere a existência dos PNGs — e não precisa.

    O `COMPOSTO` usa **os mesmos dois arquivos** que o laço de `SPRITES` já
    conferiu, então `main` nunca chega nele com um caminho que falta: sai
    antes, com a mensagem e o código 1. Chamada direta, a `compoe_dois`
    levanta o `FileNotFoundError` do Pillow, que nomeia o arquivo.

    Estes dois testes guardam esse par de comportamentos: é o que torna a
    conferência extra desnecessária, e se um dia o composto usar um terceiro
    arquivo, é aqui que a falta aparece.
    """

    def test_compor_sem_o_png_diz_qual_arquivo_falta(self):
        existe = S.ORIGEM / S.SPRITES[0][1]
        falta = S.ORIGEM / "nao_existe.png"
        with self.assertRaises(FileNotFoundError) as e:
            S.compoe_dois(existe, falta)
        self.assertIn("nao_existe.png", str(e.exception))

    def test_main_recusa_o_composto_que_falta_sem_traceback(self):
        import io
        import tempfile
        from contextlib import redirect_stderr, redirect_stdout
        with tempfile.TemporaryDirectory() as vazio, \
                tempfile.TemporaryDirectory() as destino:
            erro = io.StringIO()
            with redirect_stdout(io.StringIO()), redirect_stderr(erro):
                codigo = S.main(["--origem", vazio, "--destino", destino])
            self.assertEqual(codigo, 1)
            self.assertIn("nao encontrado", erro.getvalue())


if __name__ == "__main__":
    unittest.main()
