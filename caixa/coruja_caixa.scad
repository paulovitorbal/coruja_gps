// =============================================================================
//  coruja_gps — caixa impressa em 3D
//
//  Caixa com batente interno e tampa que desce por cima, presa por atrito.
//  Sem parafuso, por decisão do autor em 2026-10-05.
//
//  Exportar:
//    OpenSCAD -o caixa.scad.stl -D 'PECA="caixa"' coruja_caixa.scad
//    OpenSCAD -o tampa.scad.stl -D 'PECA="tampa"' coruja_caixa.scad
//
//  ## O que é MEDIDO e o que é SUPOSTO
//
//  O bloco de parâmetros está dividido em dois. O primeiro saiu da peça
//  física, a paquímetro, e está no `medidas.py` do projeto. O segundo são
//  escolhas e estimativas minhas — cada uma marcada, porque caixa impressa
//  com cota suposta errada é peça para o lixo.
//
//  ## ⚠️ A tampa fica virada para o CÉU
//
//  Não é detalhe estético: a antena GPS é um patch colado na placa e enxerga o
//  céu através dela. Por isso a tampa é lisa e fina, sem reforço sobre a área
//  da antena, e o plástico precisa ser transparente a 1,5 GHz — qualquer
//  filamento com carga de fibra de carbono está fora.
//
//  Efeito colateral bom: a gravidade ajuda a tampa a ficar no lugar, então o
//  encaixe pode ser folgado em vez de apertado. Num encaixe que ninguém pode
//  testar antes de imprimir, folgado é o erro mais barato.
// =============================================================================

PECA = "ambas";      // "caixa", "tampa", "ambas", "teste" ou "cupom"

// =============================================================================
//  DE QUE LADO FICA O ENCODER
// =============================================================================
//
// "esquerda"  →  encoder | display | buzzer     (modelo A)
// "direita"   →  buzzer  | display | encoder    (modelo B)
//
// ⚠️ **Não há escolha certa em abstrato.** Depende de onde a caixa é montada no
// painel e de qual mão alcança o botão — e as duas coisas mudam por pessoa e
// por veículo.
//
// O achado que criou este parâmetro, em 2026-10-06: com o protótipo montado no
// carro e o encoder à esquerda, a mão do autor — destro — **cobria o display
// inteiro** ao girar o botão. Não apareceu em desenho nenhum; apareceu dirigindo.
//
// Quem for montar: **simule o gesto antes de imprimir.** Ponha a mão na posição
// onde o botão vai ficar, no lugar onde a caixa vai ficar, e veja o que o braço
// esconde. Cinco segundos de mímica contra horas de impressão.
LADO_ENCODER = "esquerda";

// Altura da fatia de teste: os N mm superiores da caixa, com o batente
// inteiro. Serve para validar encaixe, parede e material sem imprimir a peça
// cheia — e, no tamanho real de 130 mm, revela empenamento, que uma amostra
// pequena esconderia.
ALTURA_TESTE   =  20.0;

// Cupom de encaixe: só uma quina da caixa e a quina correspondente da tampa,
// lado a lado. Perde o teste de empenamento — que precisa do tamanho real —
// mas responde a pergunta mais cara, que é a folga do encaixe, por uma fração
// do material.
CUPOM_LADO     =  45.0;
$fn = 64;

// ----------------------------------------------------------------- MEDIDO --
// Tudo abaixo saiu da peça, a paquímetro. Ver `medidas.py`.

PCB_X          = 120.0;   // placa, largura
PCB_Y          = 120.0;   // placa, profundidade
PCB_ESP        =   1.6;   // espessura da placa

DISPLAY_X      =  70.0;   // módulo do display, placa inteira
DISPLAY_Y      =  46.7;
DISPLAY_VIS_X  =  60.8;   // área visível (vidro + moldura preta) — é o recorte
DISPLAY_VIS_Y  =  44.0;

ENCODER_X      =  19.3;   // corpo do encoder
ENCODER_Y      =  26.5;
ENCODER_EIXO   =  20.0;   // comprimento livre do eixo

BUZZER_D       =  22.4;   // corpo do buzzer
BUZZER_FUROS   =  29.6;   // entre centros das orelhas
BUZZER_FURO_D  =   2.6;

// Furos de fixação da placa, em coordenada da planta (origem na quina
// frontal esquerda, y crescendo para a traseira).
FUROS_PCB      = [[8, 112], [112, 112], [8, 8], [112, 8]];
FURO_PCB_D     =   3.2;

// ---------------------------------------------------------------- SUPOSTO --
// ⚠️ Cada um destes é escolha ou estimativa. Os marcados com (MEDIR) mudam a
// peça se estiverem errados.

PAREDE         =   3.0;   // espessura de parede e de piso
FOLGA_PCB      =   2.0;   // entre a placa e a parede, por lado
ESPACADOR      =   5.0;   // altura do pilar que levanta a placa do piso
BORDA_DISPLAY  =  14.0;   // material abaixo do display, na face frontal
                          // (era 10; subiu para 14 para dar folga sobre a PCB)

// ✅ MEDIDO em 2026-10-06: diâmetro externo do canhão roscado, com garras
// externas. 6,7 mm — compatível com rosca M7x0,75, cujo diâmetro menor fica
// perto de 6,1, o que explica os 6,0 lidos antes por dentro da porca.
//
// O furo sai em 6,7 + 0,4 de folga = 7,1 mm.
ENCODER_BUCHA  =   6.7;
LED_FURO       =  10.4;   // (MEDIR) furo do LED de 10 mm na tampa
// ⚠️ (MEDIR) — mas deliberadamente GENEROSAS enquanto não houver medida.
//
// Nestas duas, errar para mais não custa nada: a abertura fica um pouco maior
// que o necessário e ninguém nota. Errar para menos arruína a peça — cartão
// que não sai e cabo que não entra não têm conserto depois de impressa.
//
// Assimetria de custo assim pede folga, não precisão. As medidas nominais
// seriam ~14 x 3 e ~11 x 7,5.
SD_ABERTURA    = [18.0, 5.0];   // rasgo do cartão: largura x altura
USB_ABERTURA   = [14.0, 9.0];   // abertura do cabo do Pico
// ✅ MEDIDO: corpo preto do soquete, que é o que levanta o módulo acima da
// placa. O pino inteiro tem 11 mm; o que interessa aqui é o corpo.
ALTURA_MODULO  =   8.1;

// ⚠️ (MEDIR) Altura do CENTRO de cada abertura acima da base do módulo.
// Não é metade de nada: é onde fica a boca do cartão no leitor, e onde fica o
// conector no Pico. A primeira versão usava ALTURA_MODULO/2, o que punha o
// rasgo no meio do soquete, 6 mm abaixo do cartão — furo na parede errada.
SD_SLOT_Z      =   2.0;   // boca do cartao, acima da base do modulo
USB_Z          =   2.2;   // conector do Pico, acima da base dele

// --------------------------------------------------- conector de alimentacao
//
// GX12-2 ("aviacao"), na traseira. UNICO conector externo do aparelho: traz os
// 12 V do pos-chave, e o conversor fica dentro do gabinete (ADR 0009).
//
// ⚠️ **Por que GX12 e nao jack P4.** O TVS da entrada e BIDIRECIONAL: ele
// clampa transiente mas NAO bloqueia inversao de polaridade, que chega direta
// ao conversor. A propriedade que importa neste conector e o CHAVEAMENTO
// MECANICO -- so entrar de um jeito. Barril 5,5x2,1 falha nisso: centro
// positivo e convencao, nao garantia, e qualquer fonte com plugue igual entra
// invertida. O anel roscado ainda resolve vibracao, que jack nenhum resolve.
//
// ⚠️ **Centrado em X, e e por isso que ele sobe em Z.** O rasgo do cartao vai
// de x=57,7 a 75,7 e ATRAVESSA o centro da parede (x=62). Na altura dele nao
// ha como centralizar. Acima das duas aberturas sobram 39,3 mm livres, e o
// furo cabe ali inteiro com folga.
//
// Centrado porque a caixa tem versao canhota e destra: o conector no meio faz
// a traseira ficar igual nas duas, e a aparencia nao depender do lado em que
// o aparelho foi montado no painel.
GX12_FURO      =  12.5;   // (MEDIR) ⌀12 nominal + 0,5 de folga de impressao
// ⚠️ (MEDIR) Diametro que tem de PERMANECER com a espessura de parede nominal,
// para a porca do GX12 assentar e alcancar a rosca.
//
// E a razao de o reforco ser um ANEL e nao um disco: engrossar a parede sob a
// porca some com o comprimento util de rosca, e aí ela nao fecha. Quem limita
// a espessura aqui nao e a impressao -- e o comprimento roscado da peca
// comprada. Com 3 mm de parede qualquer GX12 fecha; com 4,5 mm, talvez nao.
//
// 18 mm e generoso de proposito enquanto nao houver paquímetro: errar para
// mais custa um anel um pouco menos eficaz, errar para menos arruina a peca.
GX12_VAO_PORCA =  18.0;
GX12_REFORCO_D =  28.0;   // diametro externo do anel de reforco
GX12_REFORCO   =   1.5;   // material extra, por dentro, FORA do vao da porca
// Folga entre o topo das aberturas existentes e o centro do furo novo.
GX12_FOLGA_Z   =   1.5;

// Coruja em relevo na face frontal, abaixo do encoder. Pura decoração — é o
// nome do projeto, desenhado como traço fechado.
// 19 mm cabe nos ~21 mm livres entre o piso da face e a base do encoder,
// com 1,5 mm de folga de cada lado.
CORUJA_ALT     =  19.0;   // altura total da figura
CORUJA_TRACO   =   0.9;   // espessura da linha
CORUJA_RELEVO  =   0.8;   // quanto sobressai da parede

// Nome do aparelho, em relevo abaixo do display.
TEXTO          = "Coruja GPS";
TEXTO_ALT      =   6.0;

BUZZER_SOM_D   =   2.5;   // furos de passagem de som
BUZZER_SOM_N   =     7;   // quantos

// Encaixe da tampa.
TAMPA_SAIA     =   8.0;   // quanto a saia da tampa desce por dentro
// Por lado. 0,4 é o meio-termo escolhido para um encaixe que ninguém pode
// testar: o nylon MJF varia uns ±0,3 mm, então 0,4 pode virar 0,1 (apertado)
// ou 0,7 (frouxo).
//
// Os dois extremos têm conserto em campo, e é isso que torna o meio-termo
// defensável: apertado resolve com lixa, frouxo com uma tira de fita no
// batente. O que não teria conserto seria uma tampa que trinca ao forçar.
FOLGA_ENCAIXE  =   0.4;

// --------------------------------------------------------------- DERIVADO --

INT_X = PCB_X + 2 * FOLGA_PCB;
INT_Y = PCB_Y + 2 * FOLGA_PCB;

// Altura interna: manda o display, não os componentes.
//   piso -> base do display      = BORDA_DISPLAY - PAREDE
//   + altura do display
//   + folga acima
INT_Z = (BORDA_DISPLAY - PAREDE) + DISPLAY_Y + 3.0;

EXT_X = INT_X + 2 * PAREDE;
EXT_Y = INT_Y + 2 * PAREDE;
EXT_Z = INT_Z + PAREDE;

PCB_Z = ESPACADOR;              // face inferior da placa, a partir do piso
PCB_TOPO = PCB_Z + PCB_ESP;

// Face frontal: três colunas centradas. Qual fica de cada lado depende de
// LADO_ENCODER — a largura total não muda, porque é a mesma soma.
GAP_FACE = 3.0;
CONJ_X = ENCODER_X + GAP_FACE + DISPLAY_X + GAP_FACE + BUZZER_D;
FACE_X0 = (INT_X - CONJ_X) / 2;

ENC_ESQ  = (LADO_ENCODER == "esquerda");
LARG_ESQ = ENC_ESQ ? ENCODER_X : BUZZER_D;
LARG_DIR = ENC_ESQ ? BUZZER_D  : ENCODER_X;

CX_ESQ  = FACE_X0 + LARG_ESQ / 2;
DISP_X0 = FACE_X0 + LARG_ESQ + GAP_FACE;
DISP_CX = DISP_X0 + DISPLAY_X / 2;
CX_DIR  = DISP_X0 + DISPLAY_X + GAP_FACE + LARG_DIR / 2;

ENC_CX = ENC_ESQ ? CX_ESQ : CX_DIR;
BUZ_CX = ENC_ESQ ? CX_DIR : CX_ESQ;

// A coruja acompanha o encoder, e isso não é estética: abaixo do encoder
// sobram 21,1 mm e abaixo do buzzer só 17,05 — a figura tem 19 mm e não
// caberia do lado do buzzer.

// Altura: tudo alinhado pelo centro vertical do display.
DISP_Z0 = BORDA_DISPLAY - PAREDE;
CY = DISP_Z0 + DISPLAY_Y / 2;

// --------------------------------------------------------------- módulos --

// --- a coruja -------------------------------------------------------------
//
// Desenhada por composição de primitivas, não por polígono digitado à mão:
// corpo e cabeça são elipses, os tufos das orelhas são triângulos. Sai mais
// limpo e é mais fácil de ajustar que uma lista de vinte coordenadas.

module traco(w) {
    // Contorno de uma forma 2D, com a espessura `w`. É o que transforma
    // silhueta cheia em linha.
    difference() {
        offset(delta =  w / 2) children();
        offset(delta = -w / 2) children();
    }
}

module coruja_silhueta() {
    k = CORUJA_ALT / 16;   // tudo abaixo foi desenhado numa figura de 16 mm
    union() {
        translate([0, -3 * k]) scale([6 * k, 6.5 * k]) circle(1);     // corpo
        translate([0,  4.5 * k]) circle(5.5 * k);                      // cabeça
        for (m = [-1, 1]) scale([m, 1])                                // tufos
            polygon([[1.5 * k, 8 * k], [5.2 * k, 7 * k], [5.6 * k, 10.5 * k]]);
    }
}

module coruja_2d() {
    k = CORUJA_ALT / 16;
    traco(CORUJA_TRACO) coruja_silhueta();
    for (m = [-1, 1])
        translate([m * 2.4 * k, 5 * k]) traco(CORUJA_TRACO) circle(1.9 * k);
    // ⚠️ O bico estava a 3,0k e invadia os olhos: o topo dele ficava em 3,9k e
    // a base do olho em 3,1k. Com a linha de 0,9 mm, que se espalha 0,45 para
    // cada lado, a sobreposição era ainda maior que a dos caminhos.
    //
    // A 1,0k o topo do traço do bico fica ~0,5 mm abaixo do traço do olho.
    translate([0, 1.0 * k])                                            // bico
        traco(CORUJA_TRACO)
            polygon([[-1.1 * k, 0.9 * k], [1.1 * k, 0.9 * k], [0, -1.4 * k]]);
}

module coruja_relevo() {
    // Na face externa (y = -PAREDE), saindo para fora. Abaixo do encoder,
    // centrada na coluna dele.
    z = (CY - ENCODER_Y / 2) / 2;     // meio do espaço livre abaixo do encoder
    translate([ENC_CX, -PAREDE, z])
        rotate([90, 0, 0])
            linear_extrude(height = CORUJA_RELEVO)
                coruja_2d();
}

module texto_relevo() {
    // Centrado sob o display, no meio do material que sobra abaixo dele.
    // A coruja fica na coluna do encoder, bem à esquerda, então as duas não
    // disputam espaço.
    z = DISP_Z0 / 2;
    translate([DISP_CX, -PAREDE, z])
        rotate([90, 0, 0])
            linear_extrude(height = CORUJA_RELEVO)
                text(TEXTO, size = TEXTO_ALT, halign = "center",
                     valign = "center");
}

module pilar(x, y) {
    // Pilar que levanta a placa, com furo-guia para parafuso auto-atarraxante.
    translate([x, y, 0]) difference() {
        cylinder(d = FURO_PCB_D + 4, h = ESPACADOR);
        translate([0, 0, 1]) cylinder(d = FURO_PCB_D - 0.6, h = ESPACADOR);
    }
}

module recortes_frontais() {
    // Recortes na parede frontal, que vai de y = -PAREDE ate y = 0.
    //
    // ⚠️ Comecam em y = -PAREDE-1, nao em -1. A primeira versao usava -1 e os
    // recortes cortavam so o ultimo milimetro, deixando 2 mm de parede
    // intactos — a peca abria no OpenSCAD com os furos "cegos". A traseira
    // nao tinha o problema porque la o recorte comeca para dentro da parede.
    y0 = -PAREDE - 1;
    e = PAREDE + 2;

    // Display: o recorte é a ÁREA VISÍVEL, não o módulo. A moldura preta
    // sobra por trás e é ela que encosta na parede.
    translate([DISP_CX - DISPLAY_VIS_X / 2, y0, CY - DISPLAY_VIS_Y / 2])
        cube([DISPLAY_VIS_X, e, DISPLAY_VIS_Y]);

    // Encoder: só o furo da bucha. O corpo fica por dentro.
    translate([ENC_CX, y0, CY]) rotate([-90, 0, 0])
        cylinder(d = ENCODER_BUCHA + 0.4, h = e);

    // Buzzer: dois furos de orelha e um leque de furos de som.
    for (s = [-1, 1])
        translate([BUZ_CX, y0, CY + s * BUZZER_FUROS / 2]) rotate([-90, 0, 0])
            cylinder(d = BUZZER_FURO_D, h = e);
    for (i = [0 : BUZZER_SOM_N - 1]) {
        a = i * 360 / BUZZER_SOM_N;
        translate([BUZ_CX + 6 * cos(a), y0, CY + 6 * sin(a)]) rotate([-90, 0, 0])
            cylinder(d = BUZZER_SOM_D, h = e);
    }
    translate([BUZ_CX, y0, CY]) rotate([-90, 0, 0])
        cylinder(d = BUZZER_SOM_D, h = e);
}

module recortes_traseiros() {
    // Parede traseira (y = INT_Y). Cartão e cabo, faceados com a borda.
    e = PAREDE + 2;

    // As posições em X saem da planta da placa: leitor em 52..77,4 e Pico em
    // 28..49. O centro de cada abertura acompanha o centro do módulo.
    sd_cx  = FOLGA_PCB + (52.0 + 77.4) / 2;
    usb_cx = FOLGA_PCB + (28.0 + 49.0) / 2;
    base_modulo = PCB_TOPO + ALTURA_MODULO;
    z_sd  = base_modulo + SD_SLOT_Z;
    z_usb = base_modulo + USB_Z;

    translate([sd_cx - SD_ABERTURA[0] / 2, INT_Y - 1, z_sd - SD_ABERTURA[1] / 2])
        cube([SD_ABERTURA[0], e, SD_ABERTURA[1]]);
    translate([usb_cx - USB_ABERTURA[0] / 2, INT_Y - 1, z_usb - USB_ABERTURA[1] / 2])
        cube([USB_ABERTURA[0], e, USB_ABERTURA[1]]);

    // Furo do GX12: centrado em X, acima das duas aberturas.
    translate([INT_X / 2, INT_Y - 1, gx12_cz()])
        rotate([-90, 0, 0]) cylinder(h = e, d = GX12_FURO, $fn = 64);
}

// Altura do CENTRO do furo do GX12.
//
// Derivada, e nao constante: ela depende de onde terminam as aberturas do
// cartao e do USB, que por sua vez dependem da altura dos modulos na placa.
// Fixar um numero aqui faria o furo descolar silenciosamente se qualquer um
// desses mudar -- e descolar para BAIXO significa furo em cima do rasgo do
// cartao.
//
// ⚠️ **A folga se mede pelo ANEL, nao pelo furo.** A primeira versao usava
// `GX12_FURO / 2`, e o furo passava limpo enquanto o anel de reforco, com
// raio maior, descia por tras do rasgo do cartao e o TAPAVA -- o anel e
// somado depois do `difference`, entao ele preenche o que cruzar. O furo
// existia, a peca renderizava com `Simple: yes`, e so olhando o modelo se via.
function topo_dos_recortes() =
    let (base_modulo = PCB_TOPO + ALTURA_MODULO)
    max(base_modulo + SD_SLOT_Z + SD_ABERTURA[1] / 2,
        base_modulo + USB_Z + USB_ABERTURA[1] / 2);

function gx12_cz() =
    topo_dos_recortes() + GX12_FOLGA_Z + GX12_REFORCO_D / 2;

// Guardas de geometria. Falham o render em vez de produzir uma peca que
// parece boa e chega impressa com o rasgo do cartao fechado.
assert(gx12_cz() - GX12_REFORCO_D / 2 >= topo_dos_recortes(),
       "GX12: o anel de reforco invade as aberturas do cartao/USB");
assert(gx12_cz() + GX12_REFORCO_D / 2 <= INT_Z - TAMPA_SAIA,
       "GX12: o anel de reforco bate no batente da tampa");
assert(GX12_VAO_PORCA > GX12_FURO,
       "GX12: o vao da porca tem de ser maior que o furo");
assert(GX12_REFORCO_D > GX12_VAO_PORCA,
       "GX12: o anel precisa comecar fora do vao da porca");

// Reforco interno em volta do furo: a porca do GX12 distribui o esforco pela
// face externa, mas 3 mm de parede impressa racha com tranco lateral repetido
// no cabo. O anel engrossa so onde importa, sem custo de material relevante.
module gx12_reforco() {
    translate([INT_X / 2, INT_Y - GX12_REFORCO, gx12_cz()])
        rotate([-90, 0, 0])
            difference() {
                cylinder(h = GX12_REFORCO, d = GX12_REFORCO_D, $fn = 64);
                // O vao da porca atravessa o anel inteiro: sob a porca a
                // parede continua com a espessura nominal.
                translate([0, 0, -1])
                    cylinder(h = GX12_REFORCO + 2, d = GX12_VAO_PORCA,
                             $fn = 64);
            }
}

module caixa() {
    difference() {
        union() {
            // Corpo oco, aberto em cima.
            translate([-PAREDE, -PAREDE, -PAREDE])
                cube([EXT_X, EXT_Y, EXT_Z]);
            // (o vazio é subtraído abaixo)
        }
        // Cavidade.
        translate([0, 0, 0]) cube([INT_X, INT_Y, INT_Z + 1]);
        // Batente: rebaixo no topo da parede, onde a saia da tampa entra.
        translate([-FOLGA_ENCAIXE, -FOLGA_ENCAIXE, INT_Z - TAMPA_SAIA])
            cube([INT_X + 2 * FOLGA_ENCAIXE,
                  INT_Y + 2 * FOLGA_ENCAIXE,
                  TAMPA_SAIA + PAREDE + 1]);
        recortes_frontais();
        recortes_traseiros();
    }
    // Pilares da placa, por dentro.
    for (f = FUROS_PCB) pilar(FOLGA_PCB + f[0], FOLGA_PCB + f[1]);
    // Reforco do furo do GX12. SOMADO depois do `difference`, senao o anel
    // seria cortado pelo proprio furo que ele existe para reforcar.
    gx12_reforco();
    // A coruja e o nome, em relevo na face frontal.
    coruja_relevo();
    texto_relevo();
}

module tampa() {
    // Tampa lisa com saia que desce por dentro do batente.
    difference() {
        union() {
            translate([-PAREDE, -PAREDE, 0])
                cube([EXT_X, EXT_Y, PAREDE]);
            // Saia.
            difference() {
                translate([-FOLGA_ENCAIXE, -FOLGA_ENCAIXE, -TAMPA_SAIA])
                    cube([INT_X + 2 * FOLGA_ENCAIXE,
                          INT_Y + 2 * FOLGA_ENCAIXE, TAMPA_SAIA]);
                translate([PAREDE - FOLGA_ENCAIXE, PAREDE - FOLGA_ENCAIXE,
                           -TAMPA_SAIA - 1])
                    cube([INT_X - 2 * PAREDE + 2 * FOLGA_ENCAIXE,
                          INT_Y - 2 * PAREDE + 2 * FOLGA_ENCAIXE,
                          TAMPA_SAIA + 2]);
            }
        }
        // LED: furo único, centralizado.
        translate([INT_X / 2, INT_Y / 2, -1])
            cylinder(d = LED_FURO, h = PAREDE + 2);
    }
}

if (PECA == "caixa")      caixa();
else if (PECA == "tampa") tampa();
else if (PECA == "cupom") {
    // Quina da caixa.
    translate([0, 0, -(INT_Z - ALTURA_TESTE)])
        intersection() {
            caixa();
            translate([-PAREDE - 1, -PAREDE - 1, INT_Z - ALTURA_TESTE])
                cube([CUPOM_LADO, CUPOM_LADO, ALTURA_TESTE + PAREDE + 2]);
        }
    // Quina da tampa, ao lado, virada para cima para imprimir apoiada.
    translate([CUPOM_LADO + 8, 0, TAMPA_SAIA])
        intersection() {
            tampa();
            translate([-PAREDE - 1, -PAREDE - 1, -TAMPA_SAIA - 1])
                cube([CUPOM_LADO, CUPOM_LADO, TAMPA_SAIA + PAREDE + 2]);
        }
}
else if (PECA == "teste")
    // Fatia superior, trazida para z=0 para imprimir apoiada.
    translate([0, 0, -(INT_Z - ALTURA_TESTE)])
        intersection() {
            caixa();
            translate([-PAREDE - 1, -PAREDE - 1, INT_Z - ALTURA_TESTE])
                cube([EXT_X + 2, EXT_Y + 2, ALTURA_TESTE + PAREDE + 2]);
        }
else {
    caixa();
    translate([0, EXT_Y + 10, 0]) tampa();
}
