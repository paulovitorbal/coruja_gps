"""Configuração sem comentários, para os testes que leem configuração.

Um teste que procura `mem_limit:` no texto inteiro continua verde com a linha
comentada — e "comentei só para testar" é justamente o jeito de uma proteção
sumir sem ninguém ver. Os testes de configuração leem o que vale, não o que
está escrito.
"""

from __future__ import annotations

import re


def sem_comentarios(texto: str, marcador: str = "#") -> str:
    """O texto sem as linhas de comentário e sem o comentário de fim de linha.

    O de fim de linha só conta depois de um espaço, como no YAML: assim
    `http://loki` no Alloy e `a#b` dentro de um valor continuam inteiros.
    """
    fim_de_linha = re.compile(rf"\s{re.escape(marcador)}.*$")
    linhas = []
    for linha in texto.splitlines():
        if linha.lstrip().startswith(marcador):
            continue
        linhas.append(fim_de_linha.sub("", linha).rstrip())
    return "\n".join(linhas) + "\n"
