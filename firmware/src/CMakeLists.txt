# coruja_nucleo: logica pura, sem uma linha de hardware. Compila no host e no Pico.
add_library(coruja_nucleo STATIC
    nucleo/Geo.cpp
    nucleo/LimiarInfracao.cpp
    nucleo/BaseRadares.cpp
)
target_include_directories(coruja_nucleo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_ihm: logica de interface de usuario. Portavel, sem hardware: o
# decodificador de quadratura, o anti-repique e o modo de teste sao funcoes
# puras, e e por isso que a decodificacao inteira e testada no host.
add_library(coruja_ihm STATIC
    encoder/DecodificadorQuadratura.cpp
    encoder/AntiRepique.cpp
    encoder/EventoEncoder.cpp
    app/ModoTesteEncoder.cpp
)
target_include_directories(coruja_ihm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_log: interface + implementacao de console. Portavel.
add_library(coruja_log STATIC log/LoggerConsole.cpp)
target_include_directories(coruja_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_portes: implementacoes que tocam hardware. So no alvo Pico.
if(NOT CORUJA_TESTES)
    add_library(coruja_portes STATIC
        led/LedRgbAnodoComum.cpp
        encoder/EncoderKy040.cpp
    )
    target_include_directories(coruja_portes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(coruja_portes PUBLIC
        coruja_ihm coruja_log pico_stdlib hardware_pwm hardware_gpio)
endif()
