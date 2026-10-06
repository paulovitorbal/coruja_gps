// Configuração do mbedTLS para o coruja_gps.
//
// Montada a partir do que a cadeia do servidor REALMENTE usa, medida em
// 2026-10-06 contra `coruja.bpldev.com`:
//
//     folha        CN=bpldev.com            ECDSA P-256
//     intermediaria GTS WE1                 ECDSA P-256
//     raiz         GTS Root R4              ECDSA P-384
//     negociado    TLS 1.3 / TLS_AES_256_GCM_SHA384
//     alternativa  TLS 1.2 / ECDHE-ECDSA-CHACHA20-POLY1305
//
// Tudo que não aparece aí fica de fora: cada algoritmo ligado custa flash e,
// pior, amplia a superfície de uma biblioteca que roda **antes** de qualquer
// autenticação. Num aparelho que não fala com mais ninguém, suportar o que
// ninguém oferece não é robustez — é risco de graça.
#pragma once

// ⚠️ Necessario, e nao uma conveniencia: o `altcp_tls_mbedtls.c` que o
// pico-sdk traz le campos que o mbedTLS 3.x marcou como privados
// (`ssl.out_left`, `session.start`). Sem isto a ponte do proprio SDK nao
// compila. Nada deste projeto toca nesses campos -- a permissao existe para
// o codigo de terceiro que esta entre nos e o lwIP.
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

// ------------------------------------------------------------ plataforma
//
// Sem sistema de arquivos, sem rede própria (quem fala é o lwIP), sem tempo
// nem entropia da libc. As três coisas que faltam estão em
// `rede/PlataformaMbedtls.cpp`:
//
//   mbedtls_time        a data de parede, do relógio que o NTP acertou —
//                       é ela que decide se o certificado está no prazo
//   mbedtls_ms_time     o monotônico, do tempo desde o boot
//   mbedtls_hardware_poll  entropia do TRNG do RP2350
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY          // permite trocar calloc/free
#define MBEDTLS_PLATFORM_TIME_ALT        // a hora vem do nosso relógio
#define MBEDTLS_PLATFORM_MS_TIME_ALT     // e o monotônico, do tempo de boot
#define MBEDTLS_NO_PLATFORM_ENTROPY      // a entropia vem do RP2350
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE           // valida o PERÍODO do certificado

// ------------------------------------------------------------------- TLS
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

// ⚠️ **TLS 1.3 fica de fora, e não por preferência.** Nesta versão do mbedTLS
// ele exige a camada PSA (`MBEDTLS_PSA_CRYPTO_CLIENT`, `PSA_WANT_ALG_HKDF_*`),
// que arrasta código e RAM num aparelho onde a RAM é o recurso escasso. O
// `check_config.h` recusa compilar sem ela — foi assim que isto apareceu.
//
// O servidor aceita TLS 1.2 com `ECDHE-ECDSA-CHACHA20-POLY1305`, medido em
// 2026-10-06. ECDHE dá sigilo adiante e o AEAD é o mesmo do 1.3; o que se
// perde é o handshake de uma volta a menos, que não importa numa operação
// que já leva dezenas de segundos.

// Nome do servidor no ClientHello. **O Cloudflare não escolhe certificado sem
// isto** — um IP só, atrás de milhões de domínios, não tem como saber qual
// servir. Sem SNI o handshake falha e a mensagem não explica o motivo.
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

// ⚠️ O buffer de ENTRADA fica nos 16 KiB do padrão, e isso não é desleixo.
//
// Quem decide o tamanho do registro que chega é o **servidor**. A extensão
// `max_fragment_length` (RFC 6066) existe para pedir menos, mas o Cloudflare
// não a honra: reduzir este buffer para 4 KiB faria o handshake passar e a
// transferência morrer no primeiro registro grande — uma falha que só
// apareceria em campo, contra o servidor de verdade.
//
// São esses 16 KiB que justificam o empréstimo da memória da base.
#define MBEDTLS_SSL_IN_CONTENT_LEN   16384

// A SAÍDA, sim, é nossa: quem monta a requisição é este firmware, e o maior
// corpo que ele manda cabe muito abaixo disso. 4 KiB economizam 12 KiB de
// RAM sem depender da boa vontade de ninguém.
#define MBEDTLS_SSL_OUT_CONTENT_LEN  4096

// Só cliente, e só o que a cadeia usa.
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

// ----------------------------------------------------------- certificados
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C              // as raízes vão em PEM

// ------------------------------------------------------------- algoritmos
//
// ECDSA e ECDH para a cadeia e para a troca de chaves.
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED  // folha e intermediária
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED  // GTS Root R4 e ISRG Root X2
#define MBEDTLS_ECDSA_DETERMINISTIC
#define MBEDTLS_HMAC_DRBG_C

// As trocas de chave. ECDHE_ECDSA é a que a cadeia de hoje usa; ECDHE_RSA
// entra junto porque o Cloudflare pode rotacionar para uma folha RSA, e o
// RSA já está ligado de qualquer forma por causa da ISRG Root X1.
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

// ⚠️ RSA fica LIGADO por causa de uma raiz só: a ISRG Root X1, de 4096 bits.
// Ela está no pacote porque quem escolhe a CA do certificado é o Cloudflare,
// não o dono do aparelho — e uma rotação do lado deles, sem RSA, mataria o
// OTA. É o custo consciente dessa escolha; ver `RaizesConfiaveis.h`.
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_GENPRIME

// Cifras e resumos: exatamente os das suítes negociadas.
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C                 // o SHA-384 vive dentro dele

// ⚠️ SHA-1 e MD5 ficam de FORA. Nenhum certificado da cadeia os usa, e
// mantê-los só daria a um servidor hostil um algoritmo quebrado para
// negociar.

#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_AES_ROM_TABLES           // tabelas em flash, não em RAM
#define MBEDTLS_ERROR_C                  // mensagem legível no log

// Sem sessão retomada: o aparelho faz uma conexão por operação, com minutos
// ou dias entre elas. Guardar estado de sessão gastaria RAM para um reuso
// que não acontece.
