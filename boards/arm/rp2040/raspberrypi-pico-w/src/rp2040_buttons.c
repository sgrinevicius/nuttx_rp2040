//######################################################################################################################
// *
// *  Arquivo: "rp2040_buttons.c"
// *
// *  Título: Driver de Botões da Raspberry Pi Pico W — Adaptação do Driver Genérico do NuttX
// *
// *  Design Pattern: Adaptador de Placa — Traduz a API do driver genérico de botões ('button_lower.c') para as
// *                  operações específicas de GPIO do RP2040.
// *
// *  Objetivo: Fornecer as três funções de placa exigidas pelo driver genérico de botões do NuttX
// *            ('board_button_initialize', 'board_buttons' e 'board_button_irq'), mapeando os botões lógicos
// *            definidos em 'board.h' para os pinos físicos do RP2040.
// *
// *  Hardware: Raspberry Pi Pico W (RP2040)
// *  RTOS: Apache NuttX 13.0.0
// *
// *  Modelo: BSP de Placa.
// *
// *  NOTA CONCEITUAL — FLUXO DE INICIALIZAÇÃO:
// *
// *    Este arquivo é o ponto de integração entre o driver genérico do NuttX e o hardware específico da placa.
// *    O driver genérico não conhece GPIOs — ele apenas chama as três funções abaixo e espera que a placa saiba
// *    como ler e monitorar seus próprios botões.
// *
// *    Fluxo de chamadas (runtime):
// *
// *      'btn_lower_initialize("/dev/buttons")'    [chamado de 'rp2040_bringup.c']
// *		|
// *        └── 'button_lower.c'                    [driver genérico do NuttX]
// *			  |
// *              ├── 'board_button_initialize()'   [inicializa os pinos físicos]
// *			  |
// *              ├── 'board_buttons()'             [lê o estado atual de todos os botões]
// *			  |
// *              └── 'board_button_irq()'          [anexa handler de IRQ a um botão]
// *
// *
// *    NOTA SOBRE DEBOUNCE:
// *
// *      O bouncing mecânico dos botões é filtrado pela camada genérica do NuttX, controlada por
// *      'CONFIG_INPUT_BUTTONS_DEBOUNCE' (em milissegundos).  Este arquivo não implementa nenhum filtro próprio —
// *      apenas repassa as bordas elétricas cruas geradas pelo RP2040.
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

// Inclui tipos inteiros de largura fixa (uint32_t, etc.)
#include <stdint.h>

// Inclui códigos de erro padrão, como EINVAL
#include <errno.h>

// Inclui funções de arquitetura, como up_mdelay()
#include <nuttx/arch.h>

// Inclui a API de gerenciamento de placas (board_button_*)
#include <nuttx/board.h>

// Inclui definições da placa (NUM_BUTTONS, GPIO_BTN_USER1, etc.)
#include <arch/board/board.h>

// Inclui a API de manipulação de GPIO do RP2040
#include "rp2040_gpio.h"

// Inclui protótipos internos da placa
#include "rp2040_pico.h"


/*
 * Menuconfig CONFIG_ARCH_BUTTONS:
 *
 * Habilita a implementação de suporte a botões em nível de placa.  Sem esta opção, nenhuma das funções
 * 'board_button_*' abaixo é compilada, mesmo que o driver genérico esteja ativo.
 */

#if defined(CONFIG_ARCH_BUTTONS)


/*
 * Definições Gerais ---------------------------------------------------------------------------------------------------
 */

/*
 * Validação de configuração:
 *
 * O driver genérico de botões do NuttX depende do subsistema de interrupções para funcionar (ele anexa handlers
 * às bordas dos pinos).  Se 'CONFIG_INPUT_BUTTONS' estiver ativo sem 'CONFIG_ARCH_IRQBUTTONS', o usuário tem uma
 * configuração inválida — falhamos o build com uma mensagem clara em vez de gerar um binário quebrado.
 */

#if defined(CONFIG_INPUT_BUTTONS) && !defined(CONFIG_ARCH_IRQBUTTONS)
#  error "The NuttX Buttons Driver depends on IRQ support to work!\n"
#endif


/*
 * Variáveis Privadas (Estáticas) --------------------------------------------------------------------------------------
 */

/*
 * Mapeamento entre ID lógico de botão e pino físico do RP2040.
 *
 * O índice do array corresponde ao ID lógico do botão (BUTTON_USER1 = 0, BUTTON_USER2 = 1).  O valor de cada
 * posição é o número do pino físico do RP2040, conforme definido em 'board.h' (GPIO_BTN_USER1, GPIO_BTN_USER2).
 *
 * Este array é a única fonte de verdade sobre "qual botão está em qual pino" para este arquivo.
 */

static const uint32_t g_buttons[NUM_BUTTONS] = {

  GPIO_BTN_USER1,                          							// Pino físico do botão de usuário 1
  GPIO_BTN_USER2                           							// Pino físico do botão de usuário 2
};


//______________________________________________________________________________________________________________________



/*
 ************************************************* Funções Públicas ****************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: uint32_t board_button_initialize(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Inicializa os pinos físicos dos botões como entradas.  Deve ser chamada antes de qualquer uso de
//*              'board_buttons()' ou 'board_button_irq()'.  Retorna o número de botões disponíveis.
//*
//* RETURNS: Número de botões inicializados (NUM_BUTTONS).
//*
//**********************************************************************************************************************
uint32_t board_button_initialize(void) {


  // Índice de iteração sobre o array de botões
  int i;

  // Percorre todos os botões registrados
  for (i = 0; i < NUM_BUTTONS; i++) {

      // Configura o pino como GPIO funcional do RP2040
      rp2040_gpio_init(g_buttons[i]);

      /*
       * Configura os pulls internos do pino.
       *
       * ATENÇÃO: 'pull-up = false' e 'pull-down = false' deixam o pino flutuando quando o botão não está
       * pressionado.  Em hardware com botão físico conectado entre o pino e GND (topologia padrão), o correto
       * seria 'rp2040_gpio_set_pulls(g_buttons[i], true, false)' para habilitar o pull-up interno e evitar
       * flutuação.  Sem botão físico conectado, o pino flutuante pode gerar eventos espúrios de IRQ.
       *
       * Como o bouncing é filtrado pelo driver genérico via CONFIG_INPUT_BUTTONS_DEBOUNCE, o comportamento
       * observado com pull-up desabilitado é aceitável para bancada.  Para produção, habilitar pull-up é
       * recomendado.
       */

      // pull-up = false : pull-down = false
      rp2040_gpio_set_pulls(g_buttons[i], false, false);

    } // fim loop for

  // Retorna o número de botões inicializados
  return NUM_BUTTONS;

} // fim função uint32_t board_button_initialize(void)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: uint32_t board_buttons(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Retorna uma bitmask com o estado atual de todos os botões.  Cada bit corresponde a um botão
//*              (bit 0 = BUTTON_USER1, bit 1 = BUTTON_USER2, etc).  Um bit em '1' indica que o botão está
//*              pressionado; um bit em '0' indica que está solto.
//*
//* RETURNS: Bitmask de 32 bits representando o estado atual dos botões.
//*
//**********************************************************************************************************************
uint32_t board_buttons(void) {


  // Bitmask acumuladora (começa vazia = todos soltos)
  uint32_t ret = 0;

  // Índice de iteração sobre o array de botões
  int i;

  // Percorre todos os botões registrados
  for (i = 0; i < NUM_BUTTONS; i++) {

      // Lê o estado do pino: true = solto, false = pressionado
      bool released = rp2040_gpio_get(g_buttons[i]);

      // Se o pino está em LOW, o botão está fisicamente pressionado
      if (!released) {

          // Marca o bit correspondente no bitmask de retorno
          ret |= (1 << i);

        } // fim if

    } // fim loop for

  // Retorna o bitmask com o estado consolidado
  return ret;

} // fim função uint32_t board_buttons(void)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: int board_button_irq(int id, xcpt_t irqhandler, void *arg)
//*
//* PARAMETERS: id: ID lógico do botão (BUTTON_USER1, BUTTON_USER2, etc); irqhandler: função a ser invocada quando
//*             a borda do botão for detectada (NULL para remover); arg: argumento opaco repassado ao handler
//*
//* DESCRIPTION: Anexa (ou remove) um handler de interrupção a um botão específico.  O handler é invocado pelo
//*              subsistema de IRQ do RP2040 sempre que a borda de descida for detectada (transição de solto
//*              para pressionado, com pull-up implícito).
//*
//* RETURNS: OK (0) em caso de sucesso; -EINVAL se o ID estiver fora da faixa válida; código negativo em caso
//*          de falha em rp2040_gpio_irq_attach().
//*
//**********************************************************************************************************************
#ifdef CONFIG_ARCH_IRQBUTTONS
int board_button_irq(int id, xcpt_t irqhandler, void *arg) {


  // Valor padrão: EINVAL (assume ID inválido até prova em contrário)
  int ret = -EINVAL;

  /*
   * Valida o ID contra a faixa de botões que suportam interrupção (definida em 'board.h').
   * A operação deveria ser atômica em relação a outros acessos, mas neste driver single-threaded a checagem é
   * suficiente porque 'board_button_irq' só é chamada pelo próprio driver genérico, que serializa as chamadas.
   */

  // Verifica se o ID está dentro da faixa suportada
  if (id >= MIN_IRQBUTTON && id <= MAX_IRQBUTTON) {

      // Desabilita a IRQ do pino para evitar disparos durante a troca de handler
      rp2040_gpio_disable_irq(g_buttons[id]);

      // Anexa o handler à borda de descida (pressionar o botão = nível baixo no pino)
      ret = rp2040_gpio_irq_attach(g_buttons[id],                           // Número do pino do botão
                                   RP2040_GPIO_INTR_EDGE_LOW,               // ...borda de descida
                                   irqhandler,                              // ...handler do usuário
                                   arg);                                    // ...argumento opaco

      // Verifica se a anexação do handler falhou
      if (ret < 0) {

          // Registra a falha no log do kernel antes de propagar o erro
          syslog(LOG_ERR, "ERROR: irq_attach() failed: %d\n", ret);

          // Propaga o erro ao chamador sem tentar habilitar a IRQ
          return ret;

        } // fim if

      // Habilita a IRQ após o handler estar corretamente anexado
      rp2040_gpio_enable_irq(g_buttons[id]);

    } // fim if

  // Retorna OK se o ID era válido e o attach foi bem-sucedido; -EINVAL caso contrário
  return ret;

} // fim função int board_button_irq(...)

#endif /* CONFIG_ARCH_IRQBUTTONS */

#endif /* CONFIG_ARCH_BUTTONS */


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_buttons.c'
//----------------------------------------------------------------------------------------------------------------------
