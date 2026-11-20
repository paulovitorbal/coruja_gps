# Ferramentas de bancada: só existem no host. Elas ligam o codigo real do
# firmware a um GPS de verdade ou ao simulador, sem reimplementar nada.

# Ler o arquivo inteiro era copiado nas tres, e nas tres sem conferir o
# `ftell`: um diretorio no lugar do arquivo virava um pedido de 18 exabytes.
# Ver LeArquivo.h.
add_library(coruja_ferramentas_apoio STATIC LeArquivo.cpp)
target_include_directories(coruja_ferramentas_apoio PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(monitor_alertas monitor_alertas.cpp)
target_link_libraries(monitor_alertas PRIVATE coruja_nucleo coruja_log
                      coruja_ferramentas_apoio)

# A previa passa pela `Aplicacao`, entao precisa dos portes de host: cartao
# em diretorio, encoder no teclado, UART na pty.
add_executable(previa_produto previa_produto.cpp VisorTerminal.cpp
               PortesHost.cpp)
target_link_libraries(previa_produto PRIVATE
    coruja_ihm coruja_gps_cfg coruja_nucleo coruja_log
    coruja_ferramentas_apoio)

# Confere um log de viagem gravado contra o avaliador real. Ver o cabecalho
# do fonte: existe para decidir entre "o firmware erra" e "a conferencia em
# Python erra", o que nenhuma reimplementacao pode fazer.
add_executable(confere_viagem confere_viagem.cpp)
target_link_libraries(confere_viagem PRIVATE coruja_ihm coruja_gps_cfg
                      coruja_nucleo coruja_log coruja_ferramentas_apoio)
