/***********************************************************************************************************************
 * boards/arm/rp2040/raspberrypi-pico-w/src/rp2040_pico.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.  See the NOTICE
 * file distributed with this work for additional information regarding copyright ownership.
 *
 * The ASF licenses this file to you under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
 * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 * See the License for the specific language governing permissions and limitations under the License.
 *
 **********************************************************************************************************************/

/*
 * Inclusão de Diretivas de Compilação - Início ------------------------------------------------------------------------
 */

/* 
 * Guarda de inclusão: impede múltiplas inclusões do mesmo header no mesmo arquivo de código. O macro é derivado do
 * caminho do arquivo, garantindo unicidade no projeto.
 */

#ifndef __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_SRC_RP2040_PICO_H
#define __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_SRC_RP2040_PICO_H

/*
 * Inclusão de Bibliotecas em Arquivos de Cabeçalho --------------------------------------------------------------------
 */

/* 
 * Inclui macros globais de configuração do Kernel NuttX, geradas pelo Kconfig, necessárias para todas as opções
 */

// CONFIG_* utilizadas nas diretivas condicionais deste arquivo.
#include <nuttx/config.h>


/*
 * Protótipos de Funções Públicas --------------------------------------------------------------------------------------
 */

/* 
 * Protótipo da função principal de bring-up do board. Ela é definida em rp2040_bringup.c e chamada pelo framework de
 * inicialização do NuttX para configurar periféricos específicos da placa.
 */
int rp2040_bringup(void);

/* 
 * ATENÇÃO: A linha original abaixo foi comentada devido a um bug de BSP. A condição original ocultava o protótipo de
 * 'rp2040_dev_gpio_init()' quando 'CONFIG_ARCH_BOARD_COMMON=y', mesmo que 'CONFIG_DEV_GPIO=y', causando "implicit
 * declaration" no arquivo de 'bring-up'. A linha original era:
 *
 * #if defined(CONFIG_DEV_GPIO) && !defined(CONFIG_ARCH_BOARD_COMMON)
 * int rp2040_dev_gpio_init(void);
 * #endif
 *
 * A correção abaixo remove a restrição indevida, tornando o protótipo visível sempre que o driver de GPIO user-space
 * estiver habilitado, independentemente de o board usar bring-up comum. A implementação existe em 'rp2040_gpio.c' e 
 * deve ser declarada aqui para evitar o erro de declaração implícita.
*/

/*
 * Menuconfig CONFIG_DEV_GPIO: Habilita o driver de GPIO user-space, registrando dispositivos '/dev/gpioN' para acesso
 * por aplicações. No Pico W, os pinos GPIO25, GPIO6 e GPIO14 são registrados como dispositivos de caractere.
 */

#ifdef CONFIG_DEV_GPIO
int rp2040_dev_gpio_init(void);
#endif /* fim Menuconfig CONFIG_DEV_GPIO */

#endif /* __BOARDS_ARM_RP2040_RASPBERRYPI_PICO_SRC_RP2040_PICO_H */
// Fim da guarda de inclusão. O comentário identifica o macro para facilitar a navegação e evitar erros de pareamento.


//----------------------------------------------------------------------------------------------------------------------
// Fim do arquivo 'rp2040_pico.h'
//----------------------------------------------------------------------------------------------------------------------
