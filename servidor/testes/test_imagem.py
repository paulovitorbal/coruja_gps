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


#: Os módulos que ficam DE FORA da imagem, de propósito.
#:
#: O `pista.py` é ferramenta de bancada: roda à mão sobre o `recebidos/` do
#: hospedeiro e nenhum módulo do serviço o importa. A exceção é explícita
#: para continuar sendo escolha -- sem a lista, tirá-lo seria indistinguível
#: do esquecimento que este arquivo existe para pegar.
FERRAMENTAS_DE_HOST = {"pista.py"}


class ImagemLevaTodosOsModulos(unittest.TestCase):

    def test_a_ferramenta_de_bancada_nao_vai_na_imagem(self):
        dockerfile = (RAIZ / "Dockerfile").read_text(encoding="utf-8")
        for linha in dockerfile.splitlines():
            if not linha.startswith("COPY "):
                continue
            for ferramenta in FERRAMENTAS_DE_HOST:
                self.assertNotIn(ferramenta, linha.split(),
                                 f"{ferramenta} nao e executado pelo servico")

    def test_a_ferramenta_de_bancada_continua_existindo(self):
        # Ela sai da IMAGEM, nao do repositorio: o README a documenta e o
        # `testes/test_pista.py` a exercita.
        for ferramenta in FERRAMENTAS_DE_HOST:
            self.assertTrue((RAIZ / ferramenta).is_file())

    def test_todo_modulo_do_servidor_entra_na_imagem(self):
        dockerfile = (RAIZ / "Dockerfile").read_text(encoding="utf-8")
        copiados = set()
        for linha in dockerfile.splitlines():
            if not linha.startswith("COPY "):
                continue
            # `COPY a.py b.py /app/` — o último campo é o destino.
            for campo in linha.split()[1:-1]:
                copiados.add(campo)

        no_disco = {p.name for p in RAIZ.glob("*.py")} - FERRAMENTAS_DE_HOST
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
        #
        # O Dockerfile é lido SEM comentários: o texto dele cita módulos em
        # prosa -- `horario.py` aparece na explicação de 13/10/2026 --, e um
        # módulo citado no comentário e ausente do COPY passaria por aqui.
        from sem_comentarios import sem_comentarios  # noqa: PLC0415
        dockerfile = sem_comentarios(
            (RAIZ / "Dockerfile").read_text(encoding="utf-8"))
        proprios = {p.stem for p in RAIZ.glob("*.py")}
        for modulo in ("servidor.py", "viagem.py", "pagina.py"):
            texto = (RAIZ / modulo).read_text(encoding="utf-8")
            for nome in re.findall(r"^import (\w+)$", texto, re.MULTILINE):
                if nome in proprios:
                    self.assertIn(
                        f"{nome}.py", dockerfile,
                        f"{modulo} importa {nome}, que não vai na imagem")


class ComposeDaApi(unittest.TestCase):
    """Como a API sobe. Lido SEM comentários: ver `sem_comentarios`."""

    def compose(self) -> str:
        from sem_comentarios import sem_comentarios  # noqa: PLC0415
        return sem_comentarios(
            (RAIZ / "docker-compose.yml").read_text(encoding="utf-8"))

    def test_a_api_tem_teto_de_memoria(self):
        # Medido em 19/11/2026: o mapa de uma viagem de 10 h passa de
        # 72 MB no pico, e o de um arquivo de 8 MB -- o maior que a
        # recepcao aceita -- chega a 319 MB. Sem teto, alguns desses ao mesmo
        # tempo empurram a maquina inteira para a troca.
        self.assertRegex(self.compose(), r"(?m)^\s+mem_limit:\s*\d+[mg]\s*$")

    def test_o_endereco_publicado_sai_do_ambiente(self):
        # O padrao continua sendo a rede local: e dela que o Pico alcanca o
        # servico. Atras da borda o certo e 127.0.0.1, e isso tem de ser
        # ajustavel pelo `.env` -- este arquivo vive no repositorio publico,
        # incluido como submodulo, e edita-lo deixa o submodulo sujo.
        self.assertIn(
            '"${CORUJA_ENDERECO_HOST:-0.0.0.0}:${CORUJA_PORTA_HOST:-8081}'
            ':8080"', self.compose())


if __name__ == "__main__":
    unittest.main()
