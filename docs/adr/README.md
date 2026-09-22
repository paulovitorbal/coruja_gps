# Registros de Decisão Arquitetural

Formato Nygard: contexto, decisão, consequências. Um arquivo por decisão,
numerado e imutável — decisão revista ganha um ADR novo que **supera** o
anterior, e o antigo fica marcado como superado em vez de ser editado.

| # | Decisão | Data | Estado |
| :--- | :--- | :--- | :--- |
| [0001](0001-alvo-duplo-host-e-pico.md) | Um CMake com alvo duplo: testes no host, firmware no RP2350 | 2026-09-17 | aceita |
| [0002](0002-onde-vive-a-configuracao.md) | Credenciais no cartão, calibração compilada | 2026-09-17 | aceita |
| [0003](0003-idioma-dos-identificadores.md) | Domínio em português, infraestrutura em inglês | 2026-09-17 | aceita |
| [0004](0004-testes-contra-a-base-real.md) | Fixture sintética versionada, em vez de pular testes | 2026-09-17 | aceita |
| [0005](0005-taxa-de-amostragem-do-encoder.md) | Encoder amostrado a 1 ms, não no laço de 4 Hz | 2026-09-17 | aceita |
| [0006](0006-orcamento-de-memoria-verificado-pelo-compilador.md) | `Ponto` em 12 B e capacidade separada do teto de formato, com `static_assert` | 2026-09-17 | aceita |
| [0007](0007-fatfs-adotado-em-vez-de-escrito.md) | FAT32 por biblioteca adotada, e a partição escolhida pelo arquivo | 2026-09-20 | aceita |
| [0008](0008-formato-do-cartao.md) | Cartão em FAT32 com MBR, pela compatibilidade entre os três sistemas | 2026-09-20 | aceita |
| [0009](0009-doze-volts-dentro-do-gabinete.md) | 12 V entra no gabinete, e o buzzer é alimentado nele | 2026-09-21 | aceita |
| [0010](0010-sem-card-detect.md) | Sem card detect: cartão ausente e ilegível são o mesmo caso | 2026-09-22 | aceita |

## Decisões anteriores a este diretório

Três decisões de peso foram tomadas e justificadas antes de existir código, e
estão registradas nos documentos de especificação em vez de aqui. Não foram
duplicadas de propósito — o registro canônico é o documento:

| Decisão | Onde está | Achado |
| :--- | :--- | :--- |
| Carga integral em RAM, sem particionamento | `formato_dados.md` §1 | R-02 |
| Distância equirretangular em vez de Haversine | `formato_dados.md` §4.1 | R-03 |
| C++17 com Pico SDK em vez de MicroPython | `formato_dados.md` §9 | L-08 |
