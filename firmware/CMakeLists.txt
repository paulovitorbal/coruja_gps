# Alvo duplo: -DCORUJA_TESTES=ON compila a suite no host (sem Pico SDK);
# sem a opcao, compila o firmware para o RP2350. Ver docs/adr/0001.
#
# 3.14 e o minimo de verdade: e onde nasceu o `FetchContent_MakeAvailable`,
# que o alvo do Pico usa.
cmake_minimum_required(VERSION 3.14)

option(CORUJA_TESTES "Compila a suite de testes no host em vez do firmware" OFF)
option(CORUJA_COBERTURA "Instrumenta para cobertura (requer CORUJA_TESTES)" OFF)
option(CORUJA_SANITIZER "ASan e UBSan na suite do host (requer CORUJA_TESTES)" OFF)

# Identificacao do build, regenerada a cada compilacao. Ver
# cmake/gera_versao.cmake para o motivo de nao ser na configuracao.
set(CORUJA_VERSAO_H ${CMAKE_BINARY_DIR}/gerado/VersaoBuild.h)
add_custom_target(coruja_versao ALL
    COMMAND ${CMAKE_COMMAND}
            -DFONTE=${CMAKE_CURRENT_SOURCE_DIR}
            -DSAIDA=${CORUJA_VERSAO_H}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gera_versao.cmake
    BYPRODUCTS ${CORUJA_VERSAO_H}
    COMMENT "Gerando identificacao de build")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# `-Wall -Wextra` nos alvos DO PROJETO, e so neles.
#
# Nao com `add_compile_options`: o `pico_sdk_init()` acrescenta o SDK como
# subdiretorio DESTE, entao a opcao desceria para o SDK, o lwIP e o mbedTLS
# junto -- milhares de avisos de codigo de terceiro, que ninguem vai
# consertar e que escondem os nossos.
#
# Sem `-Werror`, de proposito: aviso que quebra o build de quem clona hoje,
# com outro compilador, e aviso que acaba desligado.
#
# A lista de alvos sai do proprio CMake (`BUILDSYSTEM_TARGETS` das NOSSAS
# pastas), e nao escrita a mao: alvo novo ja nasce com os avisos ligados.
function(coruja_avisos_nos_alvos_de)
    foreach(PASTA IN LISTS ARGN)
        get_property(ALVOS DIRECTORY "${PASTA}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(ALVO IN LISTS ALVOS)
            get_target_property(TIPO ${ALVO} TYPE)
            # Alvo de utilidade nao compila nada, e a INTERFACE empurraria a
            # opcao para quem a usa -- inclusive para fontes de terceiros.
            if(NOT TIPO STREQUAL "UTILITY"
               AND NOT TIPO STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${ALVO} PRIVATE -Wall -Wextra)
            endif()
        endforeach()
    endforeach()
endfunction()

if(CORUJA_TESTES)
    project(coruja_gps_testes CXX)
    if(CORUJA_COBERTURA)
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
    # A receita do `build-san` da liberacao. Era feita a mao, sem
    # `-fno-sanitize-recover`: o UBSan imprimia o erro e o teste seguia
    # verde, e a etapa do sanitizer passava por cima de UB. Com ela, o
    # primeiro UB aborta o teste.
    if(CORUJA_SANITIZER)
        add_compile_options(-fsanitize=address,undefined
                            -fno-sanitize-recover=all
                            -fno-omit-frame-pointer -g -O1)
        add_link_options(-fsanitize=address,undefined)
    endif()
    enable_testing()
    add_subdirectory(src)
    add_subdirectory(test)
    add_subdirectory(ferramentas)
    coruja_avisos_nos_alvos_de(src test ferramentas)
else()
    if(CORUJA_SANITIZER)
        message(FATAL_ERROR "CORUJA_SANITIZER e da suite do host: use com -DCORUJA_TESTES=ON.")
    endif()
    include(pico_sdk_import.cmake)
    project(coruja_gps C CXX ASM)
    pico_sdk_init()

    # FAT32 no cartao. Biblioteca adotada em vez de escrita: e a implementacao
    # de referencia para SD por SPI no Pico, Apache-2.0, com suporte a RP2350.
    # Ver docs/adr/0007.
    #
    # Pelo hash do commit da tag v3.6.2, e nao pelo nome: tag se move, e o
    # que entra no binario publicado tem de ser o mesmo a cada build. Sem
    # GIT_SHALLOW, que so busca por nome de ref, e nao por hash.
    include(FetchContent)
    FetchContent_Declare(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
        GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
        GIT_TAG        6c644cfc3ab03c161fee2dd7be4877e5b832fa71  # v3.6.2
        # 🔴 **SOURCE_SUBDIR aponta de proposito para um diretorio SEM
        # CMakeLists.** Assim o FetchContent BAIXA a biblioteca e NAO adiciona
        # o CMakeLists dela ao build -- quem declara o alvo somos nos, logo
        # abaixo, sem os dois fontes do driver SDIO, que e GPL-3. Ver
        # docs/licencas/README.md e firmware/cmake/fatfs_sd_sem_sdio.cmake.
        #
        # Baixar a biblioteca inteira esta certo: o que nao pode e o codigo
        # GPL-3 entrar na imagem publicada.
        SOURCE_SUBDIR  cmake-nao-usado-de-proposito)
    FetchContent_MakeAvailable(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
    include(cmake/fatfs_sd_sem_sdio.cmake)
    add_subdirectory(src)
    # -DCORUJA_DIAGNOSTICO=ON troca o firmware pelo diagnostico de pinos.
    option(CORUJA_DIAGNOSTICO "Compila o diagnostico de pinos do encoder" OFF)
    # -DCORUJA_BANCADA_DISPLAY=ON troca o firmware pela primeira ligacao do
    # painel. Sem MISO nao ha verificacao automatica: e para olhar (R-62).
    option(CORUJA_BANCADA_DISPLAY "Compila a bancada do display" OFF)
    # -DCORUJA_BITBANG=ON: SPI na unha, sem o periferico. Diagnostico.
    option(CORUJA_BITBANG "Compila o bit-bang do display" OFF)
    # -DCORUJA_BANCADA_OTA=ON: atualizacao de ponta a ponta, na tela.
    option(CORUJA_BANCADA_OTA "Compila a bancada de atualizacao" OFF)
    if(CORUJA_DIAGNOSTICO)
        add_executable(coruja_gps src/diagnostico.cpp)
    elseif(CORUJA_BANCADA_DISPLAY)
        add_executable(coruja_gps src/bancada_display.cpp)
    elseif(CORUJA_BITBANG)
        add_executable(coruja_gps src/bancada_bitbang.cpp)
    elseif(CORUJA_BANCADA_OTA)
        add_executable(coruja_gps src/bancada_ota.cpp)
    else()
        add_executable(coruja_gps src/main.cpp)
    endif()
    # O header de versao e gerado no build, entao o executavel depende do
    # alvo que o produz e precisa do diretorio onde ele cai.
    add_dependencies(coruja_gps coruja_versao)
    target_include_directories(coruja_gps PRIVATE ${CMAKE_BINARY_DIR}/gerado)

    # Versao e hash no bloco da imagem: com as particoes A/B, o bootrom
    # escolhe a de MAIOR versao no boot. Ver docs/adr/0012.
    include(cmake/versao_imagem.cmake)
    coruja_versao_imagem(${CMAKE_CURRENT_SOURCE_DIR}
                         ${CMAKE_BINARY_DIR}/gerado/VersaoImagem.h)

    # ⚠️ SEM `ROLLBACK`: com secure boot ligado, o bootrom grava a versao
    # minima em OTP, e OTP nao volta -- uma imagem velha deixaria de subir
    # para sempre, inclusive por cabo. Hoje o secure boot esta desligado e
    # nada vai para OTP; sem a opcao, ligar o secure boot um dia nao arrasta
    # isso junto sem ninguem decidir.
    #
    # A versao entra DEPOIS do link, pelo `picotool seal` -- o mesmo passo
    # que grava o hash. Nao e `pico_set_binary_version`, de proposito: sem
    # ROLLBACK ela vira definicao de compilacao, e como a versao muda a cada
    # commit, cada commit recompilava o alvo inteiro (325 objetos, 1 min 48 s,
    # medido em 16/11/2026). Restringir a definicao ao crt0 nao adianta: o
    # gerador Makefile a guarda no `flags.make`, de que todo objeto depende.
    # No seal custa so religar.
    set_property(TARGET coruja_gps APPEND PROPERTY PICOTOOL_EXTRA_PROCESS_ARGS
        --major ${CORUJA_VERSAO_MAIOR} --minor ${CORUJA_VERSAO_MENOR})
    pico_hash_binary(coruja_gps)

    # 🔴 PILHA DE 24 KiB: o SCRATCH inteiro mais 16 KiB tomados da RAM.
    #
    # O SDK poe a pilha do nucleo 0 em SCRATCH_Y (4 KiB) e a do nucleo 1 em
    # SCRATCH_X (4 KiB), contiguos em 0x20080000 e 0x20081000. Este firmware
    # nunca liga o segundo nucleo, e a secao `.scratch_x` sai do ligador com
    # ZERO bytes (conferido no mapa), entao os dois viraram um so: 8 KiB.
    #
    # 🔴 **8 KiB NAO BASTARAM.** Em 07/10/2026 o aparelho completou o primeiro
    # handshake TLS da vida dele e voltou com `a pilha foi usada ate o fundo`
    # -- a tinta consumida inteira, ate 0x20080000. Descartadas as duas
    # explicacoes alternativas antes de crescer: `.scratch_x` e `.scratch_y`
    # saem com tamanho zero (nada escreve ali por baixo da tinta), e o heap do
    # sistema parou em ~41 KiB, com o topo a mais de 100 KiB do fundo da pilha.
    # Sobrou a leitura direta: a verificacao da cadeia, com uma raiz P-384,
    # desce mais fundo que todo o resto do programa junto.
    #
    # Transbordar nao da erro legivel: vira falta grave no guarda de pilha do
    # RP2350, e o sintoma e o aparelho TRAVADO, sem uma linha de log.
    #
    # Como crescer alem do SCRATCH: tomando RAM. O SDK declara TODAS as
    # regioes com `PROVIDE` (ver `set_memory_locations.incl`), inclusive
    # `RAM_LENGTH`, entao `--defsym` as redefine de fora sem script proprio.
    # A RAM encolhe de 512 para 496 KiB e a pilha vai de 8 para 24 KiB,
    # contigua: 0x2007C000 ate 0x20082000.
    #
    # 24 KiB e numero com folga deliberada, nao medida -- a medida que havia
    # dizia so "passou de 8". A proxima execucao com `pico da pilha` no log diz
    # o valor de verdade, e ai isto se ajusta com numero na mao.
    #
    # Sobra para o heap: a RAM estatica termina em 0x2005CE4C, entao restam
    # ~127 KiB, contra ~41 KiB de pico medido. Confortavel.
    #
    # ⚠️ Ligar o nucleo 1 passa a ser INVALIDO -- a pilha dele seria a nossa.
    # Se um dia for preciso, isto tem de ser desfeito primeiro.
    target_compile_definitions(coruja_gps PRIVATE PICO_STACK_SIZE=0x6000)

    # Quanto o firmware do CARRO espera o console USB enumerar antes de
    # comecar.
    #
    # ⚠️ **Zero, e isso vale 1,5 s em toda partida.** O valor era 1500 fixo,
    # e media-se assim na partida de 15/10/2026: o modulo tinha fix em 0,5 s
    # e quem dirigia esperava 6,0 s. Um segundo e meio daquilo era espera por
    # um console que, no carro, nunca esta ligado.
    #
    # As bancadas nao usam este `main.cpp` -- elas tem fontes proprias --,
    # entao ninguem perde o console por causa disto. Para depurar o firmware
    # do carro por USB, reconfigure com:
    #
    #     cmake -S firmware -B firmware/build-pico -DCORUJA_ESPERA_CONSOLE_MS=1500
    set(CORUJA_ESPERA_CONSOLE_MS 0 CACHE STRING
        "ms de espera pelo console USB no firmware do carro")
    target_compile_definitions(coruja_gps PRIVATE
        CORUJA_ESPERA_CONSOLE_MS=${CORUJA_ESPERA_CONSOLE_MS})
    target_link_options(coruja_gps PRIVATE
        -Wl,--defsym=RAM_LENGTH=0x7C000
        -Wl,--defsym=SCRATCH_Y_ORIGIN=0x2007C000
        -Wl,--defsym=SCRATCH_Y_LENGTH=0x6000)
    # E a guarda do que o bloco acima diz em prosa: a pilha esticada passa
    # por cima de SCRATCH_X, e isso so vale enquanto NADA mora la. O
    # `ASSERT` quebra o link no dia em que alguem puser, em vez de deixar o
    # aparelho travar sem log. Ver cmake/link_guarda/.
    pico_add_linker_script_override_path(coruja_gps
        ${CMAKE_CURRENT_LIST_DIR}/cmake/link_guarda
        FILES section_extra_post_end.incl)

    target_link_libraries(coruja_gps PRIVATE
        coruja_nucleo coruja_log coruja_ihm coruja_portes coruja_rede
        coruja_ota coruja_gps_cfg
        coruja_armazenamento
        # `pico_rand` para o sorteio do identificador de execucao. Vinha
        # por transitividade do `coruja_rede`, que o usa para a entropia do
        # TLS -- dependencia implicita que quebraria no dia em que alguem
        # reorganizasse as bibliotecas.
        pico_stdlib hardware_uart pico_rand)
    # O lwipopts.h e o cyw43 procuram cabecalhos na raiz do firmware.
    target_include_directories(coruja_gps PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    pico_enable_stdio_usb(coruja_gps 1)
    pico_enable_stdio_uart(coruja_gps 0)
    pico_add_extra_outputs(coruja_gps)
    # Por ultimo: `coruja_gps` mora nesta pasta e precisa ja existir.
    coruja_avisos_nos_alvos_de(src "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
