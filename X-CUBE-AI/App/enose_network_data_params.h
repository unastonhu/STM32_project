/**
  ******************************************************************************
  * @file    enose_network_data_params.h
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-07-25T01:34:18-0400
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

#ifndef ENOSE_NETWORK_DATA_PARAMS_H
#define ENOSE_NETWORK_DATA_PARAMS_H

#include "ai_platform.h"

/*
#define AI_ENOSE_NETWORK_DATA_WEIGHTS_PARAMS \
  (AI_HANDLE_PTR(&ai_enose_network_data_weights_params[1]))
*/

#define AI_ENOSE_NETWORK_DATA_CONFIG               (NULL)


#define AI_ENOSE_NETWORK_DATA_ACTIVATIONS_SIZES \
  { 5140, }
#define AI_ENOSE_NETWORK_DATA_ACTIVATIONS_SIZE     (5140)
#define AI_ENOSE_NETWORK_DATA_ACTIVATIONS_COUNT    (1)
#define AI_ENOSE_NETWORK_DATA_ACTIVATION_1_SIZE    (5140)



#define AI_ENOSE_NETWORK_DATA_WEIGHTS_SIZES \
  { 7904, }
#define AI_ENOSE_NETWORK_DATA_WEIGHTS_SIZE         (7904)
#define AI_ENOSE_NETWORK_DATA_WEIGHTS_COUNT        (1)
#define AI_ENOSE_NETWORK_DATA_WEIGHT_1_SIZE        (7904)



#define AI_ENOSE_NETWORK_DATA_ACTIVATIONS_TABLE_GET() \
  (&g_enose_network_activations_table[1])

extern ai_handle g_enose_network_activations_table[1 + 2];



#define AI_ENOSE_NETWORK_DATA_WEIGHTS_TABLE_GET() \
  (&g_enose_network_weights_table[1])

extern ai_handle g_enose_network_weights_table[1 + 2];


#endif    /* ENOSE_NETWORK_DATA_PARAMS_H */
