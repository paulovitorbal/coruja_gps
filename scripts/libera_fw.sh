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
#  ## As dez etapas, e por que cada uma existe
#
#   1. árvore limpa      — tag aponta para commit; com pendências, o que foi
#                          tagueado não é o que está em disco. O commit
#                          conferido aqui é ANOTADO, e é ele que a tag marca:
#                          árvore e HEAD são conferidos de novo depois de
#                          cada compilação, porque um commit feito noutro
#                          terminal durante os minutos de build levaria a tag
#                          para um commit que nenhuma suíte viu
#   2. tag inédita       — mover tag publicada quebra quem já baixou
#   3. suíte no host     — a verificação de sempre
#   4. suíte no sanitizer— pega o que o host não pega: UB, leitura inválida.
#                          A receita é a opção `CORUJA_SANITIZER` do CMake.
#                          Antes ela era feita à mão, sem
#                          `-fno-sanitize-recover`, e o UBSan imprimia o erro
#                          e seguia: a etapa passava verde por cima de UB
#   5. suítes Python     — o gerador da configuração e o servidor. Nada que
#                          rodava sempre as rodava, e dois testes do gerador
#                          ficaram vermelhos por 23 dias sem ninguém ver. A
#                          do servidor pede o Docker
#   6. alvos do Pico     — produto, bancada e OTA. Compilar só o produto
#                          deixa as bancadas quebrarem sem ninguém ver
#   7. tag anotada       — carrega mensagem, autor e data; é a que o
#                          `git describe` considera por padrão
#   8. RECONSTRÓI        — **a ordem que o incidente de 2026-10-04 ensinou**:
#                          o identificador vem do `git describe` no momento
#                          do BUILD, então taguear depois de compilar carimba
#                          o binário com a versão anterior
#   9. verifica          — confere a string DENTRO do .uf2. Não é promessa de
#                          que o firmware carrega a versão, é verificação.
#                          Confere também a versão NUMÉRICA do bloco da
#                          imagem, que é a que o bootrom usa para escolher
#                          a partição (ADR 0012)
#  10. nomeia e soma     — `coruja_gps-vX.Y.Z.uf2` mais SHA-256; um
#                          `coruja_gps.uf2` solto na pasta de downloads não
#                          diz qual versão é. Junto vai a tabela de
#                          partições, `coruja_gps-vX.Y.Z-particoes.uf2`, que
#                          a primeira instalação grava uma vez
#
#  Com `--publicar`, acrescenta a décima primeira: empurra o commit e a tag e
#  cria a release no GitHub com o binário anexado. **Sem a opção, não toca na
#  rede.**
#
#  Qualquer saída antes do fim, por erro ou Ctrl-C, desfaz a tag local: a
#  próxima execução começa do mesmo lugar. Depois do push a tag já está no
#  remoto, e fica.
#
#  Por que release e não commit do binário: são ~1 MB por versão, e o git
#  guarda binário inteiro a cada mudança. Dez versões viram 10 MB de
#  histórico permanente que todo clone do repositório público paga. Asset de
#  release fica fora do banco de objetos.
#
#  Os testes estão em `scripts/testes/test_libera_fw.py`: o script roda
#  inteiro num repositório temporário, com cmake, ctest, gh e picotool falsos.
# =============================================================================
set -u

DIR="${CORUJA_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
BUILD_PICO="${CORUJA_BUILD_PICO:-$DIR/firmware/build-pico}"
BUILD_HOST="${CORUJA_BUILD_HOST:-$DIR/firmware/build-host}"
BUILD_SAN="${CORUJA_BUILD_SAN:-$DIR/firmware/build-san}"
UF2="$BUILD_PICO/coruja_gps.uf2"
# O SDK baixa o picotool em `_deps` quando nao acha um instalado; com um
# instalado no sistema, `_deps` nao existe.
PICOTOOL="${CORUJA_PICOTOOL:-$BUILD_PICO/_deps/picotool/picotool}"
[ -x "$PICOTOOL" ] || PICOTOOL="$(command -v picotool || true)"
# Fora da arvore: um log dentro dela sujaria o que a etapa 1 conferiu.
LOGS="${CORUJA_LOGS:-/tmp}"

# Teto de uma imagem: o tamanho de cada particao em `firmware/particoes.json`.
# Uma imagem maior liga e passa nas outras conferencias, mas o bootrom recusa
# o que passa do fim da particao e a gravacao por cabo nao pega.
TETO_IMAGEM=$((1536 * 1024))

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

# Toda saida antes do fim desfaz a tag local. Antes, cada `falhou` depois da
# etapa 7 tinha de lembrar de desfaze-la; o que nao lembrasse a deixava, e a
# proxima execucao parava na etapa 2.
TAG_CRIADA=0
CONCLUIDO=0
desfaz_tag() {
    if [ "$TAG_CRIADA" -eq 1 ] && [ "$CONCLUIDO" -eq 0 ]; then
        echo "     (removendo a tag: git tag -d $TAG)" >&2
        git tag -d "$TAG" > /dev/null
    fi
}
trap desfaz_tag EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Antes de qualquer build: tag fora do formato nao tem versao numerica, e o
# `versao_imagem.cmake` pararia a configuracao de qualquer jeito.
VERSAO_IMAGEM=$(versao_esperada "$TAG") \
    || falhou "a tag '$TAG' nao e vA.B.C; a versao da imagem sai dela."

# --- 1. árvore limpa ---------------------------------------------------------
etapa "1/10 arvore limpa"
SHA=$(git rev-parse --verify -q HEAD) || falhou "nao achei o HEAD."
# Chamada de novo depois de cada compilacao. O argumento diz quando.
confere_arvore() {
    if [ -n "$(git status --porcelain)" ]; then
        git status --short >&2
        falhou "arvore suja$1. Comite ou guarde antes de liberar."
    fi
    local agora
    agora=$(git rev-parse --verify -q HEAD)
    [ "$agora" = "$SHA" ] \
        || falhou "o HEAD andou$1: era $SHA, agora ${agora:-nenhum}. A tag marcaria um commit que nenhuma suite viu."
}
confere_arvore ""

# --- 2. tag inédita ----------------------------------------------------------
etapa "2/10 tag inedita"
if git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
    falhou "a tag '$TAG' ja existe."
fi

# --- 3. suíte no host --------------------------------------------------------
etapa "3/10 suite no host"
cmake --build "$BUILD_HOST" -j8 > "$LOGS/libera-host-build.log" 2>&1 \
    || falhou "a suite nao compila. Ver $LOGS/libera-host-build.log"
ctest --test-dir "$BUILD_HOST" --output-on-failure \
    > "$LOGS/libera-host-test.log" 2>&1 || {
    sed -n '/tests FAILED/,$p' "$LOGS/libera-host-test.log" >&2
    falhou "suite vermelha no host. Ver $LOGS/libera-host-test.log"
}
echo "     $(grep -oE '[0-9]+% tests passed.*' "$LOGS/libera-host-test.log")"

# --- 4. suíte no sanitizer ---------------------------------------------------
etapa "4/10 suite no sanitizer"
# A receita mora no CMake. Um build-san sem ela, de antes da opcao ou feito
# a mao, e reconfigurado com ela.
if ! grep -qx 'CORUJA_SANITIZER:BOOL=ON' "$BUILD_SAN/CMakeCache.txt" 2> /dev/null; then
    cmake -S "$DIR/firmware" -B "$BUILD_SAN" -DCORUJA_TESTES=ON \
        -DCORUJA_SANITIZER=ON > "$LOGS/libera-san-config.log" 2>&1 \
        || falhou "o sanitizer nao configura. Ver $LOGS/libera-san-config.log"
fi
cmake --build "$BUILD_SAN" -j8 > "$LOGS/libera-san-build.log" 2>&1 \
    || falhou "o sanitizer nao compila. Ver $LOGS/libera-san-build.log"
# Com a receita, o `-fno-sanitize-recover` ja faz o UB abortar o teste. O
# halt_on_error garante o mesmo se um dia a receita perder a opcao.
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1" \
    ctest --test-dir "$BUILD_SAN" --output-on-failure \
    > "$LOGS/libera-san-test.log" 2>&1 || {
    sed -n '/tests FAILED/,$p' "$LOGS/libera-san-test.log" >&2
    falhou "suite vermelha no sanitizer. Ver $LOGS/libera-san-test.log"
}
echo "     $(grep -oE '[0-9]+% tests passed.*' "$LOGS/libera-san-test.log")"

# --- 5. suítes Python --------------------------------------------------------
etapa "5/10 suites Python"
# O gerador roda no python3 da maquina. O servidor roda na IMAGEM dele, que e
# onde ele vive: o python3 3.15 do macOS recusa o `discover` daquela pasta.
python3 -m unittest discover -s "$DIR/scripts/testes" \
    > "$LOGS/libera-py-gerador.log" 2>&1 || {
    tail -20 "$LOGS/libera-py-gerador.log" >&2
    falhou "suite do gerador vermelha."
}
echo "     gerador: $(grep -E '^(OK|FAILED)' "$LOGS/libera-py-gerador.log")"
docker build -q -t coruja-gps/api:testes "$DIR/servidor" \
    > "$LOGS/libera-py-servidor.log" 2>&1 \
    || falhou "a imagem do servidor nao constroi (o Docker esta rodando?). Ver $LOGS/libera-py-servidor.log"
docker run --rm --network none -e PYTHONDONTWRITEBYTECODE=1 \
    -v "$DIR/servidor":/repo/servidor:ro \
    -v "$DIR/formato_radares.py":/repo/formato_radares.py:ro \
    -w /repo --entrypoint python3 coruja-gps/api:testes \
    -m unittest discover -s servidor/testes \
    > "$LOGS/libera-py-servidor.log" 2>&1 || {
    tail -20 "$LOGS/libera-py-servidor.log" >&2
    falhou "suite do servidor vermelha."
}
echo "     servidor: $(grep -E '^(OK|FAILED)' "$LOGS/libera-py-servidor.log")"

# --- 6. os três alvos do Pico ------------------------------------------------
etapa "6/10 alvos do Pico"
for alvo in build-pico build-bancada build-ota; do
    cmake --build "$DIR/firmware/$alvo" -j8 > "$LOGS/libera-$alvo.log" 2>&1 \
        || falhou "$alvo nao compila. Ver $LOGS/libera-$alvo.log"
    echo "     $alvo ok"
done
confere_arvore " durante o build"

# --- 7. a tag ----------------------------------------------------------------
etapa "7/10 criando a tag $TAG em ${SHA:0:12}"
git tag -a "$TAG" "$SHA" -m "$MSG" || falhou "nao deu para criar a tag."
TAG_CRIADA=1

# --- 8. RECONSTRÓI na tag ----------------------------------------------------
etapa "8/10 reconstruindo o firmware NA tag"
cmake --build "$BUILD_PICO" -j8 > "$LOGS/libera-fw.log" 2>&1 \
    || falhou "o firmware nao compila. Ver $LOGS/libera-fw.log"
confere_arvore " durante a reconstrucao"

# --- 9. verifica a versão DENTRO do binário ----------------------------------
etapa "9/10 conferindo a versao no binario"
# ⚠️ **Nao use `strings` aqui.** O `.uf2` embala 256 bytes de dado em blocos
# de 512, com 32 de cabecalho na frente: uma string que cruza a fronteira do
# bloco fica partida e o `strings` so ve o primeiro pedaco. Aconteceu com a
# v0.2.16 em 16/10/2026 -- o ELF tinha `v0.2.16`, o `.uf2` mostrava `v0.2`, e
# uma release correta foi barrada. Portao que reprova o certo acaba desligado,
# que e pior que nao ter portao.
#
# O `versao_no_uf2.py` DESEMBALA os blocos antes de procurar, e exige a
# string inteira: `v0.2.1-3-g…` nao passa por `v0.2.1`. A conferencia
# continua sendo no artefato que vai para o aparelho.
python3 "$DIR/scripts/versao_no_uf2.py" "$UF2" "$TAG" \
    || falhou "o .uf2 nao carrega '$TAG'."

# A versao NUMERICA mora no bloco da imagem, nao numa string: e o
# `picotool` que a le. ⚠️ Uma imagem com o numero da versao ANTERIOR passa
# na conferencia de cima. Gravada por cabo num aparelho com particoes A/B,
# ela sobe e o bootrom APAGA a copia mais nova; vinda pela rede, perderia a
# escolha no boot. Ver `firmware/README.md`, migracao.
[ -n "$PICOTOOL" ] || falhou "picotool nao encontrado (nem em _deps, nem no PATH)."
INFO=$("$PICOTOOL" info -a "$UF2" 2>&1) || falhou "o picotool nao leu o .uf2: $INFO"
# Qualquer linha `version:`, e nao a primeira: com `pico_set_program_version`
# o picotool imprime antes a versao do binary info, que e texto.
if printf '%s\n' "$INFO" | grep -qxE " *version: *${VERSAO_IMAGEM//./\\.}"; then
    LIDA="$VERSAO_IMAGEM"
else
    LIDA=$(printf '%s\n' "$INFO" | sed -n 's/^ *version: *//p' | tr '\n' ' ')
    falhou "a imagem diz versao '${LIDA:-nenhuma}', a tag pede '$VERSAO_IMAGEM'. Rode cmake -S firmware -B firmware/build-pico e tente de novo."
fi
printf '%s\n' "$INFO" | grep -qE '^ *hash: *verified$' \
    || falhou "o hash da imagem nao confere."
TAMANHO_IMAGEM=$(wc -c < "$BUILD_PICO/coruja_gps.bin" | tr -d ' ')
if [ "$TAMANHO_IMAGEM" -gt "$TETO_IMAGEM" ]; then
    falhou "a imagem tem $TAMANHO_IMAGEM bytes e a particao, $TETO_IMAGEM."
fi
echo "     versao da imagem $LIDA, hash conferido, $TAMANHO_IMAGEM de $TETO_IMAGEM bytes"

# --- 10. nomeia e soma --------------------------------------------------------
etapa "10/10 nomeando e somando"
ARTEFATO="$BUILD_PICO/coruja_gps-$TAG.uf2"
cp "$UF2" "$ARTEFATO" || falhou "nao copiou o artefato."
SOMA=$(shasum -a 256 "$ARTEFATO" | cut -d' ' -f1)
printf '%s  coruja_gps-%s.uf2\n' "$SOMA" "$TAG" > "$ARTEFATO.sha256"

# A tabela de particoes vai em toda release, mesmo sem mudar: quem instala
# do zero a partir da release nao tem o picotool do build para gera-la.
TABELA="$BUILD_PICO/coruja_gps-$TAG-particoes.uf2"
"$PICOTOOL" partition create "$DIR/firmware/particoes.json" "$TABELA" \
    > "$LOGS/libera-particoes.log" 2>&1 \
    || falhou "nao gerou a tabela de particoes. Ver $LOGS/libera-particoes.log"
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

if [ "$PUBLICAR" -eq 0 ]; then
    CONCLUIDO=1
    echo
    echo "Nada foi para a rede. Para publicar, a tag local tem de sair antes"
    echo "(a etapa 2 recusa tag existente) -- o script a recria e reconstroi:"
    echo "   git tag -d $TAG && $0 --publicar $TAG '<mensagem>'"
    exit 0
fi

# --- 11. publicação (só com --publicar) --------------------------------------
echo
etapa "11  publicando"
echo "     vai empurrar para $(git remote get-url origin)"
# `--atomic`: o ramo e a tag entram juntos ou nenhum entra. Com
# `--follow-tags`, um ramo recusado deixava a tag publica sozinha. O ramo
# recebe o commit CONFERIDO, e nao o HEAD do momento.
git push --atomic origin "$SHA:refs/heads/main" "refs/tags/$TAG" \
    > "$LOGS/libera-push.log" 2>&1 \
    || { cat "$LOGS/libera-push.log" >&2; falhou "o push falhou; nada foi publicado."; }
# A tag esta no remoto: daqui em diante a local fica, para bater com ela.
CONCLUIDO=1
echo "     commit e tag empurrados"

NOTAS="$MSG

Instalação do zero, ou aparelho ainda sem partições A/B: grave **uma vez** o \`coruja_gps-$TAG-particoes.uf2\` e depois o firmware. Roteiro
em \`firmware/README.md\`, seção \"Migração para partições A/B\".

⚠️ Com as partições A/B, gravar por cabo uma versão **mais velha** apaga a mais nova que estava no aparelho."
gh release create "$TAG" "$ARTEFATO" "$ARTEFATO.sha256" \
    "$TABELA" "$TABELA.sha256" \
    --title "$TAG" --notes "$NOTAS" > "$LOGS/libera-release.log" 2>&1 \
    || { cat "$LOGS/libera-release.log" >&2; falhou "a release falhou, mas a tag e os commits JA estao no remoto. Termine a mao: gh release create $TAG $ARTEFATO $ARTEFATO.sha256 $TABELA $TABELA.sha256 --title $TAG"; }
echo "     release criada: $(gh release view "$TAG" --json url --jq .url)"
