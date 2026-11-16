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
#                          que o firmware carrega a versão, é verificação.
#                          Confere também a versão NUMÉRICA do bloco da
#                          imagem, que é a que o bootrom usa para escolher
#                          a partição (ADR 0012)
#   9. nomeia e soma     — `coruja_gps-vX.Y.Z.uf2` mais SHA-256; um
#                          `coruja_gps.uf2` solto na pasta de downloads não
#                          diz qual versão é. Junto vai a tabela de
#                          partições, `coruja_gps-vX.Y.Z-particoes.uf2`, que
#                          a primeira instalação grava uma vez
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
PICOTOOL="$BUILD_PICO/_deps/picotool/picotool"

# A versão numérica que a imagem de uma tag TEM de carregar: vA.B.C vira
# A*100+B . C*100, a conta do `firmware/cmake/versao_imagem.cmake` com zero
# commits depois da tag. A conta é refeita aqui de propósito: se a
# configuração não rodou de novo depois da tag, o cabeçalho gerado e o seal
# estariam velhos JUNTOS, e só uma conta independente vê isso.
versao_esperada() {
    if [[ ! "$1" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        return 1
    fi
    echo "$((10#${BASH_REMATCH[1]} * 100 + 10#${BASH_REMATCH[2]})).$((10#${BASH_REMATCH[3]} * 100))"
}

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

# Antes de qualquer build: tag fora do formato nao tem versao numerica, e o
# `versao_imagem.cmake` pararia a configuracao de qualquer jeito.
VERSAO_IMAGEM=$(versao_esperada "$TAG") \
    || falhou "a tag '$TAG' nao e vA.B.C; a versao da imagem sai dela."

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
# ⚠️ **Nao use `strings` aqui.** O `.uf2` embala 256 bytes de dado em blocos
# de 512, com 32 de cabecalho na frente: uma string que cruza a fronteira do
# bloco fica partida e o `strings` so ve o primeiro pedaco. Aconteceu com a
# v0.2.16 em 16/10/2026 -- o ELF tinha `v0.2.16`, o `.uf2` mostrava `v0.2`, e
# uma release correta foi barrada. Portao que reprova o certo acaba desligado,
# que e pior que nao ter portao.
#
# O `versao_no_uf2.py` DESEMBALA os blocos antes de procurar. A conferencia
# continua sendo no artefato que vai para o aparelho.
if ! python3 "$DIR/scripts/versao_no_uf2.py" "$UF2" "$TAG"; then
    desfaz_tag; falhou "o .uf2 nao carrega '$TAG'."
fi

# A versao NUMERICA mora no bloco da imagem, nao numa string: e o
# `picotool` que a le. ⚠️ Uma imagem com o numero da versao ANTERIOR passa
# na conferencia de cima e, num aparelho com particoes A/B, perde para a
# imagem que ja esta la: o bootrom sobe a de maior versao.
INFO=$("$PICOTOOL" info -a "$UF2" 2>&1) || {
    desfaz_tag; falhou "o picotool nao leu o .uf2: $INFO"
}
LIDA=$(printf '%s\n' "$INFO" | sed -n 's/^ *version: *//p' | head -1)
if [ "$LIDA" != "$VERSAO_IMAGEM" ]; then
    desfaz_tag
    falhou "a imagem diz versao '${LIDA:-nenhuma}', a tag pede '$VERSAO_IMAGEM'. Rode cmake -S firmware -B firmware/build-pico e tente de novo."
fi
if ! printf '%s\n' "$INFO" | grep -qE '^ *hash: *verified$'; then
    desfaz_tag; falhou "o hash da imagem nao confere."
fi
echo "     versao da imagem $LIDA, hash conferido"

# --- 9. nomeia e soma --------------------------------------------------------
etapa "9/9  nomeando e somando"
ARTEFATO="$BUILD_PICO/coruja_gps-$TAG.uf2"
cp "$UF2" "$ARTEFATO" || { desfaz_tag; falhou "nao copiou o artefato."; }
SOMA=$(shasum -a 256 "$ARTEFATO" | cut -d' ' -f1)
printf '%s  coruja_gps-%s.uf2\n' "$SOMA" "$TAG" > "$ARTEFATO.sha256"

# A tabela de particoes vai em toda release, mesmo sem mudar: quem instala
# do zero a partir da release nao tem o picotool do build para gera-la.
TABELA="$BUILD_PICO/coruja_gps-$TAG-particoes.uf2"
"$PICOTOOL" partition create "$DIR/firmware/particoes.json" "$TABELA" \
    > /tmp/libera-particoes.log 2>&1 || {
    desfaz_tag; falhou "nao gerou a tabela de particoes. Ver /tmp/libera-particoes.log"
}
SOMA_TABELA=$(shasum -a 256 "$TABELA" | cut -d' ' -f1)
printf '%s  coruja_gps-%s-particoes.uf2\n' "$SOMA_TABELA" "$TAG" > "$TABELA.sha256"

echo
echo "OK — $TAG liberada e verificada"
echo "   $ARTEFATO"
echo "   $(ls -l "$ARTEFATO" | awk '{print $5}') bytes"
echo "   sha256 $SOMA"
echo "   versao da imagem $LIDA"
echo "   tabela: $TABELA"
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

NOTAS="$MSG

Instalação do zero, ou aparelho ainda sem partições A/B: grave **uma vez** o \`coruja_gps-$TAG-particoes.uf2\` e depois o firmware. Roteiro
em \`firmware/README.md\`, seção \"Migração para partições A/B\"."
gh release create "$TAG" "$ARTEFATO" "$ARTEFATO.sha256" \
    "$TABELA" "$TABELA.sha256" \
    --title "$TAG" --notes "$NOTAS" > /tmp/libera-release.log 2>&1 \
    || { cat /tmp/libera-release.log >&2; falhou "a release falhou."; }
echo "     release criada: $(gh release view "$TAG" --json url --jq .url)"
