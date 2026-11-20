# =============================================================================
#  Versão da imagem — o número que o bootrom do RP2350 compara (ADR 0012)
#
#  Com a tabela de partições A/B, o bootrom escolhe no boot normal a partição
#  com a MAIOR versão gravada no bloco da imagem. (Logo depois de uma gravação
#  por cabo sobe a gravada, qualquer que seja a versão: ver a seção da
#  migração no `firmware/README.md`.) O `VersaoBuild.h` (ver
#  `gera_versao.cmake`) é texto para humanos e não serve para isso: aqui
#  precisa ser um par de números de 16 bits que só cresce.
#
#      vA.B.C-N-gHASH   →   maior = A*100 + B,   menor = C*100 + N
#      v0.2.23-4-g86d7  →   maior = 2,           menor = 2304
#
#  N são os commits depois da tag, então todo commit novo sobe a versão, e
#  uma tag nova sobe também (C cresce e N volta a zero).
#
#  ⚠️ **Roda na CONFIGURAÇÃO, ao contrário do `gera_versao.cmake`.** A versão
#  vai como argumento do `picotool seal` (ver `CMakeLists.txt`), e argumento
#  de comando só se fixa na configuração. Para o número não ficar velho, a
#  configuração depende do `logs/HEAD` (muda a cada commit, amend, checkout,
#  reset) e da pasta `refs/tags` (muda quando se cria uma tag): mexeu em
#  qualquer dos dois, o próximo `cmake --build` reconfigura sozinho.
#
#  ⚠️ O `git describe` pega a tag mais PRÓXIMA, não a maior. Num histórico
#  com merge, `v0.3.0-3` pode virar `v0.2.24-6` depois de juntar um ramo
#  tagueado, e o número regride. Com histórico linear isso não acontece.
#
#  **Escreve só se o conteúdo mudou**, pelo mesmo motivo do outro: reescrever
#  sempre recompilaria tudo que inclui o cabeçalho.
# =============================================================================

function(_coruja_grava_versao_imagem SAIDA DESCRICAO MAIOR MENOR)
    set(CONTEUDO
"#pragma once
// GERADO NA CONFIGURAÇÃO — não edite. Ver cmake/versao_imagem.cmake.
#include <cstdint>

namespace coruja {
/// Versão gravada no bloco da imagem, a que o bootrom compara para escolher
/// entre as partições A e B. Derivada de `${DESCRICAO}`.
///
/// O firmware usa o mesmo par para recusar, pela rede, uma imagem que não
/// seja mais nova que a que está rodando.
constexpr std::uint16_t kVersaoImagemMaior = ${MAIOR};
constexpr std::uint16_t kVersaoImagemMenor = ${MENOR};
}  // namespace coruja
")
    set(ANTERIOR "")
    if(EXISTS "${SAIDA}")
        file(READ "${SAIDA}" ANTERIOR)
    endif()
    if(NOT ANTERIOR STREQUAL CONTEUDO)
        file(WRITE "${SAIDA}" "${CONTEUDO}")
    endif()
    message(STATUS "Versao da imagem: ${MAIOR}.${MENOR} (${DESCRICAO})")
endfunction()

function(coruja_versao_imagem FONTE SAIDA)
    # Primeiro as dependências, para valerem também no caminho sem tag: a tag
    # criada depois reconfigura sozinha. A raiz git é `dispositivo/`, não
    # `firmware/`: os caminhos vêm do próprio git. O `logs/HEAD` é do
    # diretório git da árvore de trabalho; as tags moram no diretório comum
    # (são o mesmo fora de um `git worktree`). Sem git, não há o que vigiar.
    execute_process(
        COMMAND git -C "${FONTE}" rev-parse --absolute-git-dir
        OUTPUT_VARIABLE DIR_GIT OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE RC_GIT)
    execute_process(
        COMMAND git -C "${FONTE}" rev-parse --path-format=absolute --git-common-dir
        OUTPUT_VARIABLE DIR_COMUM OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(RC_GIT EQUAL 0)
        foreach(VIGIADO "${DIR_GIT}/logs/HEAD" "${DIR_COMUM}/refs/tags")
            if(EXISTS "${VIGIADO}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                             "${VIGIADO}")
            else()
                message(WARNING
                    "versao da imagem: ${VIGIADO} nao existe. Depois de "
                    "comitar ou criar tag, rode o cmake a mao para a versao "
                    "nao envelhecer.")
            endif()
        endforeach()
    endif()

    # `--long` para sempre trazer o N, inclusive em cima da tag (`-0-g`), e
    # sem `--always`: sem tag não há número honesto a inventar.
    execute_process(
        COMMAND git -C "${FONTE}" describe --tags --long --match "v[0-9]*"
        OUTPUT_VARIABLE DESCRICAO OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE RC)
    if(NOT RC EQUAL 0)
        # Sem git, ou sem tag alcançável: o zip "Source code" da release e o
        # clone raso caem aqui. Era erro fatal, e nenhum alvo do Pico
        # configurava. Agora sai 0.0, que o `picotool seal` trata como "sem
        # versão": a imagem sobe igual, perde a escolha A/B para qualquer
        # versão de verdade, e o `scripts/libera_fw.sh` a recusa.
        message(WARNING
            "versao da imagem: nenhuma tag vA.B.C alcancavel (ou sem git). "
            "A imagem sai SEM versao (0.0). Para versionar: git tag vA.B.C "
            "e configure de novo.")
        set(DESCRICAO "sem tag")
        set(MAIOR 0)
        set(MENOR 0)
        _coruja_grava_versao_imagem("${SAIDA}" "${DESCRICAO}" ${MAIOR} ${MENOR})
        set(CORUJA_VERSAO_MAIOR ${MAIOR} PARENT_SCOPE)
        set(CORUJA_VERSAO_MENOR ${MENOR} PARENT_SCOPE)
        return()
    endif()
    if(NOT DESCRICAO MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g[0-9a-f]+$")
        message(FATAL_ERROR
            "versao da imagem: '${DESCRICAO}' nao tem a forma vA.B.C-N-gHASH.")
    endif()
    set(A ${CMAKE_MATCH_1})
    set(B ${CMAKE_MATCH_2})
    set(C ${CMAKE_MATCH_3})
    set(N ${CMAKE_MATCH_4})

    # Cada campo de 16 bits guarda dois números lado a lado; passar de 99 no
    # da direita faria duas versões diferentes darem o mesmo número, e passar
    # de 654 no da esquerda estouraria os 16 bits.
    if(N GREATER_EQUAL 100)
        # A compilacao PRE-TAG do `scripts/libera_fw.sh` (etapa 6) so quer ver
        # se compila: a etapa 8 reconstroi NA tag, e e essa imagem que se
        # publica. Com 100 ou mais commits desde a ultima tag, o guarda
        # impedia a propria liberacao que existe para criar a tag que o
        # resolve. Quem a declara (`CORUJA_BUILD_PRE_TAG=1`, so o valor 1)
        # recebe uma imagem 0.0 -- o mesmo caminho do "sem tag" acima, que o
        # `libera_fw.sh` ja recusa na etapa 9 --, e nunca um numero que
        # colida com o da tag seguinte. Sem a declaracao, o guarda segue.
        if("$ENV{CORUJA_BUILD_PRE_TAG}" STREQUAL "1")
            message(WARNING
                "versao da imagem: ${N} commits depois da tag (${DESCRICAO}); "
                "compilacao pre-tag declarada, a imagem sai SEM versao (0.0). "
                "Nao grave esta imagem: crie a tag nova e reconstrua.")
            set(DESCRICAO "${DESCRICAO} pre-tag")
            set(MAIOR 0)
            set(MENOR 0)
            _coruja_grava_versao_imagem("${SAIDA}" "${DESCRICAO}" ${MAIOR} ${MENOR})
            set(CORUJA_VERSAO_MAIOR ${MAIOR} PARENT_SCOPE)
            set(CORUJA_VERSAO_MENOR ${MENOR} PARENT_SCOPE)
            return()
        endif()
        message(FATAL_ERROR
            "versao da imagem: ${N} commits depois da tag (${DESCRICAO}). "
            "Com 100 ou mais o numero colidiria com o da tag seguinte: "
            "crie uma tag nova.")
    endif()
    if(B GREATER_EQUAL 100 OR A GREATER_EQUAL 655 OR C GREATER_EQUAL 655)
        message(FATAL_ERROR
            "versao da imagem: ${DESCRICAO} nao cabe no esquema "
            "A*100+B / C*100+N em 16 bits. Ver cmake/versao_imagem.cmake.")
    endif()
    math(EXPR MAIOR "${A} * 100 + ${B}")
    math(EXPR MENOR "${C} * 100 + ${N}")

    _coruja_grava_versao_imagem("${SAIDA}" "${DESCRICAO}" ${MAIOR} ${MENOR})
    set(CORUJA_VERSAO_MAIOR ${MAIOR} PARENT_SCOPE)
    set(CORUJA_VERSAO_MENOR ${MENOR} PARENT_SCOPE)
endfunction()
