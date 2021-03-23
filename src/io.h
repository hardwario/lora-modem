#ifndef _IO_H
#define _IO_H

#include "stm32l0xx_hal.h"

#define RADIO_RESET_PORT                          GPIOC
#define RADIO_RESET_PIN                           GPIO_PIN_0

#define RADIO_MOSI_PORT                           GPIOA
#define RADIO_MOSI_PIN                            GPIO_PIN_7

#define RADIO_MISO_PORT                           GPIOA
#define RADIO_MISO_PIN                            GPIO_PIN_6

#define RADIO_SCLK_PORT                           GPIOB
#define RADIO_SCLK_PIN                            GPIO_PIN_3

#define RADIO_NSS_PORT                            GPIOA
#define RADIO_NSS_PIN                             GPIO_PIN_15

#define RADIO_DIO_0_PORT                          GPIOB
#define RADIO_DIO_0_PIN                           GPIO_PIN_4

#define RADIO_DIO_1_PORT                          GPIOB
#define RADIO_DIO_1_PIN                           GPIO_PIN_1

#define RADIO_DIO_2_PORT                          GPIOB
#define RADIO_DIO_2_PIN                           GPIO_PIN_0

#define RADIO_DIO_3_PORT                          GPIOC
#define RADIO_DIO_3_PIN                           GPIO_PIN_13

#define RADIO_DIO_4_PORT                          GPIOA
#define RADIO_DIO_4_PIN                           GPIO_PIN_5

#define RADIO_DIO_5_PORT                          GPIOA
#define RADIO_DIO_5_PIN                           GPIO_PIN_4

#define RADIO_TCXO_VCC_PORT                       GPIOA
#define RADIO_TCXO_VCC_PIN                        GPIO_PIN_12

#define RADIO_ANT_SWITCH_PORT_RX                  GPIOA //CRF1
#define RADIO_ANT_SWITCH_PIN_RX                   GPIO_PIN_1

#define RADIO_ANT_SWITCH_PORT_TX_BOOST            GPIOC //CRF3
#define RADIO_ANT_SWITCH_PIN_TX_BOOST             GPIO_PIN_1

#define RADIO_ANT_SWITCH_PORT_TX_RFO              GPIOC //CRF2
#define RADIO_ANT_SWITCH_PIN_TX_RFO               GPIO_PIN_2

#define LPUART_TX_PIN                    GPIO_PIN_2
#define LPUART_TX_GPIO_PORT              GPIOA
#define LPUART_TX_AF                     GPIO_AF6_LPUART1
#define LPUART_RX_PIN                    GPIO_PIN_3
#define LPUART_RX_GPIO_PORT              GPIOA
#define LPUART_RX_AF                     GPIO_AF6_LPUART1

#define USART_TX_PIN                     GPIO_PIN_9
#define USART_TX_GPIO_PORT               GPIOA
#define USART_TX_AF                      GPIO_AF4_USART1
#define USART_RX_PIN                     GPIO_PIN_10
#define USART_RX_GPIO_PORT               GPIOA
#define USART_RX_AF                      GPIO_AF4_USART1

#endif
