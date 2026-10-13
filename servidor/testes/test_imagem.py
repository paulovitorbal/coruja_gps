"""O que a imagem leva tem de ser o que o servidor importa.

    python3 -m unittest discover -s servidor/testes

O `Dockerfile` copia os módulos UM A UM, de propósito: `COPY . /app` levaria
`recebidos/`, com as viagens de quem usa o aparelho, para dentro da imagem —
e daí para o registro e para o `docker history`.

O preço dessa escolha é que a lista envelhece em silêncio. Em 13/10/2026 um
módulo novo (`horario.py`) ficou de fora, a imagem subiu sem erro de
construção, e o container entrou em laço de reinício com
`ModuleNotFoundError` — depois do `push`, já em produção.
"""

import re
import sys
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))


class ImagemLevaTodosOsModulos(unittest.TestCase):

    def test_todo_modulo_do_servidor_entra_na_imagem(self):
        dockerfile = (RAIZ / "Dockerfile").read_text(encoding="utf-8")
        copiados = set()
        for linha in dockerfile.splitlines():
            if not linha.startswith("COPY "):
                continue
            # `COPY a.py b.py /app/` — o último campo é o destino.
            for campo in linha.split()[1:-1]:
                copiados.add(campo)

        no_disco = {p.name for p in RAIZ.glob("*.py")}
        faltando = sorted(no_disco - copiados)
        self.assertEqual(
            faltando, [],
            f"módulos fora do COPY do Dockerfile: {faltando}. "
            "A imagem sobe e o container morre em ModuleNotFoundError.")

    def test_a_imagem_NAO_leva_os_recebidos(self):
        # A outra metade da regra: a lista explícita existe para manter as
        # viagens fora da imagem. Um `COPY . /app` passaria no teste acima e
        # publicaria onde o carro esteve.
        dockerfile = (RAIZ / "Dockerfile").read_text(encoding="utf-8")
        for linha in dockerfile.splitlines():
            if linha.startswith("COPY "):
                self.assertNotIn(
                    " . ", linha,
                    "COPY do diretório inteiro levaria `recebidos/`")

    def test_o_servidor_nao_importa_nada_de_fora_da_imagem(self):
        # Pega o caso inverso: um import novo de módulo que nem existe no
        # diretório. `re` acha os `import x` do topo dos três módulos.
        proprios = {p.stem for p in RAIZ.glob("*.py")}
        for modulo in ("servidor.py", "viagem.py", "pagina.py"):
            texto = (RAIZ / modulo).read_text(encoding="utf-8")
            for nome in re.findall(r"^import (\w+)$", texto, re.MULTILINE):
                if nome in proprios:
                    self.assertIn(
                        f"{nome}.py",
                        (RAIZ / "Dockerfile").read_text(encoding="utf-8"),
                        f"{modulo} importa {nome}, que não vai na imagem")


if __name__ == "__main__":
    unittest.main()
