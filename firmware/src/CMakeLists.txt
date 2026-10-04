# coruja_log: interface + implementacao de console. Portavel.
# LoggerCartao e portavel desde que recebe `Armazenamento`: ele decide
# QUANDO gravar, e nao COMO. Ver R-55.
add_library(coruja_log STATIC log/LoggerConsole.cpp log/LoggerCartao.cpp)
target_include_directories(coruja_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_nucleo: logica pura, sem uma linha de hardware. Compila no host e no Pico.
add_library(coruja_nucleo STATIC
    nucleo/Geo.cpp
    nucleo/LimiarInfracao.cpp
    nucleo/BaseRadares.cpp
    nucleo/LeitorConfig.cpp
    nucleo/Configuracao.cpp
    nucleo/LinhaConfig.cpp
    nucleo/EscritorConfig.cpp
    nucleo/GravadorConfig.cpp
    nucleo/DetectorParado.cpp
    nucleo/SeletorPeriodo.cpp
    menu/MenuAjustes.cpp
    nucleo/Crc32.cpp
    nucleo/Texto.cpp
    nucleo/Nmea.cpp
    nucleo/Url.cpp
    nucleo/VerificadorDownload.cpp
    nucleo/CarregadorFluxo.cpp
    nucleo/Zonamento.cpp
    nucleo/DetectorInfracao.cpp
    nucleo/AcumuladorViagem.cpp
    nucleo/FormatoLog.cpp
    nucleo/RetomadaViagem.cpp
    nucleo/MonitorTaxa.cpp
    nucleo/Ubx.cpp
    nucleo/PeriodoDoDia.cpp
)
target_include_directories(coruja_nucleo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# A configuracao carrega um Nivel de log, e o parser usa nivel_de_texto().
target_link_libraries(coruja_nucleo PUBLIC coruja_log)

# coruja_ota: orquestracao da atualizacao do RF05. Portavel, porque recebe
# Armazenamento, Rede, Baixador e Pausa -- abstracoes, nao o hardware. E o que
# permite testar a troca atomica e as tres tentativas no host.
# coruja_gps: configuracao do receptor. Portavel, porque recebe Uart e Pausa.
add_library(coruja_gps_cfg STATIC gps/ConfiguradorGps.cpp gps/LeitorGps.cpp)
target_include_directories(coruja_gps_cfg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(coruja_gps_cfg PUBLIC coruja_nucleo coruja_log)

add_library(coruja_ota STATIC rede/AtualizadorOta.cpp)
target_include_directories(coruja_ota PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(coruja_ota PUBLIC coruja_nucleo coruja_log)

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
    buzzer/CadenciaBuzzer.cpp
    led/PadraoLed.cpp
    led/PadraoLedOta.cpp
    display/TelaPrincipal.cpp
    display/MetricaFonte.cpp
    display/TextoRolante.cpp
    display/TelaMenu.cpp
    display/TelaOta.cpp
    display/Brilho.cpp
    app/PilotoAlerta.cpp
    app/Aplicacao.cpp
    app/OtaNaTela.cpp
    app/EsperaDispensa.cpp
)
target_include_directories(coruja_ihm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# A cadencia do buzzer parte da FaixaSonora decidida pelo Zonamento.
target_link_libraries(coruja_ihm PUBLIC coruja_nucleo coruja_gps_cfg)

# coruja_portes: implementacoes que tocam hardware. So no alvo Pico.
if(NOT CORUJA_TESTES)
    add_library(coruja_portes STATIC
        led/LedRgbAnodoComum.cpp
        encoder/EncoderKy040.cpp
        buzzer/BuzzerGpio.cpp
        placa/PausaReal.cpp
        gps/UartPico.cpp
        display/RetroiluminacaoPwm.cpp
        display/PainelSt7789.cpp
        display/DesenhaTexto.cpp
        display/VisorSt7789.cpp
    )
    target_include_directories(coruja_portes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(coruja_portes PUBLIC
        coruja_ihm coruja_log pico_stdlib hardware_pwm hardware_gpio hardware_spi)

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
    )
    # O ".." e onde mora o lwipopts.h, que o proprio lwIP inclui por nome.
    target_include_directories(coruja_rede PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(coruja_rede PUBLIC
        coruja_nucleo coruja_log coruja_armazenamento pico_stdlib
        pico_cyw43_arch_lwip_poll pico_lwip_http)
endif()
