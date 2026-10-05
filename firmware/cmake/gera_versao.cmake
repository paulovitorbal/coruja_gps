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

execute_process(
    COMMAND git -C "${FONTE}" rev-parse --short=7 HEAD
    OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE RC)
if(NOT RC EQUAL 0 OR HASH STREQUAL "")
    set(HASH "sem-git")
endif()

# Árvore suja vira um asterisco. Sem isso, um build com alterações locais
# se apresentaria como o commit limpo — e seria mentira na hora de diagnosticar.
execute_process(
    COMMAND git -C "${FONTE}" status --porcelain
    OUTPUT_VARIABLE SUJA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(SUJA STREQUAL "")
    set(MARCA "")
else()
    set(MARCA "*")
endif()

string(TIMESTAMP DATA "%d/%m/%y")

set(CONTEUDO
"#pragma once
// GERADO PELO BUILD — não edite. Ver cmake/gera_versao.cmake.
namespace coruja {
/// Hash curto do commit, `*` se a árvore tinha alterações, e a data.
constexpr const char* kVersaoBuild = \"${HASH}${MARCA} ${DATA}\";
}  // namespace coruja
")

set(ANTERIOR "")
if(EXISTS "${SAIDA}")
    file(READ "${SAIDA}" ANTERIOR)
endif()
if(NOT ANTERIOR STREQUAL CONTEUDO)
    file(WRITE "${SAIDA}" "${CONTEUDO}")
endif()
