#!/bin/bash
# =============================================================================
#  libera_fw.sh — cria a tag de uma versão de firmware e reconstrói nela
#
#    scripts/libera_fw.sh v0.1.0 "o que entrou nesta versão"
#
#  ## A ordem importa, e errá-la foi o incidente de 2026-10-04
#
#  O identificador do firmware vem do `git describe` no momento do BUILD.
#  Taguear depois de compilar deixa o binário carimbado com a versão
#  anterior — e aí o aparelho mente sobre o que está rodando, que é
#  justamente o problema que a identificação existe para resolver.
#
#  Este script impõe: árvore limpa → suíte verde → tag → RECONSTRÓI → confere
#  que a string no .uf2 bate com a tag. A última etapa fecha o laço: não é
#  promessa de que o firmware carrega a versão, é verificação.
# =============================================================================
set -u

DIR="${CORUJA_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BUILD="${CORUJA_BUILD_PICO:-$DIR/firmware/build-pico}"
BUILD_HOST="${CORUJA_BUILD_HOST:-$DIR/firmware/build-host}"
UF2="$BUILD/coruja_gps.uf2"

if [ $# -lt 2 ]; then
    echo "uso: $0 <tag> <mensagem>" >&2
    echo "  ex: $0 v0.1.0 'primeira versao com registro de viagem'" >&2
    exit 64
fi
TAG="$1"; shift
MSG="$*"

cd "$DIR" || exit 70

# --- 1. arvore limpa ---------------------------------------------------------
# Tag aponta para um commit; com alterações pendentes, o que foi tagueado não
# é o que está em disco, e o `*` do describe denunciaria isso no aparelho.
if [ -n "$(git status --porcelain)" ]; then
    echo "ERRO: arvore suja. Comite ou guarde antes de liberar." >&2
    git status --short >&2
    exit 1
fi

# --- 2. a tag ainda nao existe -----------------------------------------------
if git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
    echo "ERRO: a tag '$TAG' ja existe." >&2
    exit 1
fi

# --- 3. suite verde ----------------------------------------------------------
echo "==> rodando a suite no host"
if ! cmake --build "$BUILD_HOST" -j8 > /tmp/libera-build.log 2>&1; then
    echo "ERRO: a suite nao compila. Ver /tmp/libera-build.log" >&2
    exit 1
fi
if ! ctest --test-dir "$BUILD_HOST" --output-on-failure > /tmp/libera-test.log 2>&1; then
    echo "ERRO: suite vermelha. Ver /tmp/libera-test.log" >&2
    sed -n '/tests FAILED/,$p' /tmp/libera-test.log >&2
    exit 1
fi
echo "    $(grep -oE '[0-9]+ tests passed|tests passed out of [0-9]+' /tmp/libera-test.log | tail -1)"

# --- 4. a tag ----------------------------------------------------------------
# Anotada, nao leve: carrega mensagem, autor e data, e e a que o `git
# describe` considera por padrao.
echo "==> criando a tag $TAG"
git tag -a "$TAG" -m "$MSG" || exit 1

# --- 5. RECONSTROI na tag ----------------------------------------------------
echo "==> reconstruindo o firmware na tag"
if ! cmake --build "$BUILD" -j8 > /tmp/libera-fw.log 2>&1; then
    echo "ERRO: o firmware nao compila. Ver /tmp/libera-fw.log" >&2
    echo "A tag foi criada; remova com: git tag -d $TAG" >&2
    exit 1
fi

# --- 6. verifica que o binario carrega a tag ---------------------------------
if ! strings "$UF2" | grep -qx "$TAG"; then
    echo "ERRO: o .uf2 nao carrega '$TAG'. O que ele tem:" >&2
    strings "$UF2" | grep -E "^(v[0-9]|sem-git|[0-9a-f]{7})" | head -3 >&2
    echo "A tag foi criada; remova com: git tag -d $TAG" >&2
    exit 1
fi

echo
echo "OK — $TAG liberada e verificada no binario"
echo "   $UF2"
echo "   versao no binario: $(strings "$UF2" | grep -x "$TAG")"
echo "   $(ls -l "$UF2" | awk '{print $5" bytes"}')"
echo
echo "A tela de informacao vai mostrar:  fw: $TAG"
echo
echo "Para publicar depois:  git push --follow-tags"
