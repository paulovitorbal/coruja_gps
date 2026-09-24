# Ferramentas de bancada: só existem no host. Elas ligam o codigo real do
# firmware a um GPS de verdade ou ao simulador, sem reimplementar nada.
add_executable(monitor_alertas monitor_alertas.cpp)
target_link_libraries(monitor_alertas PRIVATE coruja_nucleo coruja_log)
