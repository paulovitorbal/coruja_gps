# coruja_nucleo: logica pura, sem uma linha de hardware. Compila no host e no Pico.
add_library(coruja_nucleo STATIC
    nucleo/Geo.cpp
    nucleo/LimiarInfracao.cpp
    nucleo/BaseRadares.cpp
)
target_include_directories(coruja_nucleo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_log: interface + implementacao de console. Portavel.
add_library(coruja_log STATIC log/LoggerConsole.cpp)
target_include_directories(coruja_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# coruja_portes: implementacoes que tocam hardware. So no alvo Pico.
# Vazia por enquanto: os componentes de hardware ainda nao foram escritos.
# Ver a tabela de estado no README.
