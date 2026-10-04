#!/bin/bash
# =============================================================================
#  mutante.sh — aplica um mutante, compila, roda a suíte, restaura
#
#  Teste de mutação é disciplina padrão deste projeto: suíte que fica verde de
#  primeira é sinal de alerta, não de sucesso. Este arnês automatiza o ciclo.
#
#    scripts/mutante.sh <arquivo> <trecho-original> <trecho-mutado> <rótulo>
#
#  O trecho original precisa ocorrer EXATAMENTE UMA VEZ no arquivo — âncora
#  ambígua aborta sem mutar, porque mutar o lugar errado produz resultado que
#  parece válido e não é.
#
#  Saída: MORTO (o que se espera) ou *** SOBREVIVEU ***, que é achado.
#
#  ## Dois detalhes que custaram caro para descobrir
#
#  **O restore precisa de `touch`.** `mv` devolve o mtime do backup, que é
#  anterior ao objeto já compilado — o make considera tudo em dia e o binário
#  do mutante SOBREVIVE no diretório de build. Isso produziu dois diagnósticos
#  falsos antes de ser percebido.
#
#  **O ctest precisa de timeout.** Mutante em laço de espera não quebra o
#  teste: PENDURA. Sem o limite, a passada inteira trava sem dizer por quê.
# =============================================================================
set -u

if [ $# -ne 4 ]; then
    echo "uso: $0 <arquivo> <original> <mutado> <rotulo>" >&2
    exit 64
fi

DIR="${CORUJA_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BUILD="${CORUJA_BUILD:-$DIR/firmware/build-host}"
TIMEOUT="${CORUJA_TIMEOUT:-60}"

ARQ="$DIR/$1"; DE="$2"; PARA="$3"; ROT="$4"

if [ ! -f "$ARQ" ]; then
    echo "[$ROT] arquivo nao encontrado: $ARQ" >&2
    exit 66
fi

cp "$ARQ" "$ARQ.mut-bak"
# Restaura E RECOMPILA. Sem o rebuild, o diretorio de build fica com o
# objeto do MUTANTE enquanto o fonte ja esta limpo -- e o proximo `ctest`
# avulso acusa falhas que nao existem mais. Custa alguns segundos e evita um
# diagnostico falso que parece defeito real.
restaura() {
    mv "$ARQ.mut-bak" "$ARQ"
    touch "$ARQ"
    cmake --build "$BUILD" -j8 > /dev/null 2>&1
}
trap 'restaura' INT TERM

python3 - "$ARQ" "$DE" "$PARA" <<'PY'
import sys
arq, de, para = sys.argv[1], sys.argv[2], sys.argv[3]
texto = open(arq).read()
n = texto.count(de)
if n != 1:
    print(f"ancora ocorre {n} vezes, precisa ocorrer 1", file=sys.stderr)
    sys.exit(9)
open(arq, "w").write(texto.replace(de, para))
PY
if [ $? -ne 0 ]; then
    restaura; echo "[$ROT] ANCORA RUIM"; exit 1
fi

if ! cmake --build "$BUILD" -j8 > /tmp/mutante-build.log 2>&1; then
    restaura; echo "[$ROT] MORTO (nao compila)"; exit 0
fi

if ctest --test-dir "$BUILD" --timeout "$TIMEOUT" > /tmp/mutante-test.log 2>&1; then
    restaura
    echo "[$ROT] *** SOBREVIVEU ***"
    exit 2
fi

restaura
mortos=$(sed -n '/tests FAILED/,$p' /tmp/mutante-test.log \
         | grep -oE '[A-Za-z0-9_.]+ \(' | tr -d ' (' | head -3 | tr '\n' ' ')
echo "[$ROT] morto por: $mortos"
exit 0
