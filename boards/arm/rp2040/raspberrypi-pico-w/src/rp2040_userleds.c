//######################################################################################################################
// *
// *  Arquivo: "boards/arm/rp2040/raspberrypi-pico-w/src/rp2040_userleds.c"
// *
// *  Título: User-LEDs da Raspberry Pi Pico W — LEDs Azul, Amarelo e Vermelho (GPIOs 19, 20, 21)
// *
// *  Design Pattern: Adaptador de Placa — Traduz a API genérica de user-LEDs do NuttX para operações de GPIO do
// *                  RP2040.
// *
// *  Objetivo: Fornecer as três funções de placa exigidas pelo subsistema de user-LEDs, além do gancho opcional
// *            de gerenciamento de energia.
// *
// *  Hardware: Raspberry Pi Pico W (RP2040) + 5 LEDs externos
// *  RTOS: Apache NuttX 13.0.0
// *
// *  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.  
// *  See the NOTICE file distributed with this work for additional information regarding copyright ownership.  
// *  The ASF licenses this file to you under the Apache License, Version 2.0 (the "License"); you may not use this file 
// *  except in compliance with the License.  You may obtain a copy of the License at:
// *
// *   http://www.apache.org/licenses/LICENSE-2.0
// *
// *  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed 
// *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
// *  See the License for the specific language governing permissions and limitations under the License.
// *
// *--------------------------------------------------------------------------------------------------------------------
// *
// *  Modelo: BSP de Placa (Categoria 4 da Taxonomia).
// *
// *  FILOSOFIA E PAPEL ESTRUTURAL NA ARQUITETURA DE PLACA:
// *
// *    O subsistema de 'userleds' dá às aplicações controle direto sobre LEDs de bordo. Diferente do 'autoled'
// *    (que pertence ao SO e reporta eventos de 'boot'/'panic'), o 'userled' é um recurso do usuário: a aplicação
// *    decide o que ele significa.
// *
// *    A API é propositalmente minimalista: três funções que inicializam, escrevem um LED individual e escrevem
// *    um conjunto via bitmask. Toda a semântica de "o que cada LED representa" mora na aplicação, não no driver.
// *
// *    A exposição ao espaço de usuário ocorre por '/dev/userleds', criado automaticamente pelo NuttX. Aplicações
// *    podem então usar 'ioctl()' sem linkar diretamente contra o BSP.
// *
// *  NOTA CONCEITUAL — COEXISTÊNCIA:
// *
// *    Este arquivo implementa apenas os três LEDs de cor controláveis pela aplicação. Os outros dois LEDs vivem
// *    em subsistemas diferentes:
// *
// *      LED branco (GPIO 16) — exposto como '/dev/gpio16' pelo driver 'rp2040_gpio.c'
// *      LED verde  (GPIO 22) — reservado ao subsistema de auto-LEDs ('rp2040_autoleds.c')
// *
// *    Como os pinos são distintos, não há guarda '#ifndef CONFIG_ARCH_LEDS' neste arquivo — diferente do padrão
// *    da Pico clássica.
// *
// *  NOTA CONCEITUAL — GERENCIAMENTO DE ENERGIA (PM):
// *
// *    Este arquivo NÃO controla nenhum LED durante transições de energia. A decisão é intencional e merece
// *    explicação, porque contraria o padrão herdado do BSP da Pico clássica.
// *
// *    No BSP original (Raspberry Pi Pico sem Wi-Fi), havia um único LED de bordo conectado ao GPIO 25. Nesse
// *    cenário, o 'led_pm_notify' fazia sentido: o PM desligava o único LED disponível em 'PM_IDLE' e o
// *    reacendia em 'PM_NORMAL'. Não havia ambiguidade — só existia um LED.
// *
// *    Na Pico W customizada, existem três LEDs de usuário (azul, amarelo, vermelho), cada um com semântica
// *    própria definida pela aplicação. Não há razão objetiva para escolher um deles como "primário" para o PM
// *    controlar. Escolher arbitrariamente (por exemplo, o azul) seria uma decisão de design disfarçada de
// *    padrão — pior que não decidir.
// *
// *    Além disso, o subsistema de user-LEDs existe precisamente para dar controle à aplicação. Se o PM
// *    apagasse LEDs em 'PM_IDLE' sem avisar o app, o app perderia o estado que ele mesmo definiu. Quando a
// *    aplicação precisar reagir a transições de energia, ela pode registrar seu próprio callback de PM e
// *    chamar 'board_userled()' ou 'board_userled_all()' diretamente — é para isso que a API existe.
// *
// *    Portanto:
// *
// *      - 'led_pm_prepare' permanece — aceita sempre a transição de estado (sem efeito prático).
// *      - 'led_pm_notify' foi REMOVIDO — o campo da estrutura é NULL.
// *
// *    Se no futuro algum LED específico precisar ser gerenciado pelo PM (por exemplo, desligar todos em
// *    'PM_IDLE' para economizar energia), o ponto de intervenção é o campo '.notify' de 'g_ledscb' abaixo.
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

// Inclui tipos inteiros de largura fixa
#include <stdint.h>

// Inclui o tipo booleano nativo (true/false)
#include <stdbool.h>

// Inclui macros de logging e asserção
#include <nuttx/debug.h>

// Inclui a API de gerenciamento de placas do NuttX ('board_userled_*')
#include <nuttx/board.h>

// Inclui a API de gerenciamento de energia
#include <nuttx/power/pm.h>

// Inclui definições específicas da placa
#include <arch/board/board.h>


/*
 * BSP:
 */

// Inclui definições internas da arquitetura ARM
#include "arm_internal.h"

// Inclui definições de registradores do chip RP2040
#include "chip.h"

// Inclui a API de manipulação de GPIO do RP2040
#include "rp2040_gpio.h"

// Inclui protótipos internos da placa
#include "rp2040_pico.h"


/*
 * Variáveis Privadas (Estáticas) --------------------------------------------------------------------------------------
 */

/*
 * Mapeamento entre índice lógico de LED e pino físico do RP2040.
 *
 * O índice do array corresponde ao ID lógico do LED (BOARD_LED1 = 0, etc.). O valor de cada posição é o número do
 * pino físico do RP2040, conforme definido em 'board.h'.
 */

// Declara array de leds
static uint32_t g_ledcfg[BOARD_NLEDS] = {

  GPIO_LED1,                               				// Pino 19: LED azul
  GPIO_LED2,                               				// Pino 20: LED amarelo
  GPIO_LED3                                				// Pino 21: LED vermelho
};

/*
 * Protótipos de Funções Privadas (Estáticas) --------------------------------------------------------------------------
 */

#ifdef CONFIG_PM
static int  led_pm_prepare(struct pm_callback_s *cb, int domain, enum pm_state_e pmstate);
#endif


/*
 * Estruturas Privadas (Estáticas) -------------------------------------------------------------------------------------
 */

/*
 * Callback de gerenciamento de energia:
 *
 * Registra as funções de preparação junto ao framework de PM. A estrutura é passada para 'pm_register()' em
 * 'rp2040_led_pminitialize()'.
 *
 * O campo '.notify' é deliberadamente NULL: nenhum LED é controlado pelo PM. Ver a NOTA CONCEITUAL —
 * GERENCIAMENTO DE ENERGIA (PM) no cabeçalho deste arquivo para a justificativa.
 */

#ifdef CONFIG_PM
// Declara estrutura de funções
static struct pm_callback_s g_ledscb = {

  .notify  = NULL,                        							// Nenhum LED é controlado pelo PM
  .prepare = led_pm_prepare,              							// Função chamada antes da transição de estado
};
#endif


/*
 ********************************************** Funções Privadas *******************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: static int led_pm_prepare(struct pm_callback_s *cb, int domain, enum pm_state_e pmstate)
//*
//* DESCRIPTION: Consulta o driver antes da transição. Nenhuma preparação é necessária para os LEDs.
//*
//* RETURNS: OK (0) — aceita sempre a transição.
//*
//**********************************************************************************************************************
#ifdef CONFIG_PM
static int led_pm_prepare(struct pm_callback_s *cb, int domain, enum pm_state_e pmstate) {


  // Aceita a transição de estado sem preparação
  return OK;

} // fim função static int led_pm_prepare(...)
#endif



//______________________________________________________________________________________________________________________




/*
 ************************************************* Funções Públicas ****************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: uint32_t board_userled_initialize(void)
//*
//* DESCRIPTION: Configura os 3 pinos físicos dos LEDs de usuário como saídas. Retorna o número de LEDs.
//*
//* RETURNS: Número de LEDs inicializados (BOARD_NLEDS).
//*
//**********************************************************************************************************************
uint32_t board_userled_initialize(void) {


  // Índice de iteração
  int i;

  // Percorre todos os LEDs configurados
  for (i = 0; i < BOARD_NLEDS; i++) {

      // Configura o pino como GPIO funcional
      rp2040_gpio_init(g_ledcfg[i]);

      // Define a direção do pino como saída
      rp2040_gpio_setdir(g_ledcfg[i], true);

    } // fim loop for

  // Retorna o número de LEDs inicializados
  return BOARD_NLEDS;

} // fim função uint32_t board_userled_initialize(void)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: void board_userled(int led, bool ledon)
//*
//* DESCRIPTION: Controla o estado de um LED específico. Índices fora da faixa são ignorados.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
void board_userled(int led, bool ledon) {


  // Valida o índice lógico contra o número total de LEDs
  if ((unsigned)led < BOARD_NLEDS) {

      // Aplica o estado ao pino físico correspondente
      rp2040_gpio_put(g_ledcfg[led], ledon);

    } // fim if

} // fim função void board_userled(int led, bool ledon)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: void board_userled_all(uint32_t ledset)
//*
//* DESCRIPTION: Controla todos os LEDs de uma vez via bitmask.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
void board_userled_all(uint32_t ledset) {


  // Índice de iteração
  int i;

  // Percorre todos os LEDs configurados
  for (i = 0; i < BOARD_NLEDS; i++) {

      // Extrai o bit correspondente e aplica ao pino físico
      rp2040_gpio_put(g_ledcfg[i], (ledset & (1 << i)) != 0);

    } // fim loop for

} // fim função void board_userled_all(uint32_t ledset)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: void rp2040_led_pminitialize(void)
//*
//* DESCRIPTION: Registra o driver junto ao framework de gerenciamento de energia.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
#ifdef CONFIG_PM
void rp2040_led_pminitialize(void) {


  // Registra o callback de gerenciamento de energia
  int ret = pm_register(&g_ledscb);

  // Verifica se o registro falhou
  if (ret != OK) {

      // Sinaliza via auto-LED, se disponível
      board_autoled_on(LED_ASSERTION);

    } // fim if

} // fim função void rp2040_led_pminitialize(void)
#endif /* CONFIG_PM */


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_userleds.c'
//----------------------------------------------------------------------------------------------------------------------
