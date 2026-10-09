# Ferramentas de bancada: só existem no host. Elas ligam o codigo real do
# firmware a um GPS de verdade ou ao simulador, sem reimplementar nada.
add_executable(monitor_alertas monitor_alertas.cpp)
target_link_libraries(monitor_alertas PRIVATE coruja_nucleo coruja_log)

# A previa passa pela `Aplicacao`, entao precisa dos portes de host: cartao
# em diretorio, encoder no teclado, UART na pty.
add_executable(previa_produto previa_produto.cpp VisorTerminal.cpp
               PortesHost.cpp)
target_link_libraries(previa_produto PRIVATE
    coruja_ihm coruja_gps_cfg coruja_nucleo coruja_log)

# Confere um log de viagem gravado contra o avaliador real. Ver o cabecalho
# do fonte: existe para decidir entre "o firmware erra" e "a conferencia em
# Python erra", o que nenhuma reimplementacao pode fazer.
add_executable(confere_viagem confere_viagem.cpp)
target_link_libraries(confere_viagem PRIVATE coruja_ihm coruja_gps_cfg
                      coruja_nucleo coruja_log)
