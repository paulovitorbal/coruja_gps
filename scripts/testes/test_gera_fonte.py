"""Testes do rasterizador de fontes.

    python3 -m unittest discover -s scripts/testes

Precisam do Pillow e da JetBrains Mono instalada; sem um dos dois, pulam.
O Pillow mora no `.venv/` do checkout, fora do git, e a fonte em
`~/Library/Fonts` -- o mesmo que o proprio gerador exige.

O que se confere e o PIXEL, e nao a intencao do codigo: a primeira versao
dizia "apoia na linha de base comum" num comentario e centralizava cada
glifo pela propria tinta. O ponto final flutuava no meio da celula, e nada
acusava.
"""
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "scripts"))

TEM_PILLOW = importlib.util.find_spec("PIL") is not None
if TEM_PILLOW:
    import gera_fonte as G  # noqa: E402

PULA = "precisa do Pillow (.venv) e da JetBrains Mono instalada"


def instalado() -> bool:
    return TEM_PILLOW and G.FONTE.exists()


def linhas_com_tinta(dados: list[int], bytes_por_linha: int,
                     altura: int) -> list[int]:
    return [y for y in range(altura)
            if any(dados[y * bytes_por_linha:(y + 1) * bytes_por_linha])]


@unittest.skipUnless(instalado(), PULA)
class LinhaDeBase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        texto = next(c for c in G.CONJUNTOS if c.nome == "Texto")
        cls.altura = texto.altura
        cls.pontos, cls.bpl, glifos = G.rasteriza(texto, G.FONTE)
        cls.glifos = dict(glifos)

    def base(self, g: str) -> int:
        """A ultima linha com tinta do glifo."""
        return linhas_com_tinta(self.glifos[g], self.bpl, self.altura)[-1]

    def test_o_ponto_fica_na_linha_de_base(self):
        self.assertEqual(self.base("."), self.base("x"),
                         "o ponto flutua acima da linha de base")

    def test_os_pes_de_a_e_k_se_alinham(self):
        self.assertEqual(self.base("a"), self.base("k"))
        self.assertEqual(self.base("a"), self.base("x"))

    def test_o_descendente_desce_abaixo_da_base(self):
        self.assertGreater(self.base("g"), self.base("x"))

    def test_nenhum_glifo_e_cortado_pela_celula(self):
        # A linha de base comum e o que pode cortar: o '$' sobe e o 'g'
        # desce, e os dois tem de caber na MESMA celula. A tinta dentro da
        # celula tem de ser a do glifo solto, numa tela folgada.
        from PIL import Image, ImageDraw, ImageFont
        fonte = ImageFont.truetype(str(G.FONTE), self.pontos)
        for g, dados in self.glifos.items():
            solto = Image.new("1", (200, 200), 0)
            ImageDraw.Draw(solto).text((50, 150), g, font=fonte, fill=1,
                                       anchor="ls")
            esperado = sum(1 for p in solto.getdata() if p)
            na_celula = sum(bin(b).count("1") for b in dados)
            self.assertEqual(na_celula, esperado, f"{g!r} foi cortado")


@unittest.skipUnless(instalado(), PULA)
class Tamanho(unittest.TestCase):
    def test_sem_tamanho_que_caiba_e_erro_e_nao_glifo_cortado(self):
        # Antes o laco parava em 4 pt e seguia com a fonte de 5 pt, que
        # tinha acabado de NAO caber -- glifo cortado, e o cabecalho dizendo
        # "4 pt" sem aviso nenhum. Celula de 3 px de largura: nem a 6 pt o
        # avanco da JetBrains Mono cabe.
        impossivel = G.Conjunto("Impossivel", 3, 40, "W", 1.0)
        with self.assertRaises(ValueError) as e:
            G.rasteriza(impossivel, G.FONTE)
        self.assertIn("Impossivel", str(e.exception))


@unittest.skipUnless(TEM_PILLOW, PULA)
class Cabecalho(unittest.TestCase):
    def test_o_numero_desenha_os_tracos_de_sem_fix(self):
        # A TelaPrincipal escreve "- -" sem fix. Glifo ausente e PULADO
        # pelo painel, sem largura: a tela ficava vazia.
        numero = next(c for c in G.CONJUNTOS if c.nome == "Numero")
        for c in "- ":
            self.assertIn(c, numero.glifos)

    def test_o_rotulo_de_aspas_e_barra_sai_escrito(self):
        conj = G.Conjunto("Rotulos", 8, 8, "'\"\\a", 1.0)
        glifos = [(g, [0] * 8) for g in conj.glifos]
        with tempfile.TemporaryDirectory() as tmp:
            destino = Path(tmp) / "FonteRotulos.h"
            G.escreve(conj, 8, 1, glifos, destino)
            texto = destino.read_text(encoding="utf-8")
        for rotulo in ("// '\\''", "// '\"'", "// '\\\\'", "// 'a'"):
            self.assertIn(rotulo, texto)
        self.assertNotIn("// '?'", texto)


if __name__ == "__main__":
    unittest.main()
