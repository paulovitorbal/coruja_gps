# =============================================================================
#  Gera VersaoBuild.h — identificação do build que vai ao aparelho
#
#  Existe porque em 2026-10-04 uma gravação não pegou, o aparelho ficou com
#  firmware de cinco dias antes, e NÃO HAVIA COMO SABER. O log do cartão não
#  distinguia as versões, e meia hora foi gasta lendo código que estava certo.
#
#  Roda a CADA build, não na configuração: hash capturado no `cmake` ficaria
#  velho no primeiro commit seguinte, que é exatamente o modo de falha que
#  este arquivo existe para impedir.
#
#  **Escreve só se o conteúdo mudou.** Reescrever sempre marcaria o header
#  como novo e recompilaria tudo que o inclui, a cada build.
# =============================================================================

# `git describe` em vez do hash cru. Ele diz as três coisas de uma vez:
#
#   v0.1.0                 → exatamente na tag. Firmware liberado.
#   v0.1.0-3-ga996cb1      → três commits depois dela. Build de trabalho.
#   v0.1.0-3-ga996cb1*     → e com alterações não comitadas.
#   a996cb1                → nenhuma tag alcançável (--always).
#
# O asterisco é `--dirty=*` em vez do `-dirty` padrão: a faixa de texto da
# tela tem 26 caracteres, e seis deles custam caro. Sem a marca, um build com
# alterações locais se apresentaria como a tag limpa — mentira justo na hora
# de diagnosticar.
execute_process(
    COMMAND git -C "${FONTE}" describe --tags --always --dirty=*
    OUTPUT_VARIABLE DESCRICAO OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE RC)
if(NOT RC EQUAL 0 OR DESCRICAO STREQUAL "")
    set(DESCRICAO "sem-git")
endif()

string(TIMESTAMP DATA "%d/%m/%y")

set(CONTEUDO
"#pragma once
// GERADO PELO BUILD — não edite. Ver cmake/gera_versao.cmake.
namespace coruja {
/// `git describe` da árvore no momento do build, mais a data.
constexpr const char* kVersaoBuild = \"${DESCRICAO} ${DATA}\";
}  // namespace coruja
")

set(ANTERIOR "")
if(EXISTS "${SAIDA}")
    file(READ "${SAIDA}" ANTERIOR)
endif()
if(NOT ANTERIOR STREQUAL CONTEUDO)
    file(WRITE "${SAIDA}" "${CONTEUDO}")
endif()
