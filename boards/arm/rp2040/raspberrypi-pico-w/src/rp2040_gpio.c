//######################################################################################################################
// *
// *  Arquivo: "boards/arm/rp2040/raspberrypi-pico-w/src/rp2040_gpio.c"
// *
// *  Título: Driver GPIO User-Space da Raspberry Pi Pico W — Registro de '/dev/gpio*'
// *
// *  Design Pattern: Driver de Caractere — Encapsulamento de pinos RP2040 como dispositivos VFS acessíveis ao
// *                  espaço de usuário via '/dev/gpioN'.
// *
// *  Objetivo: Registrar pinos GPIO do RP2040 como dispositivos de caractere no VFS do NuttX, permitindo que
// *            aplicações 'user-space' leiam, escrevam e anexem handlers de interrupção a pinos individuais.
// *
// *  Hardware: Raspberry Pi Pico W (RP2040)
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
// *    O NuttX trata GPIO de duas maneiras distintas: internamente (drivers que escrevem direto em registradores)
// *    e externamente (aplicações que precisam controlar pinos sem linkar contra o BSP). Este arquivo pertence
// *    à segunda categoria — é o driver que expõe pinos do RP2040 como '/dev/gpioN'.
// *
// *    A exposição via VFS tem custo: cada operação passa por uma camada de 'gpio_operations_s' e por chamadas de
// *    sistema. Em compensação, a aplicação ganha independência total do BSP — pode ser portada para outra placa
// *    mudando apenas o nome do dispositivo.
// *
// *    O subsistema também permite que o pino seja configurado como entrada, saída ou entrada com interrupção,
// *    cada um com sua própria tabela de operações. A escolha é feita em tempo de registro, não em tempo de uso.
// *
// *  NOTA CONCEITUAL — MAPA DE PINOS DA PLACA:
// *
// *    Este driver expõe um pino de saída, um de entrada e um com interrupção, escolhidos entre os GPIOs livres
// *    do RP2040. Os outros recursos de hardware da placa (LEDs, botões, Wi-Fi) vivem em subsistemas separados:
// *
// *      LED branco   (GPIO 16) — este driver, exposto como '/dev/gpio16'
// *      LED azul     (GPIO 19) — 'rp2040_userleds.c', API de user-LED
// *      LED amarelo  (GPIO 20) — 'rp2040_userleds.c', API de user-LED
// *      LED vermelho (GPIO 21) — 'rp2040_userleds.c', API de user-LED
// *      LED verde    (GPIO 22) — 'rp2040_autoleds.c', auto-LED do SO
// *      Botão 1      (GPIO 17) — 'rp2040_buttons.c', via '/dev/buttons'
// *      Botão 2      (GPIO 18) — 'rp2040_buttons.c', via '/dev/buttons'
// *      Wi-Fi        (GPIOs 23, 24, 25, 29) — driver CYW43439
// *
// *    Esta separação evita que dois drivers escrevam no mesmo pino físico em runtime.
// *
// *  NOTA CONCEITUAL — SOBRE O LED BRANCO EM '/dev/gpio16':
// *
// *    O pino 16, originalmente uma "saída de teste" no BSP da Pico, tornou-se o LED branco da aplicação. Ele é
// *    controlado diretamente via 'ioctl(/dev/gpio16, GPIOC_WRITE, ...)' — por exemplo, pelo módulo 'mqtt_utils.c'
// *    como indicador visual de publicação MQTT bem-sucedida.
// *
// *    Não é uma saída genérica: tem papel definido no projeto, e o comentário abaixo reflete isso.
// *
// ********************************************************************************************************************/

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

// Inclui tipos básicos do sistema, como 'ssize_t' e 'off_t'
#include <sys/types.h>

// Inclui a função 'syslog()' para registro de mensagens no console
#include <syslog.h>

// Inclui o gerenciamento de interrupções do NuttX
#include <nuttx/irq.h>

// Inclui definições específicas de IRQ da arquitetura ARM
#include <arch/irq.h>

// Inclui a macro 'DEBUGASSERT()' para validações em desenvolvimento
#include <assert.h>

// Inclui macros de logging, como 'gpioinfo()' e 'snerr()'
#include <nuttx/debug.h>

// Inclui a interface do subsistema GPIO do NuttX
#include <nuttx/ioexpander/gpio.h>

// Inclui definições específicas da placa (BOARD_NGPIOOUT, etc.)
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

/*
 * Menuconfig CONFIG_DEV_GPIO:
 *
 * Habilita o driver de GPIO user-space.  O segundo predicado exclui o modo "lower half", que delega o registro
 * a um driver de ioexpander externo — nesse caso, este arquivo não deve compilar nenhuma lógica própria.
 */

#if defined(CONFIG_DEV_GPIO) && !defined(CONFIG_GPIO_LOWER_HALF)

/*
 * Definições Gerais ---------------------------------------------------------------------------------------------------
 */

/* Pino de saída:
 *
 * O LED branco externo (GPIO 16) é exposto como '/dev/gpio16' para controle direto pela aplicação.É usado, por exemplo, 
 * como indicador visual de publicação MQTT bem-sucedida no módulo 'mqtt_utils.c'.
 *
 * NÃO é uma saída de teste genérica: o pino tem função definida no projeto e é o único recurso de saída exposto pelo 
 * driver GPIO user-space da placa.
 *
 * Os outros quatro LEDs da placa vivem em subsistemas separados:
 *
 *   - LEDs azul, amarelo e vermelho em 'rp2040_userleds.c' (GPIOs 19, 20, 21)
 *   - LED verde em 'rp2040_autoleds.c' (GPIO 22)
 */

#define GPIO_OUT1             16           // Pino 16: LED branco (indicador da aplicação)

/* Pino de entrada:
 *
 * O pino 6 é um GPIO livre do RP2040 na Pico W.  O pino 2 é deliberadamente evitado porque é reivindicado pelo
 * driver de botões (GPIO_BTN_USER1) quando 'CONFIG_INPUT_BUTTONS'=y estiver ativo.
 */

#define GPIO_IN1              6            // Pino 6: entrada de teste sem IRQ

/* Pino de entrada por interrupção:
 *
 * Usado para validar o caminho de IRQ do driver de GPIO user-space, anexando um handler via 'ioctl()'.
 */

#define GPIO_IRQPIN1          14           // Pino 14: entrada de teste com IRQ


/*
 * Estruturas Privadas -------------------------------------------------------------------------------------------------
 */

/* Estrutura base de um GPIO registrado pelo driver.
 *
 * Encapsula o descritor genérico do NuttX ('gpio_dev_s') e um identificador local.
 */

struct rp2040gpio_dev_s {

  struct gpio_dev_s gpio;                  							// Descritor genérico do subsistema GPIO do NuttX
  uint8_t id;                              							// Índice deste pino dentro do array de registros
};

/* Estrutura estendida para pinos de interrupção.
 *
 * Acrescenta o 'callback user-space' que será invocado quando a IRQ disparar.
 */

struct rp2040gpint_dev_s {

  struct rp2040gpio_dev_s rp2040gpio;      							// Estrutura base do GPIO
  pin_interrupt_t callback;                							// Callback registrado pela aplicação user-space
};


/*
 * ********************************** Protótipos de Funções Privadas (Estáticas) ***************************************
 */

#if BOARD_NGPIOOUT > 0
static int gpout_read(struct gpio_dev_s *dev, bool *value);     			// Lê estado de um pino de saída
static int gpout_write(struct gpio_dev_s *dev, bool value);     			// Escreve estado em um pino de saída
#endif

#if BOARD_NGPIOIN > 0
static int gpin_read(struct gpio_dev_s *dev, bool *value);      			// Lê estado de um pino de entrada
#endif

#if BOARD_NGPIOINT > 0
static int gpint_read(struct gpio_dev_s *dev, bool *value);     			// Lê estado de um pino de entrada com IRQ
static int gpint_attach(struct gpio_dev_s *dev, pin_interrupt_t callback);  // Anexa 'callback' a um pino de interrupção
static int gpint_enable(struct gpio_dev_s *dev, bool enable);   			// Habilita/desabilita a IRQ de um pino
#endif


/*
 * Variáveis Privadas (Estáticas) --------------------------------------------------------------------------------------
 */

/*
 * Bloco de pinos de SAÍDA:
 * 
 * Tabela de operações expostas ao VFS (leitura e escrita) e array estático que mapeia os pinos físicos.
 */

#if BOARD_NGPIOOUT > 0
static const struct gpio_operations_s gpout_ops = {

  .go_read   = gpout_read,                						    // Operação de leitura exposta em '/dev/gpioN'
  .go_write  = gpout_write,              						    // Operação de escrita exposta em '/dev/gpioN'
  .go_attach = NULL,                      						    // Saídas não suportam callback de interrupção
  .go_enable = NULL,                       							// Saídas não suportam habilitação de IRQ
};


static const uint32_t g_gpiooutputs[BOARD_NGPIOOUT] = {

  GPIO_OUT1                                						    // Pino 16
};


static struct rp2040gpio_dev_s g_gpout[BOARD_NGPIOOUT];   		   // Instâncias registradas no VFS
#endif


/*
 * Bloco de pinos de ENTRADA:
 *
 * Tabela de operações expostas ao VFS (somente leitura) e array estático que mapeia os pinos físicos.
 */

#if BOARD_NGPIOIN > 0
static const struct gpio_operations_s gpin_ops = {

  .go_read   = gpin_read,                  							// Operação de leitura exposta em '/dev/gpioN'	
  .go_write  = NULL,                       							// Entradas não suportam escrita
  .go_attach = NULL,                       							// Entradas sem IRQ não suportam callback
  .go_enable = NULL,                       							// Entradas sem IRQ não suportam habilitação
};


static const uint32_t g_gpioinputs[BOARD_NGPIOIN] = {

  GPIO_IN1                                						 	// Pino 6
};


static struct rp2040gpio_dev_s g_gpin[BOARD_NGPIOIN];     			// Instâncias registradas no VFS
#endif

/*
 * Bloco de pinos de entrada com INTERRUPÇÃO:
 *
 * Tabela de operações expostas ao VFS (leitura, attach e enable) e array estático que mapeia os pinos físicos.
 */

#if BOARD_NGPIOINT > 0
static const struct gpio_operations_s gpint_ops = {

  .go_read   = gpint_read,                					 	    // Operação de leitura exposta em /dev/gpioN
  .go_write  = NULL,                      					 	    // Entradas com IRQ não suportam escrita
  .go_attach = gpint_attach,               					 	    // Anexa um callback user-space
  .go_enable = gpint_enable,               					 	    // Habilita ou desabilita a IRQ associada
};

static const uint32_t g_gpiointinputs[BOARD_NGPIOINT] = {

  GPIO_IRQPIN1,                            					 	    // Pino 14
};

static struct rp2040gpint_dev_s g_gpint[BOARD_NGPIOINT];     	    // Instâncias registradas no VFS
#endif


//______________________________________________________________________________________________________________________



/*
 ********************************************** Funções Privadas *******************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: static int gpout_read(struct gpio_dev_s *dev, bool *value)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; value: ponteiro para o resultado booleano
//*
//* DESCRIPTION: Lê o estado atual do pino de saída associado ao descritor. O valor é retornado via ponteiro.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
#if BOARD_NGPIOOUT > 0
static int gpout_read(struct gpio_dev_s *dev, bool *value) {


  // Recupera a estrutura específica do RP2040 a partir do descritor genérico
  struct rp2040gpio_dev_s *rp2040gpio = (struct rp2040gpio_dev_s *)dev;

  // Valida os ponteiros antes de acessar os dados
  DEBUGASSERT(rp2040gpio != NULL && value != NULL);

  // Garante que o índice está dentro dos limites do array de pinos
  DEBUGASSERT(rp2040gpio->id < BOARD_NGPIOOUT);

  // Registra no log de debug a operação de leitura
  gpioinfo("Reading...\n");

  // Consulta o estado elétrico do pino físico e armazena no buffer do chamador
  *value = rp2040_gpio_get(g_gpiooutputs[rp2040gpio->id]);

  // Retorna sucesso ao VFS
  return OK;

} // fim função static int gpout_read(...)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int gpout_write(struct gpio_dev_s *dev, bool value)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; value: novo estado booleano do pino
//*
//* DESCRIPTION: Aplica um novo estado elétrico ao pino de saída associado ao descritor.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
static int gpout_write(struct gpio_dev_s *dev, bool value) {


  // Recupera a estrutura específica do RP2040 a partir do descritor genérico
  struct rp2040gpio_dev_s *rp2040gpio = (struct rp2040gpio_dev_s *)dev;

  // Valida o ponteiro do descritor antes de acessar os dados
  DEBUGASSERT(rp2040gpio != NULL);

  // Garante que o índice está dentro dos limites do array de pinos
  DEBUGASSERT(rp2040gpio->id < BOARD_NGPIOOUT);

  // Registra no log de debug o valor a ser escrito
  gpioinfo("Writing %d\n", (int)value);

  // Aplica o novo estado elétrico ao pino físico
  rp2040_gpio_put(g_gpiooutputs[rp2040gpio->id], value);

  // Retorna sucesso ao VFS
  return OK;

} // fim função static int gpout_write(...)

#endif


//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int gpin_read(struct gpio_dev_s *dev, bool *value)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; value: ponteiro para o resultado booleano
//*
//* DESCRIPTION: Lê o estado atual do pino de entrada associado ao descritor.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
#if BOARD_NGPIOIN > 0
static int gpin_read(struct gpio_dev_s *dev, bool *value) {


  // Recupera a estrutura específica do RP2040 a partir do descritor genérico
  struct rp2040gpio_dev_s *rp2040gpio = (struct rp2040gpio_dev_s *)dev;

  // Valida os ponteiros antes de acessar os dados
  DEBUGASSERT(rp2040gpio != NULL && value != NULL);

  // Garante que o índice está dentro dos limites do array de pinos
  DEBUGASSERT(rp2040gpio->id < BOARD_NGPIOIN);

  // Registra no log de debug qual pino está sendo lido
  gpioinfo("Reading... pin %d\n", (int)g_gpioinputs[rp2040gpio->id]);

  // Consulta o estado elétrico do pino físico e armazena no buffer do chamador
  *value = rp2040_gpio_get(g_gpioinputs[rp2040gpio->id]);

  // Retorna sucesso ao VFS
  return OK;


} // fim função static int gpin_read(...) 

#endif


//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int rp2040gpio_interrupt(int irq, void *context, void *arg)
//*
//* PARAMETERS: irq: número da IRQ que disparou; context: contexto do registrador salvo; arg: descritor
//*
//* DESCRIPTION: Handler de interrupção de baixo nível.  Invoca o callback user-space previamente registrado
//*              via gpint_attach(), passando o descritor genérico e o ID do pino.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
#if BOARD_NGPIOINT > 0
static int rp2040gpio_interrupt(int irq, void *context, void *arg) {


  // Recupera a estrutura estendida de interrupção a partir do argumento
  struct rp2040gpint_dev_s *rp2040gpint = (struct rp2040gpint_dev_s *)arg;

  // Valida os ponteiros antes de invocar o callback
  DEBUGASSERT(rp2040gpint != NULL && rp2040gpint->callback != NULL);

  // Registra no log de debug o endereço do callback que será invocado
  gpioinfo("Interrupt! callback=%p\n", rp2040gpint->callback);

  // Invoca o callback user-space com o descritor genérico e o índice do pino
  rp2040gpint->callback(&rp2040gpint->rp2040gpio.gpio,					// Descritor genérico do GPIO
                        rp2040gpint->rp2040gpio.id);					// Índice do pino no array de IRQ

  // Retorna sucesso ao framework de interrupções
  return OK;

} // fim função static int rp2040gpio_interrupt(...) 



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int gpint_read(struct gpio_dev_s *dev, bool *value)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; value: ponteiro para o resultado booleano
//*
//* DESCRIPTION: Lê o estado atual do pino configurado como entrada com interrupção.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
static int gpint_read(struct gpio_dev_s *dev, bool *value) {


  // Recupera a estrutura estendida de interrupção a partir do descritor genérico
  struct rp2040gpint_dev_s *rp2040gpint = (struct rp2040gpint_dev_s *)dev;

  // Valida os ponteiros antes de acessar os dados
  DEBUGASSERT(rp2040gpint != NULL && value != NULL);

  // Garante que o índice está dentro dos limites do array de pinos de interrupção
  DEBUGASSERT(rp2040gpint->rp2040gpio.id < BOARD_NGPIOINT);

  // Registra no log de debug a operação de leitura
  gpioinfo("Reading int pin...\n");

  // Consulta o estado elétrico do pino físico e armazena no buffer do chamador
  *value = rp2040_gpio_get(g_gpiointinputs[rp2040gpint->rp2040gpio.id]);

  // Retorna sucesso ao VFS
  return OK;

} // fim função static int gpint_read(...)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int gpint_attach(struct gpio_dev_s *dev, pin_interrupt_t callback)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; callback: função a ser invocada quando a IRQ disparar
//*
//* DESCRIPTION: Anexa um callback user-space ao pino de interrupção.  Desabilita a IRQ, anexa o handler interno
//*              'rp2040gpio_interrupt' como trampolim, armazena o callback do usuário e aguarda gpint_enable().
//*
//* RETURNS: OK (0) em caso de sucesso; código negativo em caso de falha.
//*
//**********************************************************************************************************************
static int gpint_attach(struct gpio_dev_s *dev, pin_interrupt_t callback) {


  // Recupera a estrutura estendida de interrupção a partir do descritor genérico
  struct rp2040gpint_dev_s *rp2040gpint = (struct rp2040gpint_dev_s *)dev;

  // Obtém o número do pino físico associado a este descritor
  int irq = g_gpiointinputs[rp2040gpint->rp2040gpio.id];

  // Variável para armazenar o código de retorno de rp2040_gpio_irq_attach()
  int ret;

  // Registra no log de debug o início da operação de attach
  gpioinfo("Attaching the callback\n");

  // Desabilita a IRQ do pino para evitar disparos durante a reconfiguração
  rp2040_gpio_disable_irq(irq);

  // Anexa o handler interno como trampolim para o callback do usuário
  ret = rp2040_gpio_irq_attach(irq,                                          // Número da IRQ do pino
                               RP2040_GPIO_INTR_EDGE_LOW,                    // ...borda de descida
                               rp2040gpio_interrupt,                         // ...handler trampolim
                               &g_gpint[rp2040gpint->rp2040gpio.id]);        // ...descritor como argumento

  // Verifica se a anexação do handler falhou
  if (ret < 0) {

      // Registra a falha no log do kernel
      syslog(LOG_ERR, "ERROR: gpint_attach() failed: %d\n", ret);

      // Propaga o erro ao chamador
      return ret;

    } // fim if

  // Registra no log de debug o endereço do callback anexado
  gpioinfo("Attach %p\n", callback);

  // Armazena o callback do usuário para invocação posterior a partir do handler
  rp2040gpint->callback = callback;

  // Retorna sucesso ao VFS
  return OK;

} // fim função static int gpint_attach(...)



//______________________________________________________________________________________________________________________




//**********************************************************************************************************************
//* FUNCTION: static int gpint_enable(struct gpio_dev_s *dev, bool enable)
//*
//* PARAMETERS: dev: descritor genérico do GPIO; enable: true para habilitar, false para desabilitar a IRQ
//*
//* DESCRIPTION: Habilita ou desabilita a interrupção do pino associado.  Se nenhum callback tiver sido anexado,
//*              a habilitação é silenciosamente ignorada.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
static int gpint_enable(struct gpio_dev_s *dev, bool enable) {


  // Recupera a estrutura estendida de interrupção a partir do descritor genérico
  struct rp2040gpint_dev_s *rp2040gpint = (struct rp2040gpint_dev_s *)dev;

  // Obtém o número do pino físico associado a este descritor
  int irq = g_gpiointinputs[rp2040gpint->rp2040gpio.id];

  // Verifica se a operação solicitada é habilitação
  if (enable) {

      // Só habilita a IRQ se um callback válido já estiver registrado
      if (rp2040gpint->callback != NULL) {

          // Registra no log de debug a habilitação da interrupção
          gpioinfo("Enabling the interrupt\n");

          // Habilita a IRQ do pino no controlador de interrupções do RP2040
          rp2040_gpio_enable_irq(irq);

        } // fim if interno

    } else {

      // Registra no log de debug a desabilitação da interrupção
      gpioinfo("Disable the interrupt\n");

      // Desabilita a IRQ do pino no controlador de interrupções do RP2040
      rp2040_gpio_disable_irq(irq);

    } // fim else

  // Retorna sucesso ao VFS
  return OK;

} // fim função static int gpint_enable(...)

#endif



//______________________________________________________________________________________________________________________



/*
 ************************************************* Funções Públicas ****************************************************
 */



//**********************************************************************************************************************
//* FUNCTION: int rp2040_dev_gpio_init(void)
//*
//* PARAMETERS: void
//*
//* DESCRIPTION: Percorre os arrays de pinos de saída, entrada e interrupção, registrando cada um como um
//*              dispositivo de caractere no VFS.  Para cada pino, configura o hardware conforme sua função
//*              (direção, estado inicial e pulls) e invoca 'gpio_pin_register()' para expor '/dev/gpioN'.
//*
//* RETURNS: OK (0) em caso de sucesso.
//*
//**********************************************************************************************************************
int rp2040_dev_gpio_init(void) {


  // Índice de iteração sobre os arrays de pinos
  int i;

  // Contador auxiliar de pinos registrados (mantido para paridade com o padrão NuttX)
  int pincount = 0;

  /*
   * 1. REGISTRO DOS PINOS DE SAÍDA:
   *
   *    Para cada pino de saída, registra o dispositivo no VFS, configura a direção como saída e define o estado
   *    inicial como baixo (false).
   */

#if BOARD_NGPIOOUT > 0
  for (i = 0; i < BOARD_NGPIOOUT; i++) {

      // Define o tipo do pino como saída no descritor genérico
      g_gpout[i].gpio.gp_pintype = GPIO_OUTPUT_PIN;

      // Associa a tabela de operações de saída ao descritor
      g_gpout[i].gpio.gp_ops = &gpout_ops;

      // Armazena o índice deste pino no descritor
      g_gpout[i].id = i;

      // Registra o pino no VFS, expondo-o como /dev/gpioN
      gpio_pin_register(&g_gpout[i].gpio, g_gpiooutputs[i]);

      // Inicializa o pino conforme sua função (saída)
      rp2040_gpio_init(g_gpiooutputs[i]);

      // Define a direção do pino como saída
      rp2040_gpio_setdir(g_gpiooutputs[i], true);

      // Define o estado inicial do pino como baixo (false)
      rp2040_gpio_put(g_gpiooutputs[i], false);

      // Incrementa o contador de pinos processados
      pincount++;
 
   } // fim loop for

#endif

  // Zera o contador antes do próximo bloco
  pincount = 0;


  /*
   * 2. REGISTRO DOS PINOS DE ENTRADA:
   *
   *    Para cada pino de entrada, registra o dispositivo no VFS e configura o hardware como entrada. As pulls
   *    permanecem no estado padrão (nenhuma).
   */

#if BOARD_NGPIOIN > 0
  for (i = 0; i < BOARD_NGPIOIN; i++) {

      // Define o tipo do pino como entrada no descritor genérico
      g_gpin[i].gpio.gp_pintype = GPIO_INPUT_PIN;

      // Associa a tabela de operações de entrada ao descritor
      g_gpin[i].gpio.gp_ops = &gpin_ops;

      // Armazena o índice deste pino no descritor
      g_gpin[i].id = i;

      // Registra o pino no VFS, expondo-o como /dev/gpioN
      gpio_pin_register(&g_gpin[i].gpio, g_gpioinputs[i]);

      // Inicializa o pino conforme sua função (entrada)
      rp2040_gpio_init(g_gpioinputs[i]);

      // Incrementa o contador de pinos processados
      pincount++;

    } // fim loop for

#endif

  // Zera o contador antes do próximo bloco
  pincount = 0;


  /*
   * 3. REGISTRO DOS PINOS DE INTERRUPÇÃO:
   *
   *    Para cada pino com IRQ, registra o dispositivo no VFS, configura o hardware como entrada e habilita
   *    'pull-down' interno.  O attach do handler é feito sob demanda pelo 'user-space' via 'ioctl()'.
   */

#if BOARD_NGPIOINT > 0
  for (i = 0; i < BOARD_NGPIOINT; i++) {


      // Define o tipo do pino como entrada com interrupção
      g_gpint[i].rp2040gpio.gpio.gp_pintype = GPIO_INTERRUPT_PIN;

      // Associa a tabela de operações de interrupção ao descritor
      g_gpint[i].rp2040gpio.gpio.gp_ops = &gpint_ops;

      // Armazena o índice deste pino no descritor
      g_gpint[i].rp2040gpio.id = i;

      // Registra o pino no VFS, expondo-o como /dev/gpioN
      gpio_pin_register(&g_gpint[i].rp2040gpio.gpio, g_gpiointinputs[i]);

      // Inicializa o pino conforme sua função (entrada com IRQ)
      rp2040_gpio_init(g_gpiointinputs[i]);

      // Habilita pull-down interno (sem pull-up) para evitar flutuação quando o pino estiver solto
      rp2040_gpio_set_pulls(g_gpiointinputs[i], false, true);

      // Incrementa o contador de pinos processados
      pincount++;

    } // fim loop for

#endif

  // Retorna sucesso ao chamador (rp2040_bringup)
  return OK;

} // fim função int rp2040_dev_gpio_init(void)

#endif /* CONFIG_DEV_GPIO && !CONFIG_GPIO_LOWER_HALF */


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_gpio.c'
//----------------------------------------------------------------------------------------------------------------------
