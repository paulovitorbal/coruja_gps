#pragma once

#include "led/Cor.h"

namespace coruja::calibracao {

/// Calibração do LED RGB — **medida na placa em 2026-09-19**, não estimada.
///
/// Fica aqui, versionada, e **não** num arquivo gerado: por decisão do autor
/// em 2026-09-20, configuração é só o que varia por instalação. Estes números
/// descrevem o LED e os resistores deste projeto, e mudá-los sem medir de novo
/// só produziria cores erradas. Ver `docs/adr/0002`.
///
/// Os resistores acompanham a **cor**, não o GPIO: 330 Ω no vermelho, 470 Ω no
/// verde, 150 Ω no azul. A ordem é o inverso da sensibilidade do olho, que tem
/// pico no verde — foi o que derrubou a previsão original do R-05, que dava
/// verde e azul como invisíveis.
constexpr unsigned kResistorVermelhoOhms = 330;
constexpr unsigned kResistorVerdeOhms    = 470;
constexpr unsigned kResistorAzulOhms     = 150;

/// Cores dos quatro estados de via do RF03.4.
///
/// As compostas levam **muito pouco** do canal secundário: 19,6% de verde faz
/// âmbar, 15,7% de azul faz rosa. Os nominais que o projeto supunha — 45% e
/// 60% — erravam por 2,3× e 3,8×, e teriam dado um amarelo esverdeado e um
/// rosa lavado, quase lilás.
///
/// ✅ Critério do RF03.4 verificado na bancada: o rosa é inconfundível em
/// relação ao vermelho, e o âmbar sai âmbar claro.
constexpr Cor kSegura   {  0, 255,   0};  ///< verde — Zona Segura
constexpr Cor kAmbar    {255,  50,   0};  ///< Aproximação conforme e Semáforo
constexpr Cor kRosa     {255,   0,  40};  ///< faixa de margem (RF03.9)
constexpr Cor kPerigo   {255,   0,   0};  ///< Zona de Perigo

}  // namespace coruja::calibracao
