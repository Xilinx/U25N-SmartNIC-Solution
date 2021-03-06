/****************************************************************************
 * Firewall Error Codes for Xilinx U25N Accelerator Card.
 * Copyright 2021 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef FIREWALL__ERR_H
#define FIREWALL__ERR_H

typedef enum FirewallSdnetReturnType
{
    FIREWALL_SDNET_SUCCESS,                                       
    FIREWALL_SDNET_GENERAL_ERR_NULL_PARAM,                        
    FIREWALL_SDNET_GENERAL_ERR_INVALID_CONTEXT,                   
    FIREWALL_SDNET_GENERAL_ERR_INVALID_ENVIRONMENT_INTERFACE,     
    FIREWALL_SDNET_GENERAL_ERR_INTERNAL_ASSERTION,                

    FIREWALL_SDNET_BUILD_INFO_ERR_MALLOC_FAILED,                  

    FIREWALL_SDNET_INTERRUPT_ERR_MALLOC_FAILED,                   
    FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_MAX_NUM_ELEMENTS_EXCEED, 
    FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_MAX_NUM_FIFOS_EXCEED,    
    FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_INVALID_FIFO_NAME,       
    FIREWALL_SDNET_INTERRUPT_ERR_FIFO_NOT_FOUND,                 
    FIREWALL_SDNET_INTERRUPT_ERR_NAME_BUFFER_TOO_SMALL,          
    FIREWALL_SDNET_INTERRUPT_ERR_INVALID_ECC_ERROR_TYPE,          
    FIREWALL_SDNET_INTERRUPT_ERR_INVALID_ELEMENT_ID,              
    FIREWALL_SDNET_INTERRUPT_ERR_INVALID_FIFO_INDEX,              

    FIREWALL_SDNET_RANDOM_ERR_INVALID_MODE,                       
    FIREWALL_SDNET_RANDOM_ERR_ARRAY_INVALID_SIZE,                 
    FIREWALL_SDNET_RANDOM_ERR_INVALID_SEED,                       

    FIREWALL_SDNET_REGISTER_ERR_INVALID_DATA_SIZE,                
    FIREWALL_SDNET_REGISTER_ERR_INVALID_NUM_REGS,               
    FIREWALL_SDNET_REGISTER_ERR_INVALID_NUM_BITS,                 
    FIREWALL_SDNET_REGISTER_ERR_BUFFER_SIZE_MISMATCH,             
    FIREWALL_SDNET_REGISTER_ERR_INVALID_INDEX,                    

    FIREWALL_SDNET_COUNTER_ERR_CONFIG_INVALID_MODE,               
    FIREWALL_SDNET_COUNTER_ERR_CONFIG_UNSUPPORTED_OPERATION,      
    FIREWALL_SDNET_COUNTER_ERR_CONFIG_INVALID_NUM_COUNTERS,       
    FIREWALL_SDNET_COUNTER_ERR_INVALID_INDEX,                  
    FIREWALL_SDNET_COUNTER_ERR_INVALID_PACKET_COUNT,           
    FIREWALL_SDNET_COUNTER_ERR_INVALID_BYTE_COUNT,          

    FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_MODE,                
    FIREWALL_SDNET_METER_ERR_CONFIG_UNSUPPORTED_OPERATION,      
    FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_NUM_METERS,         
    FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_CLOCK_FREQ,         
    FIREWALL_SDNET_METER_ERR_INVALID_INDEX,                     
    FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_COLOUR_AWARE,      
    FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_DATA_RATE,         
    FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_BURST_SIZE,        
    FIREWALL_SDNET_METER_ERR_INVALID_NUM_BITS,                  

    FIREWALL_SDNET__ERR_INVALID_NUM_ENTRIES,                   
    FIREWALL_SDNET__ERR_LOOKUP_NOT_FOUND,                     
    FIREWALL_SDNET__ERR_INVALID_KEY,                          
    FIREWALL_SDNET__ERR_INVALID_ENDIAN,                       
    FIREWALL_SDNET__ERR_FULL,                                 
    FIREWALL_SDNET__ERR_NO_OPEN,                              
    FIREWALL_SDNET__ERR_INVALID_ARG,                          
    FIREWALL_SDNET__ERR_WRONG_KEY_WIDTH,                      
    FIREWALL_SDNET__ERR_TOO_MANY_INSTANCES,                   
    FIREWALL_SDNET__ERR_WRONG_BIT_FIELD_MASK,                 
    FIREWALL_SDNET__ERR_WRONG_CONST_FIELD_MASK,               
    FIREWALL_SDNET__ERR_WRONG_UNUSED_FIELD_MASK,              
    FIREWALL_SDNET__ERR_INVALID_TERNARY_FIELD_LEN,            
    FIREWALL_SDNET__ERR_WRONG_PRIO_WIDTH,                     
    FIREWALL_SDNET__ERR_WRONG_MAX,                            
    FIREWALL_SDNET__ERR_DUPLICATE_FOUND,                      
    FIREWALL_SDNET__ERR_WRONG_PREFIX,                         
    FIREWALL_SDNET__ERR_WRONG_PREFIX_MASK,                    
    FIREWALL_SDNET__ERR_WRONG_RANGE,                          
    FIREWALL_SDNET__ERR_WRONG_RANGE_MASK,                     
    FIREWALL_SDNET__ERR_KEY_NOT_FOUND,                        
    FIREWALL_SDNET__ERR_WRONG_MIN,                            
    FIREWALL_SDNET__ERR_WRONG_PRIO,                           
    FIREWALL_SDNET__ERR_WRONG_LIST_LENGTH,                    
    FIREWALL_SDNET__ERR_WRONG_NUMBER_OF_SLOTS,                
    FIREWALL_SDNET__ERR_INVALID_MEM_TYPE,                     
    FIREWALL_SDNET__ERR_TOO_HIGH_FREQUENCY,                   
    FIREWALL_SDNET__ERR_WRONG_TERNARY_MASK,                   
    FIREWALL_SDNET__ERR_MASKED_KEY_BIT_IS_SET,                
    FIREWALL_SDNET__ERR_INVALID_MODE,                         
    FIREWALL_SDNET__ERR_WRONG_RESPONSE_WIDTH,                 
    FIREWALL_SDNET__ERR_FORMAT_SYNTAX,                        
    FIREWALL_SDNET__ERR_TOO_MANY_FIELDS,                      
    FIREWALL_SDNET__ERR_TOO_MANY_RANGES,                      
    FIREWALL_SDNET__ERR_INVALID_RANGE_LEN,                    
    FIREWALL_SDNET__ERR_INVALID_RANGE_START,                  
    FIREWALL_SDNET__ERR_INVALID_PREFIX_LEN,                   
    FIREWALL_SDNET__ERR_INVALID_PREFIX_START,                 
    FIREWALL_SDNET__ERR_INVALID_PREFIX_KEY,                   
    FIREWALL_SDNET__ERR_INVALID_BIT_FIELD_LEN,                
    FIREWALL_SDNET__ERR_INVALID_BIT_FIELD_START,              
    FIREWALL_SDNET__ERR_INVALID_CONST_FIELD_LEN,              
    FIREWALL_SDNET__ERR_INVALID_CONST_FIELD_START,            
    FIREWALL_SDNET__ERR_INVALID_UNUSED_FIELD_LEN,             
    FIREWALL_SDNET__ERR_INVALID_UNUSED_FIELD_START,           
    FIREWALL_SDNET__ERR_MAX_KEY_LEN_EXCEED,                   
    FIREWALL_SDNET__ERR_INVALID_PRIO_AND_INDEX_WIDTH,         
    FIREWALL_SDNET__ERR_TOO_MANY_UNITS,                       
    FIREWALL_SDNET__ERR_NO_MASK,                              
    FIREWALL_SDNET__ERR_NOMEM,                                
    FIREWALL_SDNET__ERR_MALLOC_FAILED,                        
    FIREWALL_SDNET__ERR_OPTIMIZATION_TYPE_UNKNOWN,            
    FIREWALL_SDNET__ERR_UNKNOWN,                              
    FIREWALL_SDNET__ERR_INVALID_MEMORY_WIDTH,                 
    FIREWALL_SDNET__ERR_UNSUPPORTED_COMMAND,                  
    FIREWALL_SDNET__ERR_ENVIRONMENT,                          
    FIREWALL_SDNET__ERR_UNSUPPORTED__TYPE,                  
    FIREWALL_SDNET__ERR_NULL_POINTER,                         
    FIREWALL_SDNET__ERR_TOO_MANY_PCS,                         
    FIREWALL_SDNET__ERR_CONFIGURATION,                        
    FIREWALL_SDNET__ERR_ENVIRONMENT_FSMBUSY,                  
    FIREWALL_SDNET__ERR_ENVIRONMENT_POLLED_OUT,             

    FIREWALL_SDNET_TABLE_ERR_INVALID_TABLE_HANDLE_DRV,      
    FIREWALL_SDNET_TABLE_ERR_INVALID_TABLE_MODE,            
    FIREWALL_SDNET_TABLE_ERR_INVALID_ACTION_ID,               
    FIREWALL_SDNET_TABLE_ERR_PARAM_NOT_REQUIRED,            
    FIREWALL_SDNET_TABLE_ERR_ACTION_NOT_FOUND,                
    FIREWALL_SDNET_TABLE_ERR_FUNCTION_NOT_SUPPORTED,          
    FIREWALL_SDNET_TABLE_ERR_MALLOC_FAILED,                   
    FIREWALL_SDNET_TABLE_ERR_NAME_BUFFER_TOO_SMALL,           

    FIREWALL_SDNET_TARGET_ERR_MALLOC_FAILED,                  
    FIREWALL_SDNET_TARGET_ERR_TABLE_NOT_FOUND,                
    FIREWALL_SDNET_TARGET_ERR_MGMT_DRV_NOT_AVAILABLE,         
    FIREWALL_SDNET_TARGET_ERR_NAME_BUFFER_TOO_SMALL,        

    FIREWALL_SDNET_NUM_RETURN_CODES                           
} FirewallSdnetReturnType;

static const char * FirewallSdnetReturnTypeStrings[FIREWALL_SDNET_NUM_RETURN_CODES] =
{
    "FIREWALL_SDNET_SUCCESS",
    "FIREWALL_SDNET_GENERAL_ERR_NULL_PARAM",
    "FIREWALL_SDNET_GENERAL_ERR_INVALID_CONTEXT",
    "FIREWALL_SDNET_GENERAL_ERR_INVALID_ENVIRONMENT_INTERFACE",
    "FIREWALL_SDNET_GENERAL_ERR_INTERNAL_ASSERTION",

    "FIREWALL_SDNET_BUILD_INFO_ERR_MALLOC_FAILED",

    "FIREWALL_SDNET_INTERRUPT_ERR_MALLOC_FAILED",
    "FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_MAX_NUM_ELEMENTS_EXCEED",
    "FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_MAX_NUM_FIFOS_EXCEED",
    "FIREWALL_SDNET_INTERRUPT_ERR_CONFIG_INVALID_FIFO_NAME",
    "FIREWALL_SDNET_INTERRUPT_ERR_FIFO_NOT_FOUND",
    "FIREWALL_SDNET_INTERRUPT_ERR_NAME_BUFFER_TOO_SMALL",
    "FIREWALL_SDNET_INTERRUPT_ERR_INVALID_ECC_ERROR_TYPE",
    "FIREWALL_SDNET_INTERRUPT_ERR_INVALID_ELEMENT_ID",
    "FIREWALL_SDNET_INTERRUPT_ERR_INVALID_FIFO_INDEX",

    "FIREWALL_SDNET_RANDOM_ERR_INVALID_MODE",
    "FIREWALL_SDNET_RANDOM_ERR_ARRAY_INVALID_SIZE",
    "FIREWALL_SDNET_RANDOM_ERR_INVALID_SEED",

    "FIREWALL_SDNET_REGISTER_ERR_INVALID_DATA_SIZE",
    "FIREWALL_SDNET_REGISTER_ERR_INVALID_NUM_REGS",
    "FIREWALL_SDNET_REGISTER_ERR_INVALID_NUM_BITS",
    "FIREWALL_SDNET_REGISTER_ERR_BUFFER_SIZE_MISMATCH",
    "FIREWALL_SDNET_REGISTER_ERR_INVALID_INDEX",

    "FIREWALL_SDNET_COUNTER_ERR_CONFIG_INVALID_MODE",
    "FIREWALL_SDNET_COUNTER_ERR_CONFIG_UNSUPPORTED_OPERATION",
    "FIREWALL_SDNET_COUNTER_ERR_CONFIG_INVALID_NUM_COUNTERS",
    "FIREWALL_SDNET_COUNTER_ERR_INVALID_INDEX",
    "FIREWALL_SDNET_COUNTER_ERR_INVALID_PACKET_COUNT",
    "FIREWALL_SDNET_COUNTER_ERR_INVALID_BYTE_COUNT",

    "FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_MODE",
    "FIREWALL_SDNET_METER_ERR_CONFIG_UNSUPPORTED_OPERATION",
    "FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_NUM_METERS",
    "FIREWALL_SDNET_METER_ERR_CONFIG_INVALID_CLOCK_FREQ",
    "FIREWALL_SDNET_METER_ERR_INVALID_INDEX",
    "FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_COLOUR_AWARE",
    "FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_DATA_RATE",
    "FIREWALL_SDNET_METER_ERR_INVALID_PROFILE_BURST_SIZE",
    "FIREWALL_SDNET_METER_ERR_INVALID_NUM_BITS",
    "FIREWALL_SDNET__ERR_INVALID_NUM_ENTRIES",
    "FIREWALL_SDNET__ERR_LOOKUP_NOT_FOUND",
    "FIREWALL_SDNET__ERR_INVALID_KEY",
    "FIREWALL_SDNET__ERR_INVALID_ENDIAN",
    "FIREWALL_SDNET__ERR_FULL",
    "FIREWALL_SDNET__ERR_NO_OPEN",
    "FIREWALL_SDNET__ERR_INVALID_ARG",
    "FIREWALL_SDNET__ERR_WRONG_KEY_WIDTH",
    "FIREWALL_SDNET__ERR_TOO_MANY_INSTANCES",
    "FIREWALL_SDNET__ERR_WRONG_BIT_FIELD_MASK",
    "FIREWALL_SDNET__ERR_WRONG_CONST_FIELD_MASK",
    "FIREWALL_SDNET__ERR_WRONG_UNUSED_FIELD_MASK",
    "FIREWALL_SDNET__ERR_INVALID_TERNARY_FIELD_LEN",
    "FIREWALL_SDNET__ERR_WRONG_PRIO_WIDTH",
    "FIREWALL_SDNET__ERR_WRONG_MAX",
    "FIREWALL_SDNET__ERR_DUPLICATE_FOUND",
    "FIREWALL_SDNET__ERR_WRONG_PREFIX",
    "FIREWALL_SDNET__ERR_WRONG_PREFIX_MASK",
    "FIREWALL_SDNET__ERR_WRONG_RANGE",
    "FIREWALL_SDNET__ERR_WRONG_RANGE_MASK",
    "FIREWALL_SDNET__ERR_KEY_NOT_FOUND",
    "FIREWALL_SDNET__ERR_WRONG_MIN",
    "FIREWALL_SDNET__ERR_WRONG_PRIO",
    "FIREWALL_SDNET__ERR_WRONG_LIST_LENGTH",
    "FIREWALL_SDNET__ERR_WRONG_NUMBER_OF_SLOTS",
    "FIREWALL_SDNET__ERR_INVALID_MEM_TYPE",
    "FIREWALL_SDNET__ERR_TOO_HIGH_FREQUENCY",
    "FIREWALL_SDNET__ERR_WRONG_TERNARY_MASK",
    "FIREWALL_SDNET__ERR_MASKED_KEY_BIT_IS_SET",
    "FIREWALL_SDNET__ERR_INVALID_MODE",
    "FIREWALL_SDNET__ERR_WRONG_RESPONSE_WIDTH",
    "FIREWALL_SDNET__ERR_FORMAT_SYNTAX",
    "FIREWALL_SDNET__ERR_TOO_MANY_FIELDS",
    "FIREWALL_SDNET__ERR_TOO_MANY_RANGES",
    "FIREWALL_SDNET__ERR_INVALID_RANGE_LEN",
    "FIREWALL_SDNET__ERR_INVALID_RANGE_START",
    "FIREWALL_SDNET__ERR_INVALID_PREFIX_LEN",
    "FIREWALL_SDNET__ERR_INVALID_PREFIX_START",
    "FIREWALL_SDNET__ERR_INVALID_PREFIX_KEY",
    "FIREWALL_SDNET__ERR_INVALID_BIT_FIELD_LEN",
    "FIREWALL_SDNET__ERR_INVALID_BIT_FIELD_START",
    "FIREWALL_SDNET__ERR_INVALID_CONST_FIELD_LEN",
    "FIREWALL_SDNET__ERR_INVALID_CONST_FIELD_START",
    "FIREWALL_SDNET__ERR_INVALID_UNUSED_FIELD_LEN",
    "FIREWALL_SDNET__ERR_INVALID_UNUSED_FIELD_START",
    "FIREWALL_SDNET__ERR_MAX_KEY_LEN_EXCEED",
    "FIREWALL_SDNET__ERR_INVALID_PRIO_AND_INDEX_WIDTH",
    "FIREWALL_SDNET__ERR_TOO_MANY_UNITS",
    "FIREWALL_SDNET__ERR_NO_MASK",
    "FIREWALL_SDNET__ERR_NOMEM",
    "FIREWALL_SDNET__ERR_MALLOC_FAILED",
    "FIREWALL_SDNET__ERR_UNKNOWN",
    "FIREWALL_SDNET__ERR_INVALID_MEMORY_WIDTH",
    "FIREWALL_SDNET__ERR_UNSUPPORTED_COMMAND",
    "FIREWALL_SDNET__ERR_ENVIRONMENT",
    "FIREWALL_SDNET__ERR_UNSUPPORTED__TYPE",
    "FIREWALL_SDNET__ERR_NULL_POINTER",
    "FIREWALL_SDNET__ERR_TOO_MANY_PCS",
    "FIREWALL_SDNET__ERR_CONFIGURATION",
    "FIREWALL_SDNET__ERR_ENVIRONMENT_FSMBUSY",
    "FIREWALL_SDNET__ERR_ENVIRONMENT_POLLED_OUT",

    "FIREWALL_SDNET_TABLE_ERR_INVALID_TABLE_HANDLE_DRV",
    "FIREWALL_SDNET_TABLE_ERR_INVALID_TABLE_MODE",
    "FIREWALL_SDNET_TABLE_ERR_INVALID_ACTION_ID",
    "FIREWALL_SDNET_TABLE_ERR_PARAM_NOT_REQUIRED",
    "FIREWALL_SDNET_TABLE_ERR_ACTION_NOT_FOUND",
    "FIREWALL_SDNET_TABLE_ERR_FUNCTION_NOT_SUPPORTED",
    "FIREWALL_SDNET_TABLE_ERR_MALLOC_FAILED",
    "FIREWALL_SDNET_TABLE_ERR_NAME_BUFFER_TOO_SMALL",

    "FIREWALL_SDNET_TARGET_ERR_MALLOC_FAILED",
    "FIREWALL_SDNET_TARGET_ERR_TABLE_NOT_FOUND",
    "FIREWALL_SDNET_TARGET_ERR_NAME_BUFFER_TOO_SMALL"
};

const char *FirewallSdnetReturnTypeToString(int Value)
{
    if ((Value >= 0) && (Value < FIREWALL_SDNET_NUM_RETURN_CODES))
    {
        return FirewallSdnetReturnTypeStrings[Value];
    }

    return "FIREWALL_SDNET_UNKNOWN_RETURN_CODE";
}
#endif
