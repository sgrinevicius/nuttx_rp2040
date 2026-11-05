//######################################################################################################################
// *
// *  Arquivo: "board.h"
// *
// *  Título: Definições da Placa Raspberry Pi Pico W — Clocking, GPIO, LEDs e Botões
// *
// *  Objetivo: Centralizar as constantes de clocking, contagem de pinos GPIO, definições dos LEDs e dos botões de
// *            usuário utilizadas pelos drivers de baixo nível da Pico W.
// *
// *  Hardware: Raspberry Pi Pico W (RP2040)
// *  RTOS: Apache NuttX 13.0.0
// *
// * SPDX-License-Identifier: Apache-2.0
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
// *  Modelo: Cabeçalho de BSP (Categoria 4 da Taxonomia).
// *
// *  FILOSOFIA E PAPEL ESTRUTURAL NA ARQUITETURA DE PLACA:
// *
// *    Este é o contrato entre a camada Arch (arquitetura RP2040) e a camada Board (placa Pico W). O arquivo declara
// *    as macros e constantes que o código do kernel e dos drivers consultam para operar corretamente sobre o
// *    hardware específico desta placa. Nenhuma macro aqui é "documentação" — todas são consumidas pelo compilador.
// *
// *  NOTA CONCEITUAL — MAPA DE LEDs DA PLACA:
// *
// *    Cinco LEDs externos estão conectados a GPIOs livres do RP2040, divididos em três subsistemas:
// *
// *      LED branco   (GPIO 16) — exposto como '/dev/gpio16' via driver GPIO user-space
// *      LED azul     (GPIO 19) — user-LED 0, controlado pela aplicação
// *      LED amarelo  (GPIO 20) — user-LED 1, controlado pela aplicação
// *      LED vermelho (GPIO 21) — user-LED 2, controlado pela aplicação
// *      LED verde    (GPIO 22) — auto-LED, codifica eventos de boot e panic
// *
// *    O LED verde substitui o GPIO 25 da Pico clássica como indicador de estado do sistema.
// *
// *  NOTA CONCEITUAL — CADEIA DE DEPENDÊNCIAS DOS BOTÕES E LEDs:
// *
// *      'CONFIG_RP2040_BUTTONS=y'                     [Kconfig da placa]
// *		|
// *        ├── 'select ARCH_HAVE_BUTTONS'              [habilita 'ARCH_BUTTONS']
// *		|
// *        └── 'select ARCH_HAVE_IRQBUTTONS'           [habilita 'ARCH_IRQBUTTONS']
// *			  |
// *              └── 'CONFIG_INPUT_BUTTONS_LOWER=y'    [habilita 'btn_lower_initialize()']
// *
// *      'CONFIG_RP2040_LEDS=y'                        [Kconfig da placa]
// *		|
// *        └── 'select ARCH_HAVE_LEDS'                 [habilita 'ARCH_LEDS' e 'USERLED']
// *			  |
// *              ├── 'CONFIG_ARCH_LEDS=y'              [auto-LEDs em 'rp2040_autoleds.c']
// *			  |
// *              └── 'CONFIG_USERLED=y'                [user-LEDs em 'rp2040_userleds.c']
// *
//*********************************************************************************************************************/

/*
 * Guarda de Inclusão - Início -----------------------------------------------------------------------------------------
 */

#ifndef __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_W_INCLUDE_BOARD_H
#define __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_W_INCLUDE_BOARD_H


/*
 * Inclusão de Bibliotecas em Arquivos de Cabeçalho --------------------------------------------------------------------
 */


/*
 * NuttX:
 */

// Inclui macros globais de configuração do Kconfig
#include <nuttx/config.h>


/*
 * BSP (periféricos específicos da placa):
 */

// Inclui definições de configuração de dispositivos I2C
#include "rp2040_i2cdev.h"

// Inclui definições de configuração de dispositivos SPI
#include "rp2040_spidev.h"

// Inclui definições de configuração de dispositivos I2S
#include "rp2040_i2sdev.h"

// Inclui definições de configuração de dispositivos SD-SPI
#include "rp2040_spisd.h"


/*
 * C Libraries:
 */

/*
 * O bloco abaixo só é processado quando o arquivo é incluído em uma unidade de tradução C.  Durante a montagem
 * (assembly), tipos de dados C não existem, então 'stdint.h' é omitido.
 */

#ifndef __ASSEMBLY__
#  include <stdint.h>                      							// Tipos inteiros de largura fixa (uint32_t, etc.)
#endif


/*
 * Definições Gerais ---------------------------------------------------------------------------------------------------
 */

/*
 * Clocking:
 *
 * Parâmetros do subsistema de clock do RP2040.  Os valores refletem a configuração padrão do chip no Pico SDK e
 * determinam as frequências de operação do núcleo, periféricos, USB, ADC e RTC.
 */

// Fator de conversão de MHz para Hz
#define MHZ                     						1000000

// Frequência do cristal externo (12 MHz)
#define BOARD_XOSC_FREQ         						(12 * MHZ)

// Atraso de partida do cristal, em milissegundos
#define BOARD_XOSC_STARTUPDELAY 						1

// Frequência do PLL do sistema (125 MHz)
#define BOARD_PLL_SYS_FREQ      						(125 * MHZ)

// Frequência do PLL do USB (48 MHz)
#define BOARD_PLL_USB_FREQ     							(48 * MHZ)

// Frequência de referência do clock (12 MHz)
#define BOARD_REF_FREQ          						(12 * MHZ)

// Frequência do clock do sistema (125 MHz)
#define BOARD_SYS_FREQ          						(125 * MHZ)

// Frequência do clock dos periféricos (125 MHz)
#define BOARD_PERI_FREQ         						(125 * MHZ)

// Frequência do clock do USB (48 MHz)
#define BOARD_USB_FREQ          						(48 * MHZ)

// Frequência do clock do ADC (48 MHz)
#define BOARD_ADC_FREQ          						(48 * MHZ)

// Frequência do clock do RTC (46.875 kHz)
#define BOARD_RTC_FREQ          						46875

// Frequência base do UART, herdada do clock de periféricos
#define BOARD_UART_BASEFREQ     						BOARD_PERI_FREQ

// Frequência do tick do sistema (1 MHz)
#define BOARD_TICK_CLOCK        						(1 * MHZ)


/*
 * GPIO:
 *
 * Contagem de pinos GPIO expostos pelo driver 'user-space' em 'rp2040_gpio.c'. Cada constante corresponde ao número
 * de pinos de um tipo específico (saída, entrada, interrupção).  Os pinos físicos em si são definidos dentro do
 * próprio driver, não aqui.
 */

/*
 * O LED integrado da Pico W é controlado pelo chip CYW43439 via gSPI, não por um GPIO do RP2040.  Por isso o
 * pino de LED é explicitamente indefinido.
 */

// Nenhum GPIO do RP2040 controla o LED integrado na Pico W
#undef BOARD_GPIO_LED_PIN

// Número de pinos de saída expostos em '/dev/gpioN' (o LED branco)
#define BOARD_NGPIOOUT          						1

// Número de pinos de entrada expostos em '/dev/gpioN'
#define BOARD_NGPIOIN           						1

// Número de pinos com interrupção expostos em '/dev/gpioN'
#define BOARD_NGPIOINT          						1


/*
 * LED definitions:
 *
 * Definições dos LEDs externos conectados a GPIOs livres do RP2040.  São cinco LEDs no total, divididos em três
 * subsistemas conforme o mapa documentado no topo deste arquivo.
 */

/*
 * Valores de índice para uso com 'board_userled()'.  A ordem reflete os três LEDs controláveis pela API de
 * user-LED: azul, amarelo e vermelho.
 */

// LED azul (GPIO 19)
#define BOARD_LED1        								0

// LED amarelo (GPIO 20)
#define BOARD_LED2        								1

// LED vermelho (GPIO 21)
#define BOARD_LED3       							    2

// Número total de LEDs controláveis pela aplicação (exclui branco e verde)
#define BOARD_NLEDS       								3


/*
 * Aliases semânticos por cor.  Permitem que o código se refira ao LED pela cor física, não pelo índice.
 */

// LED azul
#define BOARD_LED_BLUE    								BOARD_LED1

// LED amarelo
#define BOARD_LED_YELLOW  								BOARD_LED2

// LED vermelho
#define BOARD_LED_RED     								BOARD_LED3

/*
 * Máscaras de bit para uso com 'board_userled_all()'.
 */

// Máscara do LED azul
#define BOARD_LED1_BIT    								(1 << BOARD_LED1)

// Máscara do LED amarelo
#define BOARD_LED2_BIT    								(1 << BOARD_LED2)

// Máscara do LED vermelho
#define BOARD_LED3_BIT    								(1 << BOARD_LED3)

/*
 * Pinos físicos do RP2040 associados a cada LED de usuário.
 */

// LED azul
#define GPIO_LED1         								19

// LED amarelo
#define GPIO_LED2         								20

// LED vermelho
#define GPIO_LED3         								21

/*
 * Pino físico reservado ao auto-LED (verde).  Substitui o GPIO 25 da Pico clássica como LED de estado do sistema.
 */

// LED verde (auto-LED)
#define GPIO_AUTOLED      								22

/*
 * Eventos do SO codificados pelo auto-LED.  Cada símbolo representa um estado lógico do sistema que é traduzido
 * em um padrão visual pelo LED verde:
 *
 *   -------------------- ----------------------------- ------
 *   SYMBOL                   Meaning                   LED
 *   -------------------- ----------------------------- ------
 */

// NuttX foi iniciado — LED verde apagado
#define LED_STARTED       								0

// Heap foi alocado — LED verde apagado
#define LED_HEAPALLOCATE  								0

// Interrupções foram habilitadas — LED verde apagado
#define LED_IRQSENABLED  								0

// Pilha de idle foi criada — LED verde aceso
#define LED_STACKCREATED  								1

// Dentro de uma interrupção — LED verde inalterado
#define LED_INIRQ         								2

// Dentro de um handler de sinal — LED verde inalterado
#define LED_SIGNAL        								2

// Uma asserção falhou — LED verde inalterado
#define LED_ASSERTION     								2

// Sistema travou — LED verde piscando
#define LED_PANIC         								3

// Não utilizado nesta placa
#undef  LED_IDLE

/*
 * IMPORTANTE: Os pinos dos LEDs (16, 19, 20, 21, 22) NÃO devem colidir com os pinos de teste do driver de GPIO
 *             'user-space' em 'rp2040_gpio.c' (6, 14), nem com os pinos dos botões (17, 18), nem com os pinos do
 *             CYW43439 (23, 24, 25, 29).
 */


/*
 * BUTTON definitions:
 *
 * Definições dos botões de usuário gerenciados pelo driver genérico do NuttX ('button_lower.c') e expostos ao
 * espaço de usuário como um único dispositivo '/dev/buttons'.
 */

// Número total de botões gerenciados pelo driver
#define NUM_BUTTONS             						2

// ID lógico do botão 1 (índice 0 no bitmask)
#define BUTTON_USER1            						0

// ID lógico do botão 2 (índice 1 no bitmask)
#define BUTTON_USER2            						1

// Máscara de bit do botão 1 (0x01)
#define BUTTON_USER1_BIT        						(1 << BUTTON_USER1)

// Máscara de bit do botão 2 (0x02)
#define BUTTON_USER2_BIT        						(1 << BUTTON_USER2)

/*
 * Mapeamento dos pinos físicos dos botões:
 *
 * NOTA: A Raspberry Pi Pico W não possui botão de usuário conectado aos GPIOs do RP2040 — o botão BOOTSEL
 *       pertence ao chip CYW43439.  Os pinos 17 e 18 são GPIOs livres do RP2040 usados para testes.
 */

// Pino RP2040 conectado ao botão de usuário 1
#define GPIO_BTN_USER1          						17

// Pino RP2040 conectado ao botão de usuário 2
#define GPIO_BTN_USER2          						18

// Menor ID de botão com suporte a interrupção
#define MIN_IRQBUTTON           						BUTTON_USER1

// Maior ID de botão com suporte a interrupção
#define MAX_IRQBUTTON           						BUTTON_USER2


/*
 * Tipos Públicos ------------------------------------------------------------------------------------------------------
 */

#ifndef __ASSEMBLY__


/*
 * Dados Públicos ------------------------------------------------------------------------------------------------------
 */

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif


/*
 * Protótipos de Funções Públicas --------------------------------------------------------------------------------------
 */

// Inicialização precoce da placa. É chamada antes de 'up_initialize()', em um contexto onde ainda não há sistema
// operacional ativo. Deve evitar qualquer operação bloqueante ou uso de periféricos complexos (I2C, SPI, etc).
void rp2040_boardearlyinitialize(void);


// Inicialização principal da placa. É chamada por 'up_initialize()' para configurar pinos, clocks e periféricos
// básicos.  Ainda roda antes do scheduler e sem suporte a alocação dinâmica.
void rp2040_boardinitialize(void);


#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */


/*
 * Guarda de Inclusão - Fim --------------------------------------------------------------------------------------------
 */

#endif /* __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_W_INCLUDE_BOARD_H */


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'board.h'
//----------------------------------------------------------------------------------------------------------------------
