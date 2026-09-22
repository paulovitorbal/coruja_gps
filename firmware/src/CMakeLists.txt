# coruja_log: interface + implementacao de console. Portavel.
add_library(coruja_log STATIC log/LoggerConsole.cpp)
target_include_directories(coruja_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_nucleo: logica pura, sem uma linha de hardware. Compila no host e no Pico.
add_library(coruja_nucleo STATIC
    nucleo/Geo.cpp
    nucleo/LimiarInfracao.cpp
    nucleo/BaseRadares.cpp
    nucleo/LeitorConfig.cpp
    nucleo/Crc32.cpp
    nucleo/Texto.cpp
    nucleo/Url.cpp
    nucleo/VerificadorDownload.cpp
    nucleo/CarregadorFluxo.cpp
)
target_include_directories(coruja_nucleo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# A configuracao carrega um Nivel de log, e o parser usa nivel_de_texto().
target_link_libraries(coruja_nucleo PUBLIC coruja_log)

# coruja_ihm: logica de interface de usuario. Portavel, sem hardware: o
# decodificador de quadratura, o anti-repique e o modo de teste sao funcoes
# puras, e e por isso que a decodificacao inteira e testada no host.
add_library(coruja_ihm STATIC
    encoder/DecodificadorQuadratura.cpp
    encoder/AntiRepique.cpp
    encoder/EventoEncoder.cpp
    app/ModoTesteEncoder.cpp
    app/ItemCalibracao.cpp
    app/ModoCalibracao.cpp
    app/CicloCores.cpp
)
target_include_directories(coruja_ihm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_portes: implementacoes que tocam hardware. So no alvo Pico.
if(NOT CORUJA_TESTES)
    add_library(coruja_portes STATIC
        led/LedRgbAnodoComum.cpp
        encoder/EncoderKy040.cpp
    )
    target_include_directories(coruja_portes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(coruja_portes PUBLIC
        coruja_ihm coruja_log pico_stdlib hardware_pwm hardware_gpio)

    # coruja_armazenamento: cartao microSD e FAT32.
    #
    # O BEFORE no include da biblioteca e o que faz o NOSSO ffconf.h vencer o
    # dela -- e com ele a sondagem de particoes do R-38. Se essa ordem se
    # perder, o static_assert do CartaoSd.cpp quebra o build em vez de deixar
    # a funcionalidade sumir em silencio.
    add_library(coruja_armazenamento STATIC
        armazenamento/CartaoSd.cpp
        armazenamento/hw_config.cpp
        # O logger de cartao vive aqui, e nao em coruja_log, porque depende do
        # CartaoSd -- e coruja_log e portavel, compila no host.
        log/LoggerCartao.cpp
    )
    target_include_directories(coruja_armazenamento PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/armazenamento)
    target_include_directories(no-OS-FatFS-SD-SDIO-SPI-RPi-Pico BEFORE INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/armazenamento)
    # Mensagens de erro da propria biblioteca, pelo printf que ja temos no
    # USB. Sem isto a falha de nivel SPI -- que e onde as fiacoes erradas
    # aparecem -- e engolida antes de virar FRESULT.
    target_compile_definitions(coruja_armazenamento PUBLIC USE_PRINTF=1)

    target_link_libraries(coruja_armazenamento PUBLIC
        coruja_nucleo coruja_log pico_stdlib hardware_spi
        no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)

    # coruja_rede: Wi-Fi e HTTP. Separada da coruja_portes porque arrasta o
    # CYW43 e o lwIP inteiros — quem nao atualiza nao deveria pagar por eles.
    add_library(coruja_rede STATIC
        rede/RedeWifi.cpp
        rede/ClienteHttp.cpp
        rede/AtualizadorOta.cpp
    )
    # O ".." e onde mora o lwipopts.h, que o proprio lwIP inclui por nome.
    target_include_directories(coruja_rede PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(coruja_rede PUBLIC
        coruja_nucleo coruja_log coruja_armazenamento pico_stdlib
        pico_cyw43_arch_lwip_poll pico_lwip_http)
endif()
