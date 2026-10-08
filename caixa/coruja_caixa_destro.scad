// =============================================================================
//  coruja_gps — caixa, MODELO B: encoder à DIREITA
//
//      buzzer  |  display  |  encoder
//
//  Mesma geometria do `coruja_caixa.scad`; só as duas colunas laterais trocam
//  de lado. Este arquivo não duplica nada — ele inclui o modelo e sobrepõe um
//  parâmetro.
//
//  Exportar:
//    OpenSCAD -o caixa_destro.stl -D 'PECA="caixa"' coruja_caixa_destro.scad
//    OpenSCAD -o tampa.stl        -D 'PECA="tampa"' coruja_caixa_destro.scad
//
//  A TAMPA É A MESMA nos dois modelos — ela não tem nada lateral, só o furo do
//  LED no centro. Imprima uma só.
//
//  ## Qual modelo escolher
//
//  Ver o cabeçalho de `coruja_caixa.scad` e o README. Em resumo: **simule o
//  gesto antes de imprimir**, no lugar onde a caixa vai ficar. O modelo A
//  (encoder à esquerda) escondeu o display do autor, que é destro — mas isso
//  depende de onde a caixa foi montada, e pode se inverter em outro painel.
// =============================================================================

include <coruja_caixa.scad>

LADO_ENCODER = "direita";
