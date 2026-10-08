#!/bin/bash
# =============================================================================
#  gera_stl.sh — os três STLs a partir das fontes OpenSCAD
#
#      ./gera_stl.sh [diretório de saída]
#
#  Os STLs **não são versionados**: são artefato, derivável da fonte. O que o
#  repositório guarda é o `.scad`. Gerar leva alguns minutos por peça.
#
#  ## Por que ele verifica depois de gerar
#
#  Em 08/10/2026 o furo do conector foi acrescentado e a peça saiu com o rasgo
#  do cartão TAPADO: o anel de reforço, somado depois do `difference`, descia
#  por trás do recorte e o preenchia. O render dizia `Simple: yes`, a malha
#  tinha o furo no lugar certo, e nada acusou — o defeito apareceu quando
#  alguém abriu o modelo e olhou.
#
#  Por isso o script roda o `verifica_caixa.py` em cada corpo e **falha** se
#  houver material dentro de um vão. O `.scad` também tem `assert()` para a
#  mesma classe de erro; os dois cobrem portas diferentes, e nenhum é sobra:
#  o `assert` pega o parâmetro fora de faixa, o verificador pega a malha.
# =============================================================================
set -u

SCAD="${OPENSCAD:-/Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD}"
AQUI="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SAIDA="${1:-$AQUI}"

if [ ! -x "$SCAD" ]; then
    echo "OpenSCAD nao encontrado em '$SCAD'." >&2
    echo "Aponte com: OPENSCAD=/caminho/para/openscad $0" >&2
    exit 69
fi
mkdir -p "$SAIDA" || exit 70

gera() {   # peça · fonte · arquivo de saída
    echo "==> $3"
    if ! "$SCAD" -o "$SAIDA/$3" -D "PECA=\"$1\"" "$AQUI/$2" 2>&1 \
            | grep -vE "^$|^Compiling|^Parsing|^Saved|^Rendering|^Geometries|^ *(Top|Simple|Vertices|Halfedges|Edges|Halffacets|Facets|Volumes)"; then
        : # nada fora do ruido normal
    fi
    [ -s "$SAIDA/$3" ] || { echo "ERRO: $3 nao foi gerado" >&2; return 1; }
}

gera caixa coruja_caixa.scad        caixa.stl        || exit 1
gera caixa coruja_caixa_destro.scad caixa_destro.stl || exit 1
gera tampa coruja_caixa.scad        tampa.stl        || exit 1

echo
echo "==> conferindo os vaos da traseira"
falhou=0
for f in caixa.stl caixa_destro.stl; do
    python3 "$AQUI/verifica_caixa.py" "$SAIDA/$f" || falhou=1
done
if [ "$falhou" -ne 0 ]; then
    echo >&2
    echo "RECUSADO: ha material dentro de um vao da traseira." >&2
    echo "Nao imprima. Ver o cabecalho deste script." >&2
    exit 1
fi

echo
echo "OK — prontos em $SAIDA"
ls -la "$SAIDA"/caixa.stl "$SAIDA"/caixa_destro.stl "$SAIDA"/tampa.stl
