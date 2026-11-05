//######################################################################################################################
// *
// *  Arquivo: "rp2040_bringup.c"
// *
// *  Título: Bringup Específico da Raspberry Pi Pico W — Inicialização do CYW43439, GPIO, LEDs e Botões
// *
// *  Design Pattern: Inicialização de Placa — Gerenciamento elétrico, registro de drivers e preparação de
// *                  periféricos.
// *
// *  Objetivo: Realizar a sequência de inicialização dos periféricos específicos da Pico W, incluindo o ciclo de
// *            alimentação do chip Wi-Fi CYW43439 e o registro dos drivers de GPIO, LEDs e botões.
// *
// *  Hardware: Raspberry Pi Pico W (RP2040)
// *  RTOS: Apache NuttX 13.0.0
// *
// *  Modelo: BSP de Placa (Categoria 4 da Taxonomia).
// *
// *  FILOSOFIA E PAPEL ESTRUTURAL NA ARQUITETURA DE PLACA:
// *
// *    Esta função é o ponto de entrada para todo o bring-up da placa após o boot mínimo do kernel. Ela é
// *    chamada uma única vez, durante a fase de late initialization, quando o VFS já está operacional e é seguro
// *    registrar dispositivos de caractere.
// *
// *    Diferente das funções de 'board_early_initialize' e 'board_initialize', que rodam antes do scheduler e
// *    não podem bloquear, esta função pode usar I2C, SPI, delays e chamar funções que dependem de alocação
// *    dinâmica — porque roda em um contexto de thread completa.
// *
// *    A ordem dos blocos abaixo não é arbitrária:
// *
// *      1. Bringup comum     — inicializa periféricos compartilhados (clocks, pinos básicos)
// *      2. Chip Wi-Fi        — alimenta e configura o CYW43439
// *      3. GPIO user-space   — expõe pinos como /dev/gpioN
// *      4. Botões            — registra /dev/buttons
// *      5. Auto-LED          — LED verde como indicador de boot/panic
// *      6. User-LEDs         — LEDs coloridos controlados pela aplicação
// *      7. PM dos LEDs       — registra callbacks de gerenciamento de energia
// *
// *    Cada bloco é protegido por seu próprio 'ifdef' de Menuconfig, refletindo o princípio de que o bringup
// *    deve compilar e rodar mesmo com configurações mínimas.
// *
// *  NOTA CONCEITUAL — FLUXO DE CHAMADAS (RUNTIME):
// *
// *      'rp2040_bringup()'
// *		|
// *        ├── 'rp2040_common_bringup()'           [inicializa periféricos comuns]
// *		|
// *        ├── 'rp2040_pico_w_wlan_power_cycle()'  [ciclo elétrico do CYW43439]
// *		|
// *        ├── 'rp2040_cyw_setup()'                [registra driver Broadcom FullMAC]
// *		|
// *        ├── 'rp2040_dev_gpio_init()'            [registra dispositivos '/dev/gpio*']
// *		|
// *        ├── 'btn_lower_initialize()'            [registra dispositivo '/dev/buttons']
// *		|
// *        ├── 'board_autoled_initialize()'        [inicializa o LED verde (auto-LED)]
// *		|
// *        ├── 'userled_lower_initialize()'        [registra '/dev/userleds' e inicializa os LEDs]
// *		|
// *        └── 'rp2040_led_pminitialize()'         [registra PM dos LEDs]
// *
// *  NOTA CONCEITUAL — SOBRE A CADEIA DO SUBSISTEMA USERLED:
// *
// *    Existe uma confusão comum sobre quem cria o dispositivo '/dev/userleds'. A cadeia é:
// *
// *      'rp2040_bringup()'
// *		|
// *        └── 'userled_lower_initialize("/dev/userleds")'   [drivers/leds/userled_lower.c]
// *			  |
// *              ├── 'board_userled_initialize()'           [rp2040_userleds.c, inicializa GPIOs]
// *			  |
// *              └── 'userled_register()'                   [drivers/leds/userled_upper.c]
// *					  |
// *                      └── cria '/dev/userleds' no VFS
// *
// *    A função 'board_userled_initialize()' NÃO registra o dispositivo — ela apenas configura os pinos
// *    físicos como saídas. Quem registra é 'userled_lower_initialize()', que internamente chama ambas as
// *    camadas. Por isso, o bringup deve chamar 'userled_lower_initialize()', não
// *    'board_userled_initialize()'.
// *
//*********************************************************************************************************************/

/*
 * Inclusão de Diretivas de Compilação - Início ------------------------------------------------------------------------
*/


/*
 * Inclusão de Bibliotecas em Arquivos de Cabeçalho --------------------------------------------------------------------
 */


/*
 * NuttX:
 */

// Inclui macros globais de configuração do Kconfig
#include <nuttx/config.h>

// Inclui funções de delay e utilitários de arquitetura, como up_mdelay()
#include <nuttx/arch.h>

// Inclui funções de debug e logging, como syslog()
#include <nuttx/debug.h>

// Inclui a API de gerenciamento de placas (board_autoled_initialize)
#include <nuttx/board.h>

// Inclui definições de GPIO do chip CYW43439
#include <nuttx/wireless/ieee80211/bcmf_gpio.h>

// Inclui definições específicas da placa Raspberry Pi Pico W
#include <arch/board/board.h>


/*
 * C Libraries:
 */

// Inclui o tipo booleano nativo (true/false)
#include <stdbool.h>

// Inclui definições para NULL, size_t e outros tipos básicos
#include <stddef.h>


/*
 * BSP:
 */

// Inclui protótipos internos da placa, incluindo rp2040_dev_gpio_init()
#include "rp2040_pico.h"

// Inclui funções de manipulação de GPIO do RP2040
#include "rp2040_gpio.h"


/*
 * Menuconfig CONFIG_ARCH_BOARD_COMMON:
 *
 * Habilita o uso do bringup comum entre placas RP2040, compartilhando inicialização de periféricos como SPI, I2C
 * e reset.
 */
#ifdef CONFIG_ARCH_BOARD_COMMON
#include "rp2040_common_bringup.h"
#endif

/*
 * Menuconfig CONFIG_RP2040_INFINEON_CYW43439:
 *
 * Habilita o suporte ao chip WiFi CYW43439 presente na Pico W.
 */
#ifdef CONFIG_RP2040_INFINEON_CYW43439
#include "rp2040_cyw43439.h"
#endif

/*
 * Menuconfig CONFIG_INPUT_BUTTONS:
 *
 * Habilita o driver genérico de botões.  O protótipo de btn_lower_initialize() só está disponível quando
 * CONFIG_INPUT_BUTTONS_LOWER também estiver ativa.  Ver Kconfig da placa para a cadeia de selects.
 */
#ifdef CONFIG_INPUT_BUTTONS
#  include <nuttx/input/buttons.h>
#endif

/*
 * Menuconfig CONFIG_USERLED:
 *
 * Habilita o subsistema de user-LEDs.  O protótipo de userled_lower_initialize() está em '<nuttx/leds/userled.h>'.
 * É esta função — e não 'board_userled_initialize()' — que cria o dispositivo '/dev/userleds'.
 */
#ifdef CONFIG_USERLED
#  include <nuttx/leds/userled.h>
#endif


/*
 * Definições Gerais ---------------------------------------------------------------------------------------------------
 */

// Pino de alimentação do chip WiFi CYW43439
#define CYW43439_POWER_ON_GPIO                  23

// Pino de chip select do chip WiFi CYW43439
#define CYW43439_CHIP_SELECT_GPIO               25

// Pino de dados do chip WiFi CYW43439
#define CYW43439_DATA_GPIO                      24

// Pino de clock do chip WiFi CYW43439
#define CYW43439_CLOCK_GPIO                     29


/*
 * Variáveis Globais ---------------------------------------------------------------------------------------------------
 */

/*
 * Dispositivo GSPI do CYW43439.
 * É preenchido durante a inicialização e usado pelo driver Broadcom FullMAC.
 */

#ifdef CONFIG_RP2040_INFINEON_CYW43439

// Ponteiro global do barramento gSPI do CYW43439
gspi_dev_t *g_cyw43439 = NULL;

#endif


//______________________________________________________________________________________________________________________



/*
 ********************************************** Funções Privadas *******************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: static void rp2040_pico_w_wlan_power_cycle(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Realiza um ciclo de alimentação no chip CYW43439 com temporizações estendidas para garantir
//*              estabilidade elétrica no cold start.  Desliga, aguarda, liga e aguarda novamente.
//*
//* RETURNS: void
//*
//**********************************************************************************************************************
#ifdef CONFIG_RP2040_INFINEON_CYW43439
static void rp2040_pico_w_wlan_power_cycle(void) {


  /*
   * 1. Aguarda estabilização da placa (proteção de cold start)
   */

  // Tempo para reguladores e cristal estabilizarem antes de configurar o pino
  up_mdelay(500);

  /*
   * 2. Evita glitch no regulador do CYW43439
   */

  // Escreve 'false' antes de mudar a direção do pino para evitar pulsos espúrios
  rp2040_gpio_put(CYW43439_POWER_ON_GPIO, false);

  // Configura o pino de alimentação como saída
  rp2040_gpio_setdir(CYW43439_POWER_ON_GPIO, true);

  /*
   * 3. Descarga total dos capacitores do chip wireless
   */

  // Mantém o pino baixo por 100 ms para garantir reset físico completo
  up_mdelay(100);

  /*
   * 4. Ativação e estabilização da PLL de rádio
   */

  // Coloca o pino em nível alto para ligar o chip
  rp2040_gpio_put(CYW43439_POWER_ON_GPIO, true);

  // Aguarda a estabilização da PLL e do cristal de rádio antes das transferências gSPI
  up_mdelay(500);

} // fim função static void rp2040_pico_w_wlan_power_cycle(void)

#endif



//______________________________________________________________________________________________________________________




/*
 ************************************************* Funções Públicas ****************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: int rp2040_bringup(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Executa a inicialização dos drivers e periféricos específicos da placa Pico W.  Realiza o bringup
//*              comum, o ciclo de alimentação do CYW43439, o registro dos drivers de GPIO, botões e LEDs.
//*
//* RETURNS: OK (0) em caso de sucesso; código negativo em caso de falha.
//*
//**********************************************************************************************************************
int rp2040_bringup(void) {


  // Código de retorno, inicializado com sucesso
  int ret = OK;

  /*
   * 1. BRINGUP COMUM DA ARQUITETURA RP2040
   */

#ifdef CONFIG_ARCH_BOARD_COMMON

  // Chama a inicialização comum de periféricos RP2040
  ret = rp2040_common_bringup();

  // Verifica se o bringup comum falhou
  if (ret < 0) {

      // Registra a falha no log
      syslog(LOG_ERR, "Falha no bringup comum do RP2040: %d\n", ret);

      // Retorna o código de erro ao chamador
      return ret;

    } // fim if

#endif

  /*
   * 2. INICIALIZAÇÃO DO CHIP WIRELESS CYW43439
   */

#ifdef CONFIG_RP2040_INFINEON_CYW43439

  // Executa o ciclo elétrico completo para evitar travamentos no cold start
  rp2040_pico_w_wlan_power_cycle();

  // Inicializa o barramento gSPI e acopla o driver Broadcom FullMAC
  g_cyw43439 = rp2040_cyw_setup(CYW43439_POWER_ON_GPIO,        // Pino de controle de energia (Power On)
                                CYW43439_CHIP_SELECT_GPIO,     // Pino de chip select
                                CYW43439_DATA_GPIO,            // Pino de dados
                                CYW43439_CLOCK_GPIO,           // Pino de clock
                                CYW43439_DATA_GPIO);           // Pino de interrupção

  // Verifica se a inicialização do chip falhou
  if (g_cyw43439 == NULL) {

      // Registra erro crítico no log
      syslog(LOG_ERR, "Erro critico: Nao foi possivel inicializar o chip CYW43439\n");

      // Retorna -ENODEV para indicar dispositivo não encontrado
      return -ENODEV;

    } // fim if

#endif

  /*
   * 3. DRIVER DE GPIO USER-SPACE
   */

#ifdef CONFIG_DEV_GPIO

  // Registra os pinos GPIO como dispositivos /dev/gpio*
  ret = rp2040_dev_gpio_init();

  // Verifica se a inicialização do driver de GPIO falhou
  if (ret < 0) {

      // Registra a falha no log
      syslog(LOG_ERR, "Falha ao inicializar o driver de GPIO user-space: %d\n", ret);

      // Retorna o código de erro ao chamador
      return ret;

    } // fim if

#endif

  /*
   * 4. DRIVER DE BOTÕES (BUTTON):
   *
   *    O driver genérico de botões só expõe btn_lower_initialize() quando CONFIG_INPUT_BUTTONS_LOWER está ativa.
   *    Se apenas CONFIG_INPUT_BUTTONS estiver ligada (por exemplo, para uso direto de board_buttons() em outra
   *    parte do sistema), basta inicializar o hardware.
   */

#if defined(CONFIG_INPUT_BUTTONS)
#  if defined(CONFIG_INPUT_BUTTONS_LOWER)

  // Registra o dispositivo /dev/buttons no VFS
  ret = btn_lower_initialize("/dev/buttons");

  // Verifica se o registro do driver de botões falhou
  if (ret < 0) {

      // Registra a falha no log do kernel
      syslog(LOG_ERR, "ERROR: btn_lower_initialize() failed: %d\n", ret);

    } // fim if

#  else

  // Apenas inicializa o hardware de botões, sem expor via VFS
  board_button_initialize();

#  endif
#endif

  /*
   * 5. DRIVER DE AUTO-LED (LED VERDE)
   */

#ifdef CONFIG_ARCH_LEDS

  // Inicializa o LED verde como auto-LED do sistema. Roda cedo para que o LED
  // já esteja pronto antes de qualquer subsistema que dependa dele.
  board_autoled_initialize();

#endif

  /*
   * 6. DRIVER DE USER-LEDs (LEDs AZUL, AMARELO, VERMELHO)
   *
   *    Aqui reside uma confusão comum: 'board_userled_initialize()' (implementada em 'rp2040_userleds.c')
   *    NÃO registra o dispositivo '/dev/userleds'. Ela apenas configura os pinos físicos como saídas.
   *
   *    Quem registra o dispositivo é 'userled_lower_initialize()' (em 'drivers/leds/userled_lower.c'), que
   *    internamente:
   *
   *      - chama 'board_userled_initialize()' para configurar os GPIOs,
   *      - aloca a estrutura do driver lower-half,
   *      - chama 'userled_register()' para criar '/dev/userleds' no VFS.
   *
   *    Por isso o bringup deve chamar 'userled_lower_initialize()', e não 'board_userled_initialize()'
   *    diretamente.
   */

#ifdef CONFIG_USERLED

  // Registra o subsistema de user-LEDs e cria o dispositivo /dev/userleds
  ret = userled_lower_initialize("/dev/userleds");

  // Verifica se o registro do driver de user-LEDs falhou
  if (ret < 0) {

      // Registra a falha no log
      syslog(LOG_ERR, "ERROR: userled_lower_initialize() failed: %d\n", ret);

      // Retorna o código de erro ao chamador
      return ret;

    } // fim if

#endif

  /*
   * 7. GERENCIAMENTO DE ENERGIA DOS LEDs (OPCIONAL)
   */

#ifdef CONFIG_PM

  // Registra o driver de LEDs junto ao framework de PM.
  rp2040_led_pminitialize();

#endif

  // Retorna sucesso ao chamador
  return OK;

} // fim função int rp2040_bringup(void)


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_bringup.c'
//----------------------------------------------------------------------------------------------------------------------
