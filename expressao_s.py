"""Leitor e escritor mínimo de s-expressão, no dialeto dos arquivos do KiCad.

Existe porque os arquivos do KiCad 10 são s-expressões multilinha, e tentar
lê-las com expressão regular dá certo nos casos fáceis e erra nos difíceis —
que são justamente os que importam (um `fp_text` com parêntese dentro da
string, um átomo quebrado em duas linhas).

O dialeto é simples: lista entre parênteses, átomos separados por espaço,
strings entre aspas duplas com `\\` de escape. Não há comentário nem caractere
especial além disso.

Um nó é `list[str | list]`; o primeiro elemento costuma ser o nome.

## Átomo nu e string não são a mesma coisa

O KiCad **distingue** `passive` (palavra-chave) de `"passive"` (string), e
recusa a biblioteca inteira quando o que ele espera entre aspas vem nu —
`(property "Reference" ...)` com `Reference` sem aspas não carrega.

Por isso `carrega` devolve o que estava entre aspas como `Txt`, e `despeja`
escreve `Txt` sempre entre aspas. Decidir isso por heurística (aspas só quando
tem espaço, por exemplo) parece funcionar e produz arquivo inválido — foi
exatamente o que aconteceu em 2026-10-05.
"""
from __future__ import annotations


class Txt(str):
    """String literal: sai sempre entre aspas, mesmo sendo uma só palavra."""

    __slots__ = ()


def carrega(texto: str) -> list:
    """S-expressão em lista aninhada. Devolve o primeiro nó do topo."""
    nos, pilha, i, n = [], [], 0, len(texto)
    while i < n:
        c = texto[i]
        if c == "(":
            novo: list = []
            if pilha:
                pilha[-1].append(novo)
            else:
                nos.append(novo)
            pilha.append(novo)
            i += 1
        elif c == ")":
            if not pilha:
                raise ValueError(f"parêntese fechando a mais na posição {i}")
            pilha.pop()
            i += 1
        elif c == '"':
            j, pedacos = i + 1, []
            while j < n and texto[j] != '"':
                if texto[j] == "\\":
                    j += 1
                pedacos.append(texto[j])
                j += 1
            if j >= n:
                raise ValueError(f"string sem fechamento a partir de {i}")
            if pilha:
                pilha[-1].append(Txt("".join(pedacos)))
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not texto[j].isspace() and texto[j] not in "()":
                j += 1
            if pilha:
                pilha[-1].append(texto[i:j])
            i = j
    if pilha:
        raise ValueError("parêntese sem fechamento")
    if not nos:
        raise ValueError("nada para ler")
    return nos[0]


def filhos(no: list, nome: str) -> list[list]:
    """Sublistas diretas cujo primeiro elemento é `nome`."""
    return [f for f in no if isinstance(f, list) and f and f[0] == nome]


def filho(no: list, nome: str) -> list | None:
    achados = filhos(no, nome)
    return achados[0] if achados else None


def valor(no: list, nome: str, indice: int = 1):
    """Primeiro argumento de `(nome arg ...)`, ou None."""
    f = filho(no, nome)
    if f is None or len(f) <= indice:
        return None
    return f[indice]


def _atomo(x: str) -> str:
    """Átomo como sai no arquivo.

    `Txt` sempre entre aspas; átomo nu só quando não tem caractere que precise
    de escape. O que entrou com aspas sai com aspas — é o que faz a ida-e-volta
    reproduzir o arquivo de origem.
    """
    nu = bool(x) and all(c.isalnum() or c in "_.-+*/" for c in x)
    if nu and not isinstance(x, Txt):
        return x
    return '"' + x.replace("\\", "\\\\").replace('"', '\\"') + '"'


def despeja(no, nivel: int = 0) -> str:
    """Lista aninhada de volta para texto, indentado como o KiCad escreve."""
    if isinstance(no, str):
        return _atomo(no)
    tab = "\t" * nivel
    if not any(isinstance(f, list) for f in no):
        return tab + "(" + " ".join(_atomo(f) for f in no) + ")"
    partes = [tab + "(" + _atomo(no[0]) if no else tab + "("]
    for f in no[1:]:
        if isinstance(f, list):
            partes.append("\n" + despeja(f, nivel + 1))
        else:
            partes.append(" " + _atomo(f))
    partes.append("\n" + tab + ")")
    return "".join(partes)
