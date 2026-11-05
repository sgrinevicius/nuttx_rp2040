//######################################################################################################################
// *
// *  Arquivo: "boards/arm/rp2040/raspberrypi-pico-w/src/rp2040_autoleds.c"
// *
// *  Título: Auto-LED da Raspberry Pi Pico W — LED Verde (GPIO 22) como Indicador de Estado do SO
// *
// *  Design Pattern: Adaptador de Placa — Traduz a API genérica de auto-LEDs do NuttX ('board_autoled_*') para as
// *                  operações específicas de GPIO do RP2040.
// *
// *  Objetivo: Fornecer as três funções de placa exigidas pelo subsistema de auto-LEDs do NuttX, mapeando o LED
// *            lógico para o pino físico GPIO 22 (LED verde externo).
// *
// *  Hardware: Raspberry Pi Pico W (RP2040) + 5 LEDs externos
// *  RTOS: Apache NuttX 13.0.0
// *
// * Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.  
// * See the NOTICE file distributed with this work for additional information regarding copyright ownership.  
// * The ASF licenses this file to you under the Apache License, Version 2.0 (the "License"); you may not use this file 
// * except in compliance with the License.  You may obtain a copy of the License at:
// *
// *   http://www.apache.org/licenses/LICENSE-2.0
// *
// * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on 
// * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
// * See the License for the specific language governing permissions and limitations under the License.
// *
// *--------------------------------------------------------------------------------------------------------------------
// *  Modelo: BSP de Placa (Categoria 4 da Taxonomia).
// *
// *  FILOSOFIA E PAPEL ESTRUTURAL NA ARQUITETURA DE PLACA:
// *
// *    O subsistema de auto-LEDs existe para responder a uma única pergunta: como um sistema embarcado sem
// *    console serial informa ao operador que o boot terminou bem ou que travou? A resposta é um LED controlado
// *    inteiramente pelo SO, cujo padrão de piscar codifica o estado do sistema.
// *
// *    O auto-LED é iniciado antes de qualquer driver de aplicação. Isso significa que este arquivo não pode
// *    depender de subsistemas complexos (I2C, SPI, VFS, alocação dinâmica). Toda a implementação usa apenas
// *    'rp2040_gpio_init', 'rp2040_gpio_setdir' e 'rp2040_gpio_put' — funções que escrevem diretamente em
// *    registradores do RP2040, sem intermediários.
// *
// *  HISTÓRIA E CRÍTICA DO SUBSISTEMA (para o leitor que chegou até aqui):
// *
// *    Este subsistema é herança do Linux. O kernel tem algo equivalente em 'drivers/leds/leds-gpio.c' e no
// *    framework 'LED_TRIGGER_*', onde LEDs de bordo se associam a eventos do sistema — heartbeat do kernel,
// *    atividade de disco, carga de CPU, etc. A ideia era boa: dar ao operador um canal visual mínimo para
// *    saber se a máquina está viva, sem precisar de console.
// *
// *    O NuttX importou o conceito, mas o simplificou até virar uma tabela de oito símbolos para apenas três
// *    comportamentos visuais distintos: DESLIGADO (LED_STARTED, LED_HEAPALLOCATE, LED_IRQSENABLED), LIGADO
// *    (LED_STACKCREATED) e PISCANDO (LED_PANIC). Os quatro símbolos intermediários — LED_INIRQ, LED_SIGNAL,
// *    LED_ASSERTION e o próprio valor "2" usado internamente — colapsam todos no mesmo comportamento: NÃO
// *    MEXER NO LED. Ou seja: existem para serem ignorados.
// *
// *    A pergunta honesta é: por que um subsistema com três comportamentos úteis precisa de oito símbolos? A
// *    resposta é arqueológica. Quando o NuttX foi escrito, fazia sentido reservar espaço para estados que
// *    "poderiam vir a ser usados". Nenhum BSP em uma década usou a distinção entre LED_INIRQ e LED_ASSERTION.
// *    Ninguém olha para um LED aceso e pensa "ah, o sistema está dentro de uma IRQ". O LED aceso significa uma
// *    coisa só: o boot terminou. Depois disso, ele vira ruído visual.
// *
// *    Mas o subsistema é API pública há mais de uma década. Há dezenas de BSPs usando os oito símbolos. O custo
// *    de refatorar excede o de conviver com a redundância. Este arquivo é um adaptador para uma interface
// *    antiga, não uma tentativa de modernizá-la. É por isso que 'board_autoled_on' e 'board_autoled_off' têm
// *    quatro casos cada, dos quais três são visualmente idênticos e um é um no-op.
// *
// *    Para quem lê este arquivo pela primeira vez e se pergunta "por que 'case 2' retorna sem fazer nada?":
// *    porque o projeto originalmente previa que, durante uma IRQ ou um handler de sinal, o LED não deveria
// *    ser tocado — preservando o último estado visual estável enquanto o sistema resolvia algo internamente.
// *    É um comportamento defensivo que faz sentido no papel, mas que na prática nunca foi exercitado.
// *
// *    Se um dia esse subsistema for redesenhado, três símbolos bastam: OFF, ON, PANIC. O resto é cerimônia.
// *
// *  NOTA CONCEITUAL — CODIFICAÇÃO DE ESTADOS DO SO:
// *
// *      --------------------  --------------------------  ---------------
// *      SÍMBOLO               SIGNIFICADO                 LED
// *      --------------------  --------------------------  ---------------
// *      'LED_STARTED'         NuttX iniciou               OFF
// *      'LED_HEAPALLOCATE'    Heap alocado                OFF
// *      'LED_IRQSENABLED'     Interrupções habilitadas    OFF
// *      'LED_STACKCREATED'    Pilha de idle criada        ON
// *      'LED_INIRQ'           Dentro de uma interrupção   N/C
// *      'LED_SIGNAL'          Dentro de handler de sinal  N/C
// *      'LED_ASSERTION'       Uma asserção falhou         N/C
// *      'LED_PANIC'           Sistema travou              FLASH
// *      --------------------  --------------------------  ---------------
// *
// *    N/C = "No Change" (Não Alterar). Não significa "Não Configurado" nem "Não Conectado". Significa que o
// *    LED deve permanecer no estado em que está — o driver retorna sem tocar no pino.
// *
// *  NOTA CONCEITUAL — DE ONDE VEM O "FLASH" DO PANIC:
// *
// *    Repare que nem 'board_autoled_on(3)' nem 'board_autoled_off(3)' piscam o LED. Cada um faz uma coisa só:
// *    liga ou desliga. O piscar do estado 'LED_PANIC' emerge do CHAMADOR, não do driver. Quando o NuttX entra
// *    em panic (em 'sched/panic.c'), ele executa algo como:
// *
// *        for (; ; ) {
// *            board_autoled_on(LED_PANIC);
// *            up_mdelay(100);
// *            board_autoled_off(LED_PANIC);
// *            up_mdelay(100);
// *        }
// *
// *    A alternância é responsabilidade de quem chama. O driver é deliberadamente burro — não tem timer, não
// *    tem estado interno, não sabe piscar. Só sabe ligar e desligar o pino. Isso mantém o código mínimo, o
// *    que é essencial em um subsistema que roda antes do scheduler estar pronto.
// *
// *  NOTA SOBRE A PICO W:
// *
// *    O LED verde externo (GPIO 22) substitui o GPIO 25 da Pico clássica como indicador de estado do SO.
// *    Diferente do LED integrado da Pico W (que está no chip CYW43439 e depende do barramento gSPI), este LED
// *    é acessado diretamente via registradores do RP2040 — sem dependência do chip Wi-Fi.
// *
// *  NOTA SOBRE COEXISTÊNCIA:
// *
// *    Os três subsistemas de LED da placa usam pinos físicos distintos, sem sobreposição. Por isso não há
// *    guarda de exclusão mútua entre autoleds, userleds e o driver GPIO user-space.
// *
//*********************************************************************************************************************/

/*
 * Inclusão de Diretivas de Compilação - Início
 */

#include <nuttx/config.h>

/*
 * Inclusão de Bibliotecas em Arquivos de Cabeçalho
 */


/*
 * NuttX:
 */

// Inclui a API de gerenciamento de placas do NuttX ('board_autoled_*')
#include <nuttx/board.h>

// Inclui definições específicas da placa (GPIO_AUTOLED, LED_STARTED, etc.)
#include <arch/board/board.h>


/*
 * BSP:
 */

// Inclui definições internas da arquitetura ARM
#include "arm_internal.h"

// Inclui definições de registradores do chip RP2040
#include "chip.h"

// Inclui a API de manipulação de GPIO do RP2040 ('rp2040_gpio_init', 'rp2040_gpio_put', etc.)
#include "rp2040_gpio.h"

// Inclui protótipos internos da placa
#include "rp2040_pico.h"

/*
 * Menuconfig CONFIG_ARCH_LEDS:
 *
 * Habilita a compilação das funções 'board_autoled_*' apenas quando o subsistema de auto-LEDs está ativo.
 */

#ifdef CONFIG_ARCH_LEDS



/*
 ************************************************* Funções Públicas ****************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: void board_autoled_initialize(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Configura o pino do auto-LED como saída. Chamado pelo NuttX durante o boot, antes de qualquer uso
//*              de 'board_autoled_on' ou 'board_autoled_off'.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
void board_autoled_initialize(void) {


  // Configura o pino como GPIO funcional do RP2040
  rp2040_gpio_init(GPIO_AUTOLED);

  // Define a direção do pino como saída
  rp2040_gpio_setdir(GPIO_AUTOLED, true);

} // fim função void board_autoled_initialize(void)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: void board_autoled_on(int led)
//*
//* PARAMETERS: led: identificador do estado lógico do LED (LED_STARTED, LED_INIRQ, etc.)
//*
//* DESCRIPTION: Acende o auto-LED conforme o estado lógico solicitado. O estado 2 (LED_INIRQ, LED_SIGNAL,
//*              LED_ASSERTION) mantém o LED inalterado. Os demais estados acendem o LED.
//*
//*              Este comportamento é herdado do subsistema equivalente do Linux, onde fazia sentido que certos
//*              eventos internos não alterassem o indicador visual. Na prática, nunca foi exercitado em nenhum
//*              BSP. Ver a HISTÓRIA E CRÍTICA DO SUBSISTEMA no cabeçalho deste arquivo para o contexto completo.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
void board_autoled_on(int led) {


  // Estado padrão: LED aceso
  bool ledon = true;

  // Interpreta o estado lógico
  switch (led) {

      // Estado 0: LED desligado
      // Usado por LED_STARTED, LED_HEAPALLOCATE e LED_IRQSENABLED — as três primeiras etapas do boot, quando
      // o LED permanece apagado até o sistema estar pronto para operar.
      case 0:
        ledon = false;
        break;

      // Estado 2: LED sem alteração (IRQ/sinal/asserção)
      // Esta é a "cerimônia herdada" descrita no cabeçalho. Retorna sem tocar no pino para preservar o último
      // estado visual enquanto o sistema resolve um evento interno. Nenhum BSP upstream usa essa distinção.
      case 2:
        return;

      // Estado 1 e 3: LED aceso
      // Estado 1 = LED_STACKCREATED (boot terminou com sucesso). Estado 3 = LED_PANIC (sistema travou — o
      // piscar emerge do chamador, como descrito no cabeçalho).
      case 1:
      case 3:
        break;

    } // fim switch

  // Aplica o estado ao pino físico
  rp2040_gpio_put(GPIO_AUTOLED, ledon);

} // fim função void board_autoled_on(int led)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: void board_autoled_off(int led)
//*
//* PARAMETERS: led: identificador do estado lógico do LED
//*
//* DESCRIPTION: Apaga o auto-LED conforme o estado lógico solicitado. O estado 2 mantém o LED inalterado. Os demais
//*              estados apagam o LED.
//*
//*              Como descrito no cabeçalho deste arquivo, o piscar de LED_PANIC não é feito aqui — emerge do
//*              chamador, que alterna entre 'board_autoled_on(3)' e 'board_autoled_off(3)' em loop.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
void board_autoled_off(int led) {


  // Interpreta o estado lógico
  switch (led) {

      // Estados 0, 1 e 3: LED desligado
      // Estado 0 = fases iniciais do boot. Estado 1 = ciclo de piscar do panic (o chamador alterna). Estado 3 =
      // mesmo caso. Todos apagam o pino.
      case 0:
      case 1:
      case 3:
        break;

      // Estado 2: LED sem alteração
      // Simétrico ao 'board_autoled_on' — ver a nota lá sobre a herança do subsistema do Linux.
      case 2:
        return;

    } // fim switch

  // Aplica o estado ao pino físico
  rp2040_gpio_put(GPIO_AUTOLED, false);

} // fim função void board_autoled_off(int led)


#endif /* CONFIG_ARCH_LEDS */


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_autoleds.c'
//----------------------------------------------------------------------------------------------------------------------
