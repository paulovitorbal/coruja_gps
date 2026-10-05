#!/bin/bash
# =============================================================================
#  libera_fw.sh — o pipeline de liberação, inteiro e local
#
#    scripts/libera_fw.sh [--publicar] <tag> <mensagem>
#
#  Roda na máquina, sem CI na nuvem. Por decisão do autor em 2026-10-05: num
#  projeto de estudo, build reprodutível aqui vale mais que infraestrutura
#  remota para manter.
#
#  ## As nove etapas, e por que cada uma existe
#
#   1. árvore limpa      — tag aponta para commit; com pendências, o que foi
#                          tagueado não é o que está em disco
#   2. tag inédita       — mover tag publicada quebra quem já baixou
#   3. suíte no host     — a verificação de sempre
#   4. suíte no sanitizer— pega o que o host não pega: UB, leitura inválida
#   5. alvos do Pico     — produto, bancada e OTA. Compilar só o produto
#                          deixa as bancadas quebrarem sem ninguém ver
#   6. tag anotada       — carrega mensagem, autor e data; é a que o
#                          `git describe` considera por padrão
#   7. RECONSTRÓI        — **a ordem que o incidente de 2026-10-04 ensinou**:
#                          o identificador vem do `git describe` no momento
#                          do BUILD, então taguear depois de compilar carimba
#                          o binário com a versão anterior
#   8. verifica          — confere a string DENTRO do .uf2. Não é promessa de
#                          que o firmware carrega a versão, é verificação
#   9. nomeia e soma     — `coruja_gps-vX.Y.Z.uf2` mais SHA-256; um
#                          `coruja_gps.uf2` solto na pasta de downloads não
#                          diz qual versão é
#
#  Com `--publicar`, acrescenta a décima: empurra commits e tag e cria a
#  release no GitHub com o binário anexado. **Sem a opção, não toca na rede.**
#
#  Por que release e não commit do binário: são ~1 MB por versão, e o git
#  guarda binário inteiro a cada mudança. Dez versões viram 10 MB de
#  histórico permanente que todo clone do repositório público paga. Asset de
#  release fica fora do banco de objetos.
# =============================================================================
set -u

DIR="${CORUJA_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BUILD_PICO="${CORUJA_BUILD_PICO:-$DIR/firmware/build-pico}"
BUILD_HOST="${CORUJA_BUILD_HOST:-$DIR/firmware/build-host}"
BUILD_SAN="${CORUJA_BUILD_SAN:-$DIR/firmware/build-san}"
UF2="$BUILD_PICO/coruja_gps.uf2"

PUBLICAR=0
if [ "${1:-}" = "--publicar" ]; then
    PUBLICAR=1
    shift
fi

if [ $# -lt 2 ]; then
    echo "uso: $0 [--publicar] <tag> <mensagem>" >&2
    echo "  ex: $0 v0.1.1 'corrige o rumo 360 no infracoes.log'" >&2
    exit 64
fi
TAG="$1"; shift
MSG="$*"

cd "$DIR" || exit 70
falhou() { echo "ERRO: $*" >&2; exit 1; }
etapa()  { echo "==> $*"; }

# --- 1. árvore limpa ---------------------------------------------------------
etapa "1/9  arvore limpa"
if [ -n "$(git status --porcelain)" ]; then
    git status --short >&2
    falhou "arvore suja. Comite ou guarde antes de liberar."
fi

# --- 2. tag inédita ----------------------------------------------------------
etapa "2/9  tag inedita"
if git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
    falhou "a tag '$TAG' ja existe."
fi

# --- 3. suíte no host --------------------------------------------------------
etapa "3/9  suite no host"
cmake --build "$BUILD_HOST" -j8 > /tmp/libera-host-build.log 2>&1 \
    || falhou "a suite nao compila. Ver /tmp/libera-host-build.log"
ctest --test-dir "$BUILD_HOST" > /tmp/libera-host-test.log 2>&1 || {
    sed -n '/tests FAILED/,$p' /tmp/libera-host-test.log >&2
    falhou "suite vermelha no host."
}
echo "     $(grep -oE '[0-9]+% tests passed.*' /tmp/libera-host-test.log)"

# --- 4. suíte no sanitizer ---------------------------------------------------
etapa "4/9  suite no sanitizer"
cmake --build "$BUILD_SAN" -j8 > /tmp/libera-san-build.log 2>&1 \
    || falhou "o sanitizer nao compila. Ver /tmp/libera-san-build.log"
ctest --test-dir "$BUILD_SAN" > /tmp/libera-san-test.log 2>&1 || {
    sed -n '/tests FAILED/,$p' /tmp/libera-san-test.log >&2
    falhou "suite vermelha no sanitizer."
}
echo "     $(grep -oE '[0-9]+% tests passed.*' /tmp/libera-san-test.log)"

# --- 5. os três alvos do Pico ------------------------------------------------
etapa "5/9  alvos do Pico"
for alvo in build-pico build-bancada build-ota; do
    cmake --build "$DIR/firmware/$alvo" -j8 > "/tmp/libera-$alvo.log" 2>&1 \
        || falhou "$alvo nao compila. Ver /tmp/libera-$alvo.log"
    echo "     $alvo ok"
done

# --- 6. a tag ----------------------------------------------------------------
etapa "6/9  criando a tag $TAG"
git tag -a "$TAG" -m "$MSG" || falhou "nao deu para criar a tag."
desfaz_tag() { echo "     (removendo a tag: git tag -d $TAG)" >&2; git tag -d "$TAG" > /dev/null; }

# --- 7. RECONSTRÓI na tag ----------------------------------------------------
etapa "7/9  reconstruindo o firmware NA tag"
cmake --build "$BUILD_PICO" -j8 > /tmp/libera-fw.log 2>&1 || {
    desfaz_tag; falhou "o firmware nao compila. Ver /tmp/libera-fw.log"
}

# --- 8. verifica a versão DENTRO do binário ----------------------------------
etapa "8/9  conferindo a versao no binario"
if ! strings "$UF2" | grep -qx "$TAG"; then
    echo "     o binario tem:" >&2
    strings "$UF2" | grep -E "^(v[0-9]|sem-git|[0-9a-f]{7})" | head -3 >&2
    desfaz_tag; falhou "o .uf2 nao carrega '$TAG'."
fi

# --- 9. nomeia e soma --------------------------------------------------------
etapa "9/9  nomeando e somando"
ARTEFATO="$BUILD_PICO/coruja_gps-$TAG.uf2"
cp "$UF2" "$ARTEFATO" || { desfaz_tag; falhou "nao copiou o artefato."; }
SOMA=$(shasum -a 256 "$ARTEFATO" | cut -d' ' -f1)
printf '%s  coruja_gps-%s.uf2\n' "$SOMA" "$TAG" > "$ARTEFATO.sha256"

echo
echo "OK — $TAG liberada e verificada"
echo "   $ARTEFATO"
echo "   $(ls -l "$ARTEFATO" | awk '{print $5}') bytes"
echo "   sha256 $SOMA"
echo "   a tela vai mostrar:  fw: $TAG"

# --- 10. publicação (só com --publicar) --------------------------------------
if [ "$PUBLICAR" -eq 0 ]; then
    echo
    echo "Nada foi para a rede. Para publicar:"
    echo "   $0 --publicar $TAG '<mensagem>'"
    exit 0
fi

echo
etapa "10  publicando"
echo "     vai empurrar para $(git remote get-url origin)"
git push --follow-tags origin HEAD > /tmp/libera-push.log 2>&1 \
    || { cat /tmp/libera-push.log >&2; falhou "o push falhou."; }
echo "     commits e tag empurrados"

gh release create "$TAG" "$ARTEFATO" "$ARTEFATO.sha256" \
    --title "$TAG" --notes "$MSG" > /tmp/libera-release.log 2>&1 \
    || { cat /tmp/libera-release.log >&2; falhou "a release falhou."; }
echo "     release criada: $(gh release view "$TAG" --json url --jq .url)"
