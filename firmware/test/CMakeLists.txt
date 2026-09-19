find_package(GTest REQUIRED)
include(GoogleTest)

# A fixture de 100 pontos sinteticos e versionada como texto iGO8 e convertida
# em tempo de build pelo proprio conversor do projeto. Isso faz a suite
# exercitar a cadeia inteira, conversor + carregador, sem depender de dado de
# terceiro e sem pular teste nenhum (ver docs/adr/0004 e test/dados/README.md).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(FIXTURE_TXT ${CMAKE_CURRENT_SOURCE_DIR}/dados/fixture_igo8.txt)
set(FIXTURE_BIN ${CMAKE_CURRENT_BINARY_DIR}/fixture.bin)
add_custom_command(
    OUTPUT ${FIXTURE_BIN}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../converte.py
            ${FIXTURE_TXT} ${FIXTURE_BIN}
    DEPENDS ${FIXTURE_TXT}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../converte.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../../formato_radares.py
    COMMENT "Convertendo a fixture iGO8 para radares.bin"
    VERBATIM)
add_custom_target(fixture DEPENDS ${FIXTURE_BIN})

# O radares.bin completo continua opcional: quem tiver uma base real de verdade
# ganha quatro testes extras de escala, que se pulam quando ela nao existe.
set(CORUJA_BASE_REAL "" CACHE FILEPATH "radares.bin completo, opcional")

add_executable(testes
    nucleo/GeoTest.cpp
    nucleo/LimiarInfracaoTest.cpp
    nucleo/BaseRadaresTest.cpp
    log/LoggerConsoleTest.cpp
    encoder/DecodificadorQuadraturaTest.cpp
    encoder/AntiRepiqueTest.cpp
    app/ModoTesteEncoderTest.cpp
    placa/PinosTest.cpp
    app/ModoCalibracaoTest.cpp
)
target_include_directories(testes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(testes fixture)
target_compile_definitions(testes PRIVATE
    CORUJA_FIXTURE_BIN="${FIXTURE_BIN}"
    CORUJA_BASE_REAL="${CORUJA_BASE_REAL}")
target_link_libraries(testes PRIVATE
    coruja_nucleo coruja_log coruja_ihm GTest::gtest GTest::gtest_main)
gtest_discover_tests(testes)
