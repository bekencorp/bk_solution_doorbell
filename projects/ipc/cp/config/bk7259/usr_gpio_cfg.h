// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once


#ifdef __cplusplus
extern "C" {
#endif


/* CP-side GPIO configuration is used ONLY for declaring GPIO pins that
 * require interrupts, and for configuring the UART0 pins. All other GPIO
 * pinmux/pull/level/etc. settings should be configured on the AP side. */
#define GPIO_DEFAULT_DEV_CONFIG  \
{\
	{GPIO_8, GPIO_SECOND_FUNC_ENABLE, GPIO_DEV_GPIO_INPUT, GPIO_DEV_INVALID, GPIO_PULL_DISABLE, GPIO_INT_DISABLE, GPIO_INT_TYPE_FALLING_EDGE, GPIO_LOW_POWER_DISCARD_IO_STATUS, GPIO_DRIVER_CAPACITY_3, GPIO_INIT_ENABLE},\
	{GPIO_10, GPIO_SECOND_FUNC_ENABLE,  GPIO_DEV_UART0_RXD, GPIO_DEV_INVALID,   GPIO_PULL_UP_EN,   GPIO_INT_DISABLE, GPIO_INT_TYPE_LOW_LEVEL,    GPIO_LOW_POWER_DISCARD_IO_STATUS, GPIO_DRIVER_CAPACITY_0, GPIO_INIT_ENABLE},\
	{GPIO_11, GPIO_SECOND_FUNC_ENABLE,  GPIO_DEV_UART0_TXD, GPIO_DEV_GPIO_INPUT,   GPIO_PULL_UP_EN,   GPIO_INT_DISABLE, GPIO_INT_TYPE_LOW_LEVEL,    GPIO_LOW_POWER_DISCARD_IO_STATUS, GPIO_DRIVER_CAPACITY_0, GPIO_INIT_ENABLE},\
}
