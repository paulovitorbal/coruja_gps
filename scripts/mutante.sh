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
#  Saída: MORTO (o que se espera, código 0) ou *** SOBREVIVEU *** (código 2),
#  que é achado. Âncora ruim sai com 1; suíte vermelha ANTES de mutar, com 3;
#  Ctrl-C, com 130. Qualquer saída devolve o arquivo original; se não
#  conseguir, sai com 74 e diz onde ele está.
#
#  ## Detalhes que custaram caro para descobrir
#
#  **O restore precisa de `touch`.** `mv` devolve o mtime do backup, que é
#  anterior ao objeto já compilado — o make considera tudo em dia e o binário
#  do mutante SOBREVIVE no diretório de build. Isso produziu dois diagnósticos
#  falsos antes de ser percebido.
#
#  **O ctest precisa de timeout.** Mutante em laço de espera não quebra o
#  teste: PENDURA. Sem o limite, a passada inteira trava sem dizer por quê.
#
#  **A suíte roda uma vez SEM o mutante.** Com ela já vermelha, todo mutante
#  sairia "morto", e a passada inteira mentiria.
#
#  **Um `.mut-bak` que já existe é de uma execução que caiu no meio**, e o
#  ORIGINAL está nele. Copiar por cima o apagaria; o script recusa começar.
#
#  **Ctrl-C para tudo.** O ctest interrompido devolve falha, e seguir adiante
#  imprimia MORTO para um mutante que ninguém terminou de testar.
#
#  Os testes estão em `scripts/testes/test_mutante.py`, com cmake e ctest
#  falsos e um fonte de mentira.
# =============================================================================
set -u

if [ $# -ne 4 ]; then
    echo "uso: $0 <arquivo> <original> <mutado> <rotulo>" >&2
    exit 64
fi

DIR="${CORUJA_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BUILD="${CORUJA_BUILD:-$DIR/firmware/build-host}"
TIMEOUT="${CORUJA_TIMEOUT:-60}"
LOGS="${CORUJA_LOGS:-/tmp}"

ARQ="$DIR/$1"; DE="$2"; PARA="$3"; ROT="$4"
BAK="$ARQ.mut-bak"

if [ ! -f "$ARQ" ]; then
    echo "[$ROT] arquivo nao encontrado: $ARQ" >&2
    exit 66
fi
if [ -e "$BAK" ]; then
    echo "[$ROT] ja existe $BAK: uma execucao anterior caiu no meio, e o" >&2
    echo "  ORIGINAL pode estar nele. Compare e restaure a mao antes de seguir:" >&2
    echo "    diff \"$BAK\" \"$ARQ\"" >&2
    echo "    mv \"$BAK\" \"$ARQ\"" >&2
    exit 73
fi

# Restaura E RECOMPILA. Sem o rebuild, o diretorio de build fica com o
# objeto do MUTANTE enquanto o fonte ja esta limpo -- e o proximo `ctest`
# avulso acusa falhas que nao existem mais. Custa alguns segundos e evita um
# diagnostico falso que parece defeito real.
#
# Roda na saida, qualquer que seja ela: so ha o que restaurar enquanto o
# backup existe.
restaura() {
    [ -e "$BAK" ] || return 0
    if ! mv "$BAK" "$ARQ"; then
        echo "[$ROT] NAO RESTAUREI: o original esta em $BAK" >&2
        return 1
    fi
    touch "$ARQ"
    cmake --build "$BUILD" -j8 > /dev/null 2>&1 \
        || echo "[$ROT] o rebuild depois de restaurar falhou: rode cmake --build $BUILD" >&2
}
# Restauracao que falha vale mais que o veredito: o fonte esta errado.
trap 'restaura || exit 74' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Linha de base: a suite tem de passar SEM o mutante.
if ! cmake --build "$BUILD" -j8 > "$LOGS/mutante-build.log" 2>&1; then
    echo "[$ROT] nao compila sem o mutante (linha de base). Ver $LOGS/mutante-build.log" >&2
    exit 3
fi
if ! ctest --test-dir "$BUILD" --timeout "$TIMEOUT" > "$LOGS/mutante-base.log" 2>&1; then
    echo "[$ROT] suite vermelha SEM o mutante (linha de base): nada a medir." >&2
    echo "  Ver $LOGS/mutante-base.log" >&2
    exit 3
fi

cp -p "$ARQ" "$BAK" || exit 1

# O mutante vai para um temporario ao lado e entra por `rename`: o arquivo
# nunca fica pela metade, nem se a escrita cair no meio. Em bytes, para o
# resto do fonte voltar identico.
python3 - "$ARQ" "$DE" "$PARA" <<'PY'
import os, sys
arq, de, para = sys.argv[1], sys.argv[2].encode(), sys.argv[3].encode()
with open(arq, "rb") as f:
    texto = f.read()
n = texto.count(de)
if n != 1:
    print(f"ancora ocorre {n} vezes, precisa ocorrer 1", file=sys.stderr)
    sys.exit(9)
novo = arq + ".mut-novo"
with open(novo, "wb") as f:
    f.write(texto.replace(de, para))
os.replace(novo, arq)
PY
if [ $? -ne 0 ]; then
    echo "[$ROT] ANCORA RUIM"; exit 1
fi

if ! cmake --build "$BUILD" -j8 > "$LOGS/mutante-build.log" 2>&1; then
    echo "[$ROT] MORTO (nao compila)"; exit 0
fi

if ctest --test-dir "$BUILD" --timeout "$TIMEOUT" > "$LOGS/mutante-test.log" 2>&1; then
    echo "[$ROT] *** SOBREVIVEU ***"
    exit 2
fi

mortos=$(sed -n '/tests FAILED/,$p' "$LOGS/mutante-test.log" \
         | grep -oE '[A-Za-z0-9_.]+ \(' | tr -d ' (' | head -3 | tr '\n' ' ')
echo "[$ROT] morto por: $mortos"
exit 0
