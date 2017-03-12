/**
  ******************************************************************************
  * @file    R3_1_F30X_PWMnCurrFdbkClass.c
  * @author  STMicroelectronics - System Lab - MC Team
  * @version 4.3.0
  * @date    22-Sep-2016 15:29
  * @brief   This file contains implementation of current sensor class to be
  *          instantiated when the three shunts current sensing topology is 
  *          used.
  *          It is specifically designed for STM32F302x8 microcontrollers and
  *          implements the successive sampling of two motor current using
  *          only one ADC.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2016 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "PWMnCurrFdbkClass.h"
#include "PWMnCurrFdbkPrivate.h"
#include "R3_1_F30X_PWMnCurrFdbkClass.h"
#include "R3_1_F30X_PWMnCurrFdbkPrivate.h"
#include "MCIRQHandlerClass.h"
#include "MCIRQHandlerPrivate.h"
#include "MCLibraryConf.h"
#include "MCLibraryISRPriorityConf.h"
#include "MC_type.h"

#define TIMxCCER_MASK_CH123        ((uint32_t)  0x00000555u)

#define NB_CONVERSIONS 16u

#define CLASS_VARS   ((_CPWMC)this)->Vars_str
#define CLASS_PARAMS ((_CPWMC)this)->pParams_str
#define DCLASS_PARAMS ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str
#define DCLASS_VARS  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->DVars_str

#define PWM_PERIOD pLocalVars_Str->Half_PWMPeriod

#define CCMR2_CH4_DISABLE 0x8FFFu
#define CCMR2_CH4_PWM1    0x6000u
#define CCMR2_CH4_PWM2    0x7000u

#ifdef MC_CLASS_DYNAMIC
	#include "stdlib.h" /* Used for dynamic allocation */
#else
	_DCR3_1_F30X_PWMC_t R3_1_F30X_PWMCpool[MAX_DRV_PWMC_NUM];
	unsigned char R3_1_F30X_PWMC_Allocated = 0u;
#endif
        
/* DIR bits of TIM1 CR1 register identification for correct check of Counting direction detection*/
#define DIR_MASK 0x0010u       /* binary value: 0000000000010000 */
        
/* JSQR register Mask */
#define JSQR_CLEAR_Mask             ((uint32_t)0x00000000)
#define JSQR_EDGE_CLEAR_Mask        (~(uint32_t) ADC_ExternalTrigInjecEventEdge_BothEdge)            
        
/** 
  * @brief  BDTR structure definition 
  * @note   This extend the STD lib structure to set also
  *         BKIN2 enable/disable
  *         BKIN2 polarity
  *         BKIN  Filter
  *         BKIN2 Filter
  *         It must be used with the function TIM_BDTRConfig_MC
  */
typedef struct
{
  
  uint16_t TIM_OSSRState;        /*!< Specifies the Off-State selection used in Run mode.
                                      This parameter can be a value of @ref TIM_OSSR_Off_State_Selection_for_Run_mode_state */

  uint16_t TIM_OSSIState;        /*!< Specifies the Off-State used in Idle state.
                                      This parameter can be a value of @ref TIM_OSSI_Off_State_Selection_for_Idle_mode_state */

  uint16_t TIM_LOCKLevel;        /*!< Specifies the LOCK level parameters.
                                      This parameter can be a value of @ref TIM_Lock_level */ 

  uint16_t TIM_DeadTime;         /*!< Specifies the delay time between the switching-off and the
                                      switching-on of the outputs.
                                      This parameter can be a number between 0x00 and 0xFF  */

  uint32_t TIM_Break;            /*!< Specifies whether the TIM Break input is enabled or not. 
                                      This parameter can be a value of @ref TIM_Break1_Input_enable_disable */

  uint32_t TIM_BreakPolarity;    /*!< Specifies the TIM Break Input pin polarity.
                                      This parameter can be a value of @ref TIM_Break_Polarity */

  uint16_t TIM_AutomaticOutput;  /*!< Specifies whether the TIM Automatic Output feature is enabled or not. 
                                      This parameter can be a value of @ref TIM_AOE_Bit_Set_Reset */
  uint32_t TIM_Break2;           /*!< Specifies whether the TIM Break input is enabled or not. 
                                      This parameter can be a value of @ref TIM_Break2_Input_enable_disable */
  uint32_t TIM_Break2Polarity;   /*!< specifies the Break2 polarity.
                                      This parameter can be one of the following values:
                                      @arg TIM_Break2Polarity_Low: Break2 input is active low
                                      @arg TIM_Break2Polarity_High: Break2 input is active high */
  uint8_t TIM_Break1Filter;      /*!< Specifies the Break1 filter value.
                                      This parameter must be a value between 0x00 and 0x0F */
  uint8_t TIM_Break2Filter;      /*!< Specifies the Break2 filter value.
                                      This parameter must be a value between 0x00 and 0x0F */
} TIM_BDTRInitTypeDef_MC;

/* These function overloads the TIM_BDTRConfig and TIM_BDTRStructInit of the standard library */
static void TIM_BDTRConfig_MC(TIM_TypeDef* TIMx, TIM_BDTRInitTypeDef_MC *TIM_BDTRInitStruct);
static void TIM_BDTRStructInit_MC(TIM_BDTRInitTypeDef_MC* TIM_BDTRInitStruct);

static void R3_1_F30X_Init(CPWMC this);
static void R3_1_F30X_TIMxInit(TIM_TypeDef* TIMx, CPWMC this);
static void R3_1_F30X_COMPInit(CPWMC this);
static void R3_1_F30X_CurrentReadingCalibration(CPWMC this);
static void R3_1_F30X_GetPhaseCurrents(CPWMC this,Curr_Components* pStator_Currents);
static void R3_1_F30X_TurnOnLowSides(CPWMC this);
static void R3_1_F30X_SwitchOnPWM(CPWMC this);
static void R3_1_F30X_SwitchOffPWM(CPWMC this);
static void R3_1_F30X_ADC_SetSamplingTime(CPWMC this, ADConv_t ADConv_struct);
static void R3_1_F30X_HFCurrentsCalibrationAB(CPWMC this,Curr_Components* pStator_Currents);
static void R3_1_F30X_HFCurrentsCalibrationC(CPWMC this,Curr_Components* pStator_Currents);
static uint16_t R3_1_F30X_WriteTIMRegisters(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect1(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect2(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect3(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect4(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect5(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointSect6(CPWMC this);
static uint16_t R3_1_F30X_SetADCSampPointCalibration(CPWMC this);
static uint16_t R3_1_F30X_ExecRegularConv(CPWMC this, uint8_t bChannel);
static uint16_t R3_1_F30X_IsOverCurrentOccurred(CPWMC this);
static uint16_t F30X_GPIOPin2Source(uint16_t GPIO_Pin);
static uint16_t R3_1_F30X_RLDetectionModeSetDuty(CPWMC this, uint16_t hDuty);
static void R3_1_F30X_RLDetectionModeEnable(CPWMC this);
static void R3_1_F30X_RLDetectionModeDisable(CPWMC this);
static void R3_1_F30X_RLGetPhaseCurrents(CPWMC this,Curr_Components* pStator_Currents);
static void R3_1_F30X_RLTurnOnLowSides(CPWMC this);
static void R3_1_F30X_RLSwitchOnPWM(CPWMC this);
static void R3_1_F30X_RLSwitchOffPWM(CPWMC this);
static void *R3_1_F30X_IRQHandler(void *this, unsigned char flag);
static uint32_t R3_1_F30X_ADC_InjectedChannelConfig(ADC_TypeDef* ADCx, 
       uint8_t ADC_Channel, uint8_t Rank, uint8_t ADC_SampleTime, 
       uint8_t SequencerLength, uint16_t ADC_ExternalTriggerInjectedPolarity, 
       uint16_t ADC_ExternalTriggerInjected);
static uint32_t SingleADC_InjectedConfig(ADC_TypeDef* ADCx, 
       ADC_InjectedInitTypeDef* ADC_InjectedInitStruct);
static void R3_1_F30X_SetAOReferenceVoltage(uint32_t DAC_Channel, uint16_t hDACVref);

/**
  * @brief  Creates an object of the class R3_1_F30X
  * @param  pPWMnCurrFdbkParams pointer to an PWMnCurrFdbk parameters structure
  * @param  pR3_1_F30XParams pointer to an R3_1_F30X parameters structure
  * @retval CR3_1_F30X_PWMC new instance of R3_1_F30X object
  */
CR3_1_F30X_PWMC R3_1_F3XX_NewObject(pPWMnCurrFdbkParams_t pPWMnCurrFdbkParams, pR3_1_F30XParams_t pR3_1_F30XParams)
{
  _CPWMC _oPWMnCurrFdbk;
  _DCR3_1_F30X_PWMC _oR3_1_F30X;
  
  _oPWMnCurrFdbk = (_CPWMC)PWMC_NewObject(pPWMnCurrFdbkParams);
  
#ifdef MC_CLASS_DYNAMIC
  _oR3_1_F30X = (_DCR3_1_F30X_PWMC)calloc(1u,sizeof(_DCR3_1_F30X_PWMC_t));
#else
  if (R3_1_F30X_PWMC_Allocated  < MAX_DRV_PWMC_NUM)
  {
    _oR3_1_F30X = &R3_1_F30X_PWMCpool[R3_1_F30X_PWMC_Allocated++];
  }
  else
  {
    _oR3_1_F30X = MC_NULL;
  }
#endif
  
  _oR3_1_F30X->pDParams_str = pR3_1_F30XParams;
  _oPWMnCurrFdbk->DerivedClass = (void*)_oR3_1_F30X;
  
  _oPWMnCurrFdbk->Methods_str.pIRQ_Handler = &R3_1_F30X_IRQHandler;
  
  Set_IRQ_Handler(pR3_1_F30XParams->IRQnb, (_CMCIRQ)_oPWMnCurrFdbk);
  
  _oPWMnCurrFdbk->Methods_str.pPWMC_Init = &R3_1_F30X_Init;
  _oPWMnCurrFdbk->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_GetPhaseCurrents;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SwitchOffPWM = &R3_1_F30X_SwitchOffPWM;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SwitchOnPWM = &R3_1_F30X_SwitchOnPWM;        
  _oPWMnCurrFdbk->Methods_str.pPWMC_CurrentReadingCalibr = 
    &R3_1_F30X_CurrentReadingCalibration;         
  _oPWMnCurrFdbk->Methods_str.pPWMC_TurnOnLowSides = &R3_1_F30X_TurnOnLowSides;         
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect1 = 
    &R3_1_F30X_SetADCSampPointSect1;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect2 = 
    &R3_1_F30X_SetADCSampPointSect2;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect3 = 
    &R3_1_F30X_SetADCSampPointSect3;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect4 = 
    &R3_1_F30X_SetADCSampPointSect4;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect5 = 
    &R3_1_F30X_SetADCSampPointSect5;
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetADCSampPointSect6 = 
    &R3_1_F30X_SetADCSampPointSect6;
  _oPWMnCurrFdbk->Methods_str.pPWMC_ExecRegularConv= &R3_1_F30X_ExecRegularConv;
  
  _oPWMnCurrFdbk->Methods_str.pPWMC_SetSamplingTime= &R3_1_F30X_ADC_SetSamplingTime;
  
  _oPWMnCurrFdbk->Methods_str.pPWMC_IsOverCurrentOccurred = 
    &R3_1_F30X_IsOverCurrentOccurred;
  
  _oPWMnCurrFdbk->Methods_str.pRLDetectionModeEnable = &R3_1_F30X_RLDetectionModeEnable;
  
  _oPWMnCurrFdbk->Methods_str.pRLDetectionModeDisable = &R3_1_F30X_RLDetectionModeDisable;
  
  _oPWMnCurrFdbk->Methods_str.pRLDetectionModeSetDuty = &R3_1_F30X_RLDetectionModeSetDuty;
  
  return ((CR3_1_F30X_PWMC)_oPWMnCurrFdbk);
}

/** @addtogroup STM32_PMSM_MC_Library
  * @{
  */
  
/** @addtogroup PWMnCurrFdbk_R3_1_F30X
  * @{
  */

/** @defgroup R3_1_F30X_class_private_methods R3_1_F30X class private methods
* @{
*/

/**
* @brief  It initializes peripherals for current reading 
*         in three shunts configuration using STM32F302x8
* @param  this: related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_Init(CPWMC this)
{
  NVIC_InitTypeDef NVIC_InitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  ADC_InitTypeDef ADC_InitStructure;
  ADC_CommonInitTypeDef ADC_CommonInitStructure;
  ADC_InjectedInitTypeDef ADC_InjectedInitStruct;
  pVars_t pVars_str = &CLASS_VARS;
  pDVars_t pDVars_str = &DCLASS_VARS;  
  pDParams_t pDParams_str = DCLASS_PARAMS; 

  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  ADC_TypeDef* ADCx_1;
 
  
  pVars_str->bMotor = (pDParams_str->bInstanceNbr==1u?M1:M2);
  pDVars_str->Half_PWMPeriod = ((((_CPWMC) this)->pParams_str->hPWMperiod)/2u);
    
  /* Peripheral clocks enabling ---------------------------------------------*/
  
  RCC->AHBENR |= RCC_AHBPeriph_CRC;
  
  /* ADC Periph clock enable */ 
  RCC_AHBPeriphClockCmd(pDParams_str->wAHBPeriph, ENABLE);
  
  /* Enable GPIOA-GPIOI clock */
  RCC_AHBPeriphClockCmd( RCC_AHBPeriph_GPIOA | 
                         RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOC | 
                           RCC_AHBPeriph_GPIOD | RCC_AHBPeriph_GPIOE | 
                             RCC_AHBPeriph_GPIOF, ENABLE);    
  /* Enable DMA1 clock */
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE); 
  
  /* Enable TIM1 - TIM8 clock */
  if(TIMx == TIM1)
  {
    /* Enable TIM1 clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
		/* Clock source is APB high speed clock*/
		RCC_TIMCLKConfig(RCC_TIM1CLK_HCLK);
  }
  else
  {
    /* Enable TIM8 clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);
		/* Clock source is APB high speed clock*/
		RCC_TIMCLKConfig(RCC_TIM8CLK_HCLK);
  }
  
  /* Enable the CCS */
  RCC_ClockSecuritySystemCmd((FunctionalState)(ENABLE));
	
	R3_1_F30X_TIMxInit(TIMx, this);
  
  /* GPIOs configurations --------------------------------------------------*/
  GPIO_StructInit(&GPIO_InitStructure);
  
  
  /****** Configure phase A ADC channel GPIO as analog input ****/
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hIaPin;  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_Init(pDParams_str->hIaPort,
            &GPIO_InitStructure);
  GPIO_PinLockConfig(pDParams_str->hIaPort, pDParams_str->hIaPin);
  
  /****** Configure phase B ADC channel GPIO as analog input ****/
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hIbPin;  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_Init(pDParams_str->hIbPort, &GPIO_InitStructure);
  GPIO_PinLockConfig(pDParams_str->hIbPort, pDParams_str->hIbPin);
  
  /****** Configure phase C ADC channel GPIO as analog input ****/
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hIcPin;  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_Init(pDParams_str->hIcPort, &GPIO_InitStructure);
  GPIO_PinLockConfig(pDParams_str->hIcPort, pDParams_str->hIcPin);
  
  
  /****** Configure TIMx Channel 1, 2 and 3 Outputs ******/ 
  GPIO_PinAFConfig(pDParams_str->hCh1Port, F30X_GPIOPin2Source(pDParams_str->hCh1Pin), pDParams_str->bCh1AF);
  GPIO_PinAFConfig(pDParams_str->hCh2Port, F30X_GPIOPin2Source(pDParams_str->hCh2Pin), pDParams_str->bCh2AF);
  GPIO_PinAFConfig(pDParams_str->hCh3Port, F30X_GPIOPin2Source(pDParams_str->hCh3Pin), pDParams_str->bCh3AF);
  
  GPIO_StructInit(&GPIO_InitStructure);
  
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh1Pin;
  GPIO_Init(pDParams_str->hCh1Port, &GPIO_InitStructure);
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh2Pin;
  GPIO_Init(pDParams_str->hCh2Port, &GPIO_InitStructure);
  GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh3Pin;
  GPIO_Init(pDParams_str->hCh3Port, &GPIO_InitStructure);
  
  GPIO_PinLockConfig(pDParams_str->hCh1Port, pDParams_str->hCh1Pin);
  GPIO_PinLockConfig(pDParams_str->hCh2Port, pDParams_str->hCh2Pin);
  GPIO_PinLockConfig(pDParams_str->hCh3Port, pDParams_str->hCh3Pin);
  
  
  /****** Configure TIMx Channel 1N, 2N and 3N Outputs, if enabled ******/    
  if ((pDParams_str->LowSideOutputs)== LS_PWM_TIMER) 
  { 
    GPIO_PinAFConfig(pDParams_str->hCh1NPort, F30X_GPIOPin2Source(pDParams_str->hCh1NPin), pDParams_str->bCh1NAF);
    GPIO_PinAFConfig(pDParams_str->hCh2NPort, F30X_GPIOPin2Source(pDParams_str->hCh2NPin), pDParams_str->bCh2NAF);
    GPIO_PinAFConfig(pDParams_str->hCh3NPort, F30X_GPIOPin2Source(pDParams_str->hCh3NPin), pDParams_str->bCh3NAF);
    
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh1NPin;  
    GPIO_Init(pDParams_str->hCh1NPort, &GPIO_InitStructure);  
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh2NPin;  
    GPIO_Init(pDParams_str->hCh2NPort, &GPIO_InitStructure);    
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh3NPin;  
    GPIO_Init(pDParams_str->hCh3NPort, &GPIO_InitStructure);
    
    GPIO_PinLockConfig(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin);
    GPIO_PinLockConfig(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin);
    GPIO_PinLockConfig(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin);
  }  
  else if ((pDParams_str->LowSideOutputs)== ES_GPIO)
  {
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh1NPin;
    GPIO_Init(pDParams_str->hCh1NPort, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh2NPin;
    GPIO_Init(pDParams_str->hCh2NPort, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hCh3NPin;
    GPIO_Init(pDParams_str->hCh3NPort, &GPIO_InitStructure);
    
    GPIO_PinLockConfig(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin);
    GPIO_PinLockConfig(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin);
    GPIO_PinLockConfig(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin);
    
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  }
  else
  {
  }
  
  /****** Configure TIMx BKIN input, if enabled ******/
  if ((pDParams_str->bBKINMode) == EXT_MODE)
  {
    GPIO_PinAFConfig(pDParams_str->hBKINPort, F30X_GPIOPin2Source(pDParams_str->hBKINPin), pDParams_str->bBKINAF);
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hBKINPin;  
    GPIO_Init(pDParams_str->hBKINPort, &GPIO_InitStructure); 
    GPIO_PinLockConfig(pDParams_str->hBKINPort, pDParams_str->hBKINPin);
  }
  
  /****** Configure TIMx BKIN2 input, if enabled ******/
  if ((pDParams_str->bBKIN2Mode) == EXT_MODE)
  {
    GPIO_PinAFConfig(pDParams_str->hBKIN2Port, F30X_GPIOPin2Source(pDParams_str->hBKIN2Pin), pDParams_str->bBKIN2AF);
    GPIO_InitStructure.GPIO_Pin = pDParams_str->hBKIN2Pin;  
    GPIO_Init(pDParams_str->hBKIN2Port, &GPIO_InitStructure); 
    GPIO_PinLockConfig(pDParams_str->hBKIN2Port, pDParams_str->hBKIN2Pin);
  }
  
  if(TIMx == TIM1)
  {   
    /* TIM1 Counter Clock stopped when the core is halted */
    DBGMCU_APB2PeriphConfig(DBGMCU_TIM1_STOP, ENABLE);
  }
  else
  {
    /* TIM8 Counter Clock stopped when the core is halted */
    DBGMCU_APB2PeriphConfig(DBGMCU_TIM8_STOP, ENABLE);
  }
  
  R3_1_F30X_COMPInit(this);
 
  /* Assignment of ADC resources for motor phases current measurements*/
  ADCx_1 = ADC1;
  pDVars_str->ADCx_1 = ADCx_1;
    
  /* For TIM1 the defualt vualue ADC_ExternalTrigInjecConvEvent_0 (TIM1_TRGO)
  is used.*/
  pDVars_str->ADC_ExternalTriggerInjected = ADC_ExternalTrigInjecConvEvent_0;
	
  /* Init ADC peripherals and related IRQ handler*/
  ADC_DeInit(ADC1);
  
  /* Common init */
  ADC_CommonStructInit(&ADC_CommonInitStructure);
  ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
  ADC_CommonInitStructure.ADC_Clock = pDParams_str->wADC_Clock_Divider;
  ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
  ADC_CommonInitStructure.ADC_DMAMode = ADC_DMAMode_OneShot;
  ADC_CommonInitStructure.ADC_TwoSamplingDelay= 0u;
  ADC_CommonInit(ADC1, &ADC_CommonInitStructure);
  
  ADC_VoltageRegulatorCmd(ADC1, ENABLE);
  
  /* Wait for Regulator Startup time, once for both */
  {
    uint16_t waittime = 0u;
    for(waittime=0u;waittime<65000u;waittime++)
    {
	waittime=waittime;
    }
  }    
  
  ADC_SelectCalibrationMode(ADC1,ADC_CalibrationMode_Single);    
  ADC_StartCalibration(ADC1);
  while (ADC_GetCalibrationStatus(ADC1)== SET )
  {
  }
  
 
  /* Enable the ADC Interrupt */
#if defined(STM32F302x8)
  NVIC_InitStructure.NVIC_IRQChannel = (uint8_t) ADC1_IRQn;
#else
  NVIC_InitStructure.NVIC_IRQChannel = (uint8_t) ADC1_2_IRQn;
#endif
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = ADC_PRE_EMPTION_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = ADC_SUB_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);    
  
  /* ADCx_1 and ADCx_2 registers configuration ---------------------------------*/
  
  /* Enable ADCx_1 */
  ADC_Cmd(ADC1, ENABLE);
    
  /* Configure the ADC_x1&2 for reg conversions */
  ADC_StructInit(&ADC_InitStructure);
  ADC_InitStructure.ADC_ContinuousConvMode = ADC_ContinuousConvMode_Disable;
  ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b; 
  ADC_InitStructure.ADC_ExternalTrigConvEvent = ADC_ExternalTrigConvEvent_0;
  ADC_InitStructure.ADC_ExternalTrigEventEdge = ADC_ExternalTrigEventEdge_None;    
  ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Left;
  ADC_InitStructure.ADC_OverrunMode = ADC_OverrunMode_Disable;
  ADC_InitStructure.ADC_AutoInjMode = ADC_AutoInjec_Disable;
  ADC_InitStructure.ADC_NbrOfRegChannel = 1u;
  
  ADC_Init(ADC1, &ADC_InitStructure);
    
  /* Configuration of ADC sequence of two currents for the future JSQR register setting*/     
  ADC_InjectedInitStruct.ADC_ExternalTrigInjecConvEvent =ADC_ExternalTrigInjecConvEvent_0;
  ADC_InjectedInitStruct.ADC_ExternalTrigInjecEventEdge = ADC_ExternalTrigInjecEventEdge_RisingEdge;
  ADC_InjectedInitStruct.ADC_NbrOfInjecChannel = 2u;
  
  /*AB currents sequence --------------------------------------------------------------------------------*/
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIaChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIbChannel;
  ADC_InjectedInitStruct.ADC_InjecSequence3 = 0u;
  ADC_InjectedInitStruct.ADC_InjecSequence4 = 0u;
  ADC_InjectedChannelSampleTimeConfig(ADC1,pDParams_str->bIaChannel,pDParams_str->b_IaSamplingTime);
  ADC_InjectedChannelSampleTimeConfig(ADC1,pDParams_str->bIbChannel,pDParams_str->b_IbSamplingTime);
  
  pDVars_str->wADC_JSQR_phAB= SingleADC_InjectedConfig(ADC1, &ADC_InjectedInitStruct);
  
  /*BA currents sequence --------------------------------------------------------------------------------*/    
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIbChannel;
  ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIaChannel;
  
  pDVars_str->wADC_JSQR_phBA= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct); 
  
  /*AC currents sequence --------------------------------------------------------------------------------*/   
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIaChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 =pDParams_str->bIcChannel; 
  ADC_InjectedChannelSampleTimeConfig(ADCx_1,pDParams_str->bIaChannel,pDParams_str->b_IaSamplingTime);
  ADC_InjectedChannelSampleTimeConfig(ADCx_1,pDParams_str->bIcChannel,pDParams_str->b_IcSamplingTime);
  
  pDVars_str->wADC_JSQR_phAC= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  
  /*CA currents sequence --------------------------------------------------------------------------------*/      
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIcChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIaChannel; 
  
  pDVars_str->wADC_JSQR_phCA= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  
  /*BC currents sequence --------------------------------------------------------------------------------*/      
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIbChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIcChannel;    
  ADC_InjectedChannelSampleTimeConfig(ADCx_1,pDParams_str->bIbChannel,pDParams_str->b_IbSamplingTime);
  ADC_InjectedChannelSampleTimeConfig(ADCx_1,pDParams_str->bIcChannel,pDParams_str->b_IcSamplingTime);
  
  pDVars_str->wADC_JSQR_phBC= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  
  /*CB currents sequence -------------------------------------------------------------------------------- */      
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIcChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIbChannel; 
  
  pDVars_str->wADC_JSQR_phCB= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  /* ---------------------------------------------------------------------------------------------------- */
  
  /* Configuration of ADC single sequence of single current for the future JSQR register setting*/    
  
  /* Common settings for Single Phase current reading ------------------------------------------*/
  ADC_InjectedInitStruct.ADC_ExternalTrigInjecConvEvent = ADC_ExternalTrigInjecConvEvent_0;
  ADC_InjectedInitStruct.ADC_ExternalTrigInjecEventEdge = ADC_ExternalTrigInjecEventEdge_RisingEdge;
  
  /* Single Phase A current acquisition configuration ------------------------------------------*/
  ADC_InjectedInitStruct.ADC_NbrOfInjecChannel =1u;
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIaChannel; 
  ADC_InjectedInitStruct.ADC_InjecSequence2 = 0u;
  ADC_InjectedInitStruct.ADC_InjecSequence3 = 0u;
  ADC_InjectedInitStruct.ADC_InjecSequence4 = 0u;
  
  pDVars_str->wADC_JSQR_phA= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  
  /* Single Phase B current acquisition configuration ------------------------------------------*/
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIbChannel;
  
  pDVars_str->wADC_JSQR_phB= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);
  
  /* Single Phase C current acquisition configuration ------------------------------------------*/
  ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIcChannel;
  
  pDVars_str->wADC_JSQR_phC= SingleADC_InjectedConfig(ADCx_1, &ADC_InjectedInitStruct);   
  
  /* Queue Of Context Mode for injected channels Enabling */
  ADC_SelectQueueOfContextMode(ADCx_1,ENABLE);  /* JQM =1*/
  
  ADCx_1->JSQR = R3_1_F30X_ADC_InjectedChannelConfig(ADCx_1, 0u, 1u, 0u, 2u, ADC_ExternalTrigInjecEventEdge_RisingEdge, pDVars_str->ADC_ExternalTriggerInjected);
  ADCx_1->CR |= ADC_CR_JADSTART;
  
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Disable);
	
  /* Fictitious ADC1 Trigger to start ADC1 Conversion. */
  TIMx->CCR4 = 0xFFFFu;
  TIMx->CCR4 = 0x0u;
  
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);

  /* ADC_FLAG_JEOS wait if is RESET. Wait the end of ADC1 injected conversion sequence.  */
  while (ADC_GetFlagStatus(ADCx_1,ADC_FLAG_JEOS)==RESET)
  {
  }
  
  /* ADCx_1 Injected conversions end interrupt enabling */
  ADC_ClearFlag(ADCx_1, ADC_FLAG_JEOS);
  ADC_ITConfig(ADCx_1, ADC_IT_JEOS, ENABLE);
  
  if(pDParams_str->TIMx==TIM1)
  {
    /* Enable the TIM1 BRK interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = (uint8_t) TIM1_BRK_TIM15_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = TIMx_BRK_PRE_EMPTION_PRIORITY;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = TIMx_BRK_SUB_PRIORITY;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
  }
  else
  {
  }
  
  /* Clear the flags */
  pDVars_str->OverVoltageFlag = FALSE;
  pDVars_str->OverCurrentFlag = FALSE;
}

/**
* @brief  It initializes TIMx peripheral for PWM generation
* @param 'TIMx': Timer to be initialized
* @param 'this': related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_TIMxInit(TIM_TypeDef* TIMx, CPWMC this)
{
  TIM_TimeBaseInitTypeDef TIMx_TimeBaseStructure;
  TIM_OCInitTypeDef TIMx_OCInitStructure;
  TIM_BDTRInitTypeDef_MC TIMx_BDTRInitStructure;
  pDVars_t pDVars_str = &DCLASS_VARS;  
  pDParams_t pDParams_str =DCLASS_PARAMS; 
  
  /* TIMx Peripheral Configuration -------------------------------------------*/
  /* TIMx Registers reset */
  TIM_DeInit(TIMx);
  TIM_TimeBaseStructInit(&TIMx_TimeBaseStructure);
  /* Time Base configuration */
  TIMx_TimeBaseStructure.TIM_Prescaler = (uint16_t)(pDParams_str->bTim_Clock_Divider) - 1u;
  TIMx_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_CenterAligned1;
  TIMx_TimeBaseStructure.TIM_Period = pDVars_str->Half_PWMPeriod;
  TIMx_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV2;
  TIMx_TimeBaseStructure.TIM_RepetitionCounter = pDParams_str->bRepetitionCounter;
  TIM_TimeBaseInit(TIMx, &TIMx_TimeBaseStructure);
  
  /* Channel 1, 2,3 and 4 Configuration in PWM mode */
  TIM_OCStructInit(&TIMx_OCInitStructure);  
  TIMx_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1; 
  TIMx_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; 
  TIMx_OCInitStructure.TIM_Pulse = (uint32_t)(pDVars_str->Half_PWMPeriod)/2u; /* dummy value */
  
  /* Channel 1 */
  TIMx_OCInitStructure.TIM_OCPolarity = pDParams_str->hCh1Polarity;      
  TIMx_OCInitStructure.TIM_OCIdleState = pDParams_str->hCh1IdleState;    
  if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
  {
    TIMx_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable; 
    TIMx_OCInitStructure.TIM_OCNPolarity = pDParams_str->hCh1NPolarity; 
    TIMx_OCInitStructure.TIM_OCNIdleState = pDParams_str->hCh1NIdleState;     
  }    
  else
  {
    TIMx_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Disable;
  }    
  TIM_OC1Init(TIMx, &TIMx_OCInitStructure); 
  
  
  /* Channel 2 */
  TIMx_OCInitStructure.TIM_OCPolarity = pDParams_str->hCh2Polarity;      
  TIMx_OCInitStructure.TIM_OCIdleState = pDParams_str->hCh2IdleState;    
  if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
  {
  TIMx_OCInitStructure.TIM_OCNPolarity = pDParams_str->hCh2NPolarity; 
  TIMx_OCInitStructure.TIM_OCNIdleState = pDParams_str->hCh2NIdleState;         
  }
  TIM_OC2Init(TIMx, &TIMx_OCInitStructure); 
  
  
  /* Channel 3 */
  TIMx_OCInitStructure.TIM_OCPolarity = pDParams_str->hCh3Polarity;      
  TIMx_OCInitStructure.TIM_OCIdleState = pDParams_str->hCh3IdleState;    
  if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
  {
  TIMx_OCInitStructure.TIM_OCNPolarity = pDParams_str->hCh3NPolarity; 
  TIMx_OCInitStructure.TIM_OCNIdleState = pDParams_str->hCh3NIdleState;         
  }
  TIM_OC3Init(TIMx, &TIMx_OCInitStructure);   
  
  /* Channel 4 */
  TIMx_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2; 
  TIMx_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;      
  TIMx_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Reset; 
  TIMx_OCInitStructure.TIM_Pulse = 0xFFFFu;
  TIM_OC4Init(TIMx, &TIMx_OCInitStructure); 
  
  /* Enables the TIMx Preload on CC1 Register */
  TIM_OC1PreloadConfig(TIMx, TIM_OCPreload_Enable);
  /* Enables the TIMx Preload on CC2 Register */
  TIM_OC2PreloadConfig(TIMx, TIM_OCPreload_Enable);
  /* Enables the TIMx Preload on CC3 Register */
  TIM_OC3PreloadConfig(TIMx, TIM_OCPreload_Enable);
  /* Enables the TIMx Preload on CC4 Register */
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable); 
  
  /* Set channel 4 as TRGO (Center TRIGGER - Overflow of TIM1)*/
  TIM_SelectOutputTrigger(TIMx,TIM_TRGOSource_OC4Ref);
  
  TIM_BDTRStructInit_MC(&TIMx_BDTRInitStructure);
  /* Dead Time */
  TIMx_BDTRInitStructure.TIM_OSSRState = TIM_OSSRState_Enable;
  TIMx_BDTRInitStructure.TIM_OSSIState = TIM_OSSIState_Disable;
  TIMx_BDTRInitStructure.TIM_LOCKLevel = TIM_LOCKLevel_1; 
  TIMx_BDTRInitStructure.TIM_DeadTime = (pDParams_str->hDeadTime)/2u;
  
  /* Always enable BKIN for safety fature */
  TIMx_BDTRInitStructure.TIM_Break = TIM_Break1_Enable;
  if ((pDParams_str->bBKINMode) == EXT_MODE)
  {
    /* Set from the power stage */
    TIMx_BDTRInitStructure.TIM_BreakPolarity = pDParams_str->hBKINPolarity;
  }
  else
  {
    /* Internal - always active high */
    TIMx_BDTRInitStructure.TIM_BreakPolarity = TIM_Break1Polarity_High;
  }
  TIMx_BDTRInitStructure.TIM_Break1Filter = pDParams_str->bBKINFilter;
  TIMx_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Disable;
  TIM_ClearITPendingBit(TIMx, TIM_IT_Break);
  TIM_ITConfig(TIMx, TIM_IT_Break, ENABLE);
  
  if ((pDParams_str->bBKIN2Mode) != NONE)
  {
    TIMx_BDTRInitStructure.TIM_Break2 = TIM_Break2_Enable;
    if ((pDParams_str->bBKIN2Mode) == EXT_MODE)
    {
      /* Set from the power stage */
      TIMx_BDTRInitStructure.TIM_Break2Polarity = pDParams_str->hBKIN2Polarity;
    }
    else
    {
      /* Internal - always active high */
      TIMx_BDTRInitStructure.TIM_Break2Polarity = TIM_Break2Polarity_High;
    }
    TIMx_BDTRInitStructure.TIM_Break2Filter = pDParams_str->bBKIN2Filter;
    TIMx_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Disable;
    TIMx->SR = ~(((uint32_t)(TIM_IT_Break)<<1)); /* Clear BKIN2F. Not possible with stdlib V1.0.1 */
    TIM_ITConfig(TIMx, TIM_IT_Break, ENABLE);
  }
  TIM_BDTRConfig_MC(TIMx, &TIMx_BDTRInitStructure);
 
  TIM_SelectInputTrigger(TIMx,TIM_TS_ITR1);
  TIM_SelectSlaveMode(TIMx,TIM_SlaveMode_Trigger);

  TIM_Cmd(TIMx, ENABLE);
}

/**
* @brief  It stores into 'this' object variables the voltage present on Ia and 
*         Ib current feedback analog channels when no current is flowin into the
*         motor
* @param  this: related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_CurrentReadingCalibration(CPWMC this)
{
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDVars_t pDVars_str = &DCLASS_VARS;
  pDParams_t pDParams_str =  DCLASS_PARAMS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  uint16_t hCalibrationPeriodCounter;
  uint16_t hMaxPeriodsNumber;
  
  pDVars_str-> wPhaseAOffset = 0u;
  pDVars_str-> wPhaseBOffset = 0u; 
  pDVars_str-> wPhaseCOffset = 0u; 
  
  pDVars_str->bIndex=0u;
  
  /* It forces inactive level on TIMx CHy and CHyN */
  TIMx->CCER &= (~TIMxCCER_MASK_CH123);
   
  /* Offset calibration for A & B phases */
  /* Change function to be executed in ADCx_ISR */ 
  ((_CPWMC) this)->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_HFCurrentsCalibrationAB;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect1 = &R3_1_F30X_SetADCSampPointCalibration;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect2 = &R3_1_F30X_SetADCSampPointCalibration;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect3 = &R3_1_F30X_SetADCSampPointCalibration;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect4 = &R3_1_F30X_SetADCSampPointCalibration;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect5 = &R3_1_F30X_SetADCSampPointCalibration;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect6 = &R3_1_F30X_SetADCSampPointCalibration;
  
   pDVars_str->wADC1_JSQR = pDVars_str->wADC_JSQR_phAB;
  
  R3_1_F30X_SwitchOnPWM(this);
  
  /* Wait for NB_CONVERSIONS to be executed */
  hMaxPeriodsNumber=(NB_CONVERSIONS+1u)*(((uint16_t)(pDParams_str->bRepetitionCounter)+1u)>>1);
  TIMx->SR = (uint16_t)~TIM_FLAG_CC1;
  hCalibrationPeriodCounter = 0u;
  while (pDVars_str->bIndex < NB_CONVERSIONS)
  {
    if (TIMx->SR & TIM_FLAG_CC1)
    {
      TIMx->SR = (uint16_t)~TIM_FLAG_CC1;
      hCalibrationPeriodCounter++;
      if (hCalibrationPeriodCounter >= hMaxPeriodsNumber)
      {
        if (pDVars_str->bIndex < NB_CONVERSIONS)
        {
          pBaseVars->SWerror = 1u;
          break;
        }
      }
    }
  }
  
  R3_1_F30X_SwitchOffPWM(this);

  /* Offset calibration for C phase */
  /* Reset bIndex */
  pDVars_str->bIndex=0u;

  /* Change function to be executed in ADCx_ISR */ 
  ((_CPWMC) this)->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_HFCurrentsCalibrationC;

/* "Phase C current calibration to verify"    */
  pDVars_str->wADC1_JSQR = pDVars_str->wADC_JSQR_phC;
  
  R3_1_F30X_SwitchOnPWM(this);
  
  /* Wait for NB_CONVERSIONS to be executed */
  TIMx->SR = (uint16_t)~TIM_FLAG_CC1;
  hCalibrationPeriodCounter = 0u;
  while (pDVars_str->bIndex < NB_CONVERSIONS)
  {
    if (TIMx->SR & TIM_FLAG_CC1)
    {
      TIMx->SR = (uint16_t)~TIM_FLAG_CC1;
      hCalibrationPeriodCounter++;
      if (hCalibrationPeriodCounter >= hMaxPeriodsNumber)
      {
        if (pDVars_str->bIndex < NB_CONVERSIONS)
        {
          pBaseVars->SWerror = 1u;
          break;
        }
      }
    }
  }
  
  R3_1_F30X_SwitchOffPWM(this);
  
  /* Shift of N bits to divide for the NB_ CONVERSIONS = 16= 2^N with N = 4 */
  pDVars_str->wPhaseAOffset >>=4; 
  pDVars_str->wPhaseBOffset >>=4; 
  pDVars_str->wPhaseCOffset >>=4; 

  /* Change back function to be executed in ADCx_ISR */ 
  ((_CPWMC) this)->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_GetPhaseCurrents;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect1 = &R3_1_F30X_SetADCSampPointSect1;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect2 = &R3_1_F30X_SetADCSampPointSect2;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect3 = &R3_1_F30X_SetADCSampPointSect3;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect4 = &R3_1_F30X_SetADCSampPointSect4;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect5 = &R3_1_F30X_SetADCSampPointSect5;
  ((_CPWMC) this)->Methods_str.pPWMC_SetADCSampPointSect6 = &R3_1_F30X_SetADCSampPointSect6;
  
  /* To program the first samplig at the next switch on PWM */
  pDVars_str->wADC1_JSQR = pDVars_str->wADC_JSQR_phAB;

  /* It over write TIMx CCRy wrongly written by FOC during calibration so as to 
     force 50% duty cycle on the three inverer legs */
  /* Disable TIMx preload */  
  TIMx->CCMR1 &= 0xF7F7u;
  TIMx->CCMR2 &= 0xF7F7u;
  TIMx->CCR1 = pDVars_str->Half_PWMPeriod;
  TIMx->CCR2 = pDVars_str->Half_PWMPeriod;
  TIMx->CCR3 = pDVars_str->Half_PWMPeriod;
  
  /* Enable TIMx preload */
  TIMx->CCMR1 |= 0x0808u;
  TIMx->CCMR2 |= 0x0808u;
  
  /* It re-enable drive of TIMx CHy and CHyN by TIMx CHyRef*/
  TIMx->CCER |= TIMxCCER_MASK_CH123;
  
  pDVars_str->BrakeActionLock = FALSE;
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  It computes and return latest converted motor phase currents motor
* @param  this: related object of class CPWMC
* @retval Ia and Ib current in Curr_Components format
*/
static void R3_1_F30X_GetPhaseCurrents(CPWMC this,Curr_Components* pStator_Currents)
{
  uint8_t bSector;
  int32_t wAux;
  uint16_t hReg1,hReg2;
  pDVars_t pDVars_str = &(((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->DVars_str);
  
  /* Clear the flag to indicate the start of FOC algorithm*/
  DCLASS_PARAMS->TIMx->SR = (uint16_t)(~TIM_FLAG_Update);
  
  hReg1 = (uint16_t)(pDVars_str->ADCx_1->JDR1); 
  hReg2 = (uint16_t)(pDVars_str->ADCx_1->JDR2);
  
  bSector = (uint8_t)(((_CPWMC)this)->Vars_str.hSector);
  
  switch (bSector)
  {
  case SECTOR_4:
  case SECTOR_5:
    {
      /* Current on Phase C is not accessible     */
      
      /* Ia = PhaseAOffset - ADC converted value) */
      if(bSector == SECTOR_4)
      {
        wAux = (int32_t)(pDVars_str->wPhaseAOffset)-(int32_t)(hReg2);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseAOffset)-(int32_t)(hReg1);
      }
      
      /* Saturation of Ia */
      if (wAux < S16_MIN)
      {
        pStator_Currents->qI_Component1= S16_MIN;
      }
      else  if (wAux > S16_MAX)
      {
        pStator_Currents->qI_Component1= S16_MAX;
      }
      else
      {
        pStator_Currents->qI_Component1= (int16_t)wAux;
      }
      
      /* Ib = PhaseBOffset - ADC converted value) */
      if(bSector == SECTOR_4)
      {
        wAux = (int32_t)(pDVars_str->wPhaseBOffset)-(int32_t)(hReg1);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseBOffset)-(int32_t)(hReg2);
      }
      
      /* Saturation of Ib */
      if (wAux < S16_MIN)
      {
        pStator_Currents->qI_Component2= S16_MIN;
      }
      else  if (wAux > S16_MAX)
      {
        pStator_Currents->qI_Component2= S16_MAX;
      }
      else
      {
        pStator_Currents->qI_Component2= (int16_t)wAux;
      }
    }
    break;
    
  case SECTOR_6:
  case SECTOR_1:
    {
      /* Current on Phase A is not accessible     */
      
      /* Ib = PhaseBOffset - ADC converted value) */
      if(bSector == SECTOR_6)
      {
        wAux = (int32_t)(pDVars_str->wPhaseBOffset)-(int32_t)(hReg2);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseBOffset)-(int32_t)(hReg1);
      }
      
      /* Saturation of Ib */
      if (wAux < S16_MIN)
      {
        pStator_Currents->qI_Component2= S16_MIN;
      }
      else  if (wAux > S16_MAX)
      {
        pStator_Currents->qI_Component2= S16_MAX;
      }
      else
      {
        pStator_Currents->qI_Component2= (int16_t)wAux;
      }
      
      /* Ic = PhaseCOffset - ADC converted value) */
      /* Ia = -Ic -Ib */
      if(bSector == SECTOR_6)
      {
        wAux = (int32_t)(pDVars_str->wPhaseCOffset)-(int32_t)(hReg1);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseCOffset)-(int32_t)(hReg2);
      }
      
      wAux = -wAux - (int32_t)pStator_Currents->qI_Component2;
      
      /* Saturation of Ia */
      if (wAux> S16_MAX)
      {
        pStator_Currents->qI_Component1 = S16_MAX;
      }
      else  if (wAux <S16_MIN)
      {
        pStator_Currents->qI_Component1 = S16_MIN;
      }
      else
      {
        pStator_Currents->qI_Component1 = (int16_t)wAux;
      }
    }
    break;
    
  case SECTOR_2:
  case SECTOR_3:
    {
      /* Current on Phase B is not accessible     */
      
      /* Ia = PhaseAOffset - ADC converted value) */
      if(bSector == SECTOR_3)
      {
        wAux = (int32_t)(pDVars_str->wPhaseAOffset)-(int32_t)(hReg2);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseAOffset)-(int32_t)(hReg1);
      }
      
      /* Saturation of Ia */
      if (wAux < S16_MIN)
      {
        pStator_Currents->qI_Component1= S16_MIN;
      }
      else  if (wAux > S16_MAX)
      {
        pStator_Currents->qI_Component1= S16_MAX;
      }
      else
      {
        pStator_Currents->qI_Component1= (int16_t)wAux;
      }
      
      /* Ic = PhaseCOffset - ADC converted value) */
      /* Ib = -Ic -Ia */
      if(bSector == SECTOR_3)
      {
        wAux = (int32_t)(pDVars_str->wPhaseCOffset)-(int32_t)(hReg1);
      }
      else
      {
        wAux = (int32_t)(pDVars_str->wPhaseCOffset)-(int32_t)(hReg2);
      }
      
      wAux = -wAux -  (int32_t)pStator_Currents->qI_Component1;
      
      /* Saturation of Ib */
      if (wAux> S16_MAX)
      {
        pStator_Currents->qI_Component2=S16_MAX;
      }
      else  if (wAux <S16_MIN)
      {
        pStator_Currents->qI_Component2 = S16_MIN;
      }
      else
      {
        pStator_Currents->qI_Component2 = (int16_t)wAux;
      }
    }
    break;
    
  default:
    {
    }
    break;
  }   
}

/**
* @brief  Implementaion of PWMC_GetPhaseCurrents to be performed during 
*         calibration. It sum up injected conversion data into wPhaseAOffset and
*         wPhaseBOffset to compute the offset introduced in the current feedback
*         network. It is requied to proper configure ADC inputs before to enable
*         the offset computation.
* @param  this: related object of class CPWMC
* @retval It always returns {0,0} in Curr_Components format
*/
static void R3_1_F30X_HFCurrentsCalibrationAB(CPWMC this,Curr_Components* pStator_Currents)
{  
  /* Derived class members container */
  pDVars_t pDVars_str = &DCLASS_VARS; 
  
  /* Clear the flag to indicate the start of FOC algorithm*/
  DCLASS_PARAMS->TIMx->SR = (uint16_t)(~TIM_FLAG_Update);
  
  if (pDVars_str->bIndex < NB_CONVERSIONS)
  {
    pDVars_str-> wPhaseAOffset += pDVars_str->ADCx_1->JDR1;
    pDVars_str-> wPhaseBOffset += pDVars_str->ADCx_1->JDR2;
    pDVars_str->bIndex++;
  }
}

/**
* @brief  Implementaion of PWMC_GetPhaseCurrents to be performed during 
*         calibration. It sum up injected conversion data into wPhaseCOffset
*         to compute the offset introduced in the current feedback
*         network. It is requied to proper configure ADC input before to enable
*         the offset computation.
* @param  this: related object of class CPWMC
* @retval It always returns {0,0} in Curr_Components format
*/
static void R3_1_F30X_HFCurrentsCalibrationC(CPWMC this,Curr_Components* pStator_Currents)
{
  /* Derived class members container */
  pDVars_t pDVars_str = &DCLASS_VARS;
  
  /* Clear the flag to indicate the start of FOC algorithm*/
  DCLASS_PARAMS->TIMx->SR = (uint16_t)(~TIM_FLAG_Update);
  
  if (pDVars_str->bIndex < NB_CONVERSIONS)
  {
    pDVars_str-> wPhaseCOffset += pDVars_str->ADCx_1->JDR1;
    pDVars_str->bIndex++;
  }
}

/**
  * @brief  It turns on low sides switches. This function is intended to be 
  *         used for charging boot capacitors of driving section. It has to be 
  *         called each motor start-up when using high voltage drivers
  * @param  this: related object of class CPWMC
  * @retval none
  */
static void R3_1_F30X_TurnOnLowSides(CPWMC this)
{
  pDParams_t pLocalDParams = DCLASS_PARAMS;
  TIM_TypeDef*  TIMx = DCLASS_PARAMS->TIMx;  
  
  /* Clear Update Flag */
  TIM_ClearFlag(TIMx, (uint16_t)(TIM_FLAG_Update));
  
  /*Turn on the three low side switches */
  TIMx->CCR1 = 0u;
  TIMx->CCR2 = 0u;
  TIMx->CCR3 = 0u;
  
  /* Wait until next update */
  while (TIM_GetFlagStatus(TIMx,TIM_FLAG_Update)==RESET)
  {}
  
  /* Main PWM Output Enable */
  TIMx->BDTR |= TIM_BDTR_MOE;
  
  if ((pLocalDParams->LowSideOutputs)== ES_GPIO)
  {
    GPIO_WriteBit(pLocalDParams->hCh1NPort, pLocalDParams->hCh1NPin, Bit_SET);
    GPIO_WriteBit(pLocalDParams->hCh2NPort, pLocalDParams->hCh2NPin, Bit_SET);
    GPIO_WriteBit(pLocalDParams->hCh3NPort, pLocalDParams->hCh3NPin, Bit_SET);
  }
  return; 
}


/**
* @brief  It enables PWM generation on the proper Timer peripheral acting on MOE
*         bit
* @param  this: related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_SwitchOnPWM(CPWMC this)
{  
  TIM_TypeDef* TIMx = DCLASS_PARAMS->TIMx;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  pVars_t pVars_str = &CLASS_VARS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  
  /* wait for a new PWM period */
  TIM_ClearFlag(TIMx, (uint16_t)(TIM_FLAG_Update));
  while (TIM_GetFlagStatus(TIMx,TIM_FLAG_Update) == RESET)
  {}
  /* Clear Update Flag */
  TIM_ClearFlag(TIMx, (uint16_t)(TIM_FLAG_Update));
  
  /* Set all duty to 50% */
  if (pVars_str->RLDetectionMode == TRUE)
  {
    TIMx->CCR1 = 1u;
    pDVars_str->ADCx_1->JSQR = pDVars_str->wADC1_JSQR;
  }
  else
  {
    TIMx->CCR1 = (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1;
  }
  TIMx->CCR2 = (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1;
  TIMx->CCR3 = (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1;
  TIMx->CCR4 = (uint32_t)(pDVars_str->Half_PWMPeriod) - 5u;
  
  while (TIM_GetFlagStatus(TIMx,TIM_FLAG_Update) == RESET)
  {}
  
  /* Main PWM Output Enable */
  TIMx->BDTR |= TIM_OSSIState_Enable; 
  TIMx->BDTR |= TIM_BDTR_MOE;
  
  if ((pDParams_str->LowSideOutputs)== ES_GPIO)
  {
    if ((TIMx->CCER & TIMxCCER_MASK_CH123) != 0u)
    {
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_SET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_SET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_SET);
    }
    else
    {
      /* It is executed during calibration phase the EN signal shall stay off */
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
    }
  }
  pDVars_str->ADCx_1->JSQR = pDVars_str->wADC1_JSQR;

  return; 
}


/**
* @brief  It disables PWM generation on the proper Timer peripheral acting on 
*         MOE bit
* @param  this: related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_SwitchOffPWM(CPWMC this)
{ 
  pDParams_t pDParams_str = DCLASS_PARAMS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  TIM_TypeDef* TIMx = pDParams_str->TIMx;
  
  /* Main PWM Output Disable */
  if (DCLASS_VARS.BrakeActionLock == TRUE)
  {
  }
  else
  {
    TIMx->BDTR &= ~((uint32_t)(TIM_OSSIState_Enable));
    
    if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
    }
  }
  TIMx->BDTR &= (uint32_t)~TIM_BDTR_MOE;
  
  /* ADC_ITConfig(ADCx, ADC_IT_JEOS, DISABLE);*/
  pDVars_str->ADCx_1->IER &= (~(uint32_t)ADC_IT_JEOS);
  
  /* Flushing JSQR queue of context by setting JADSTP = 1 (JQM)=1 */
  pDVars_str->ADCx_1->CR |= ADC_CR_JADSTP;
  pDVars_str->ADCx_1->JSQR = R3_1_F30X_ADC_InjectedChannelConfig(pDVars_str->ADCx_1, 0u, 1u, 0u, 2u, ADC_ExternalTrigInjecEventEdge_RisingEdge, pDVars_str->ADC_ExternalTriggerInjected);
  pDVars_str->ADCx_1->CR |= ADC_CR_JADSTART;
  
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Disable);
  /* Set CC4 as PWM mode 2 (default) */
  TIMx->CCMR2 &= CCMR2_CH4_DISABLE;
  TIMx->CCMR2 |= CCMR2_CH4_PWM2;
  /* Imposing of a change of state from 1 to 0 logic state*/
  TIMx->CCR4 = 0xFFFFu;
  TIMx->CCR4 = 0x0u;
	
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);
  
  while (ADC_GetFlagStatus(pDVars_str->ADCx_1,ADC_FLAG_JEOS)==RESET)
  {}
  /* ADCx_1 Injected conversions end interrupt enabling */
  ADC_ClearFlag(pDVars_str->ADCx_1, ADC_FLAG_JEOS);
  ADC_ITConfig(pDVars_str->ADCx_1, ADC_IT_JEOS, ENABLE);  
  return; 
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  It stores into 'this' object variables the voltage present on Ia and 
*         Ib current feedback analog channels when no current is flowin into the
*         motor
* @param  this: related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_WriteTIMRegisters(CPWMC this)
{
  uint32_t wCCR4Aux;
  uint16_t hAux;
  TIM_TypeDef*  TIMx = DCLASS_PARAMS->TIMx;
  pDVars_t pDVars_str = &DCLASS_VARS;
      
  TIMx->CCR1 = ((_CPWMC) this)->Vars_str.hCntPhA;
  TIMx->CCR2 = ((_CPWMC) this)->Vars_str.hCntPhB;
  TIMx->CCR3 = ((_CPWMC) this)->Vars_str.hCntPhC;
  wCCR4Aux = (uint16_t)(TIMx->CCR4);
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Disable);
  TIMx->CCR4 = 0xFFFFu;
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);
  
  TIMx->CCR4 = wCCR4Aux;
  pDVars_str->ADCx_1->JSQR = pDVars_str->wADC1_JSQR;
    
  /* Limit for update event */
  /* Check the status flag. If an update event has occurred before to set new
  values of regs the FOC rate is too high */
  if (TIMx->SR & TIM_FLAG_Update)
  {
    hAux = MC_FOC_DURATION;
  }
  else
  {
    hAux = MC_NO_ERROR;
  }
  if (((_CPWMC) this)->Vars_str.SWerror == 1u)
  {
    hAux = MC_FOC_DURATION;
    ((_CPWMC) this)->Vars_str.SWerror = 0u;
  }
  return hAux;
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling during calibration.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointCalibration(CPWMC this)
{
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  
  TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
  
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 1.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect1(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;


  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */
    
     pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB;
  }
  else
  { /* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */
    
    uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phBC;
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    {  /* Crossing Point Searching */
      hDeltaDuty = (u16)(pBaseVars->hCntPhA - pBaseVars->hCntPhB);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhA)*2u)
      {
        hCntSmp = pBaseVars->hCntPhA - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhA + pDParams_str->hTafter; /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        { 
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;
          hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }   
    /* Set JSQR register */
    pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;
    
    /* Set TIMx_CH4 value */
    TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 2.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect2(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;

  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */
    
    pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB;
  }
  else
  {
 /* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */    
    
    uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phAC;
    /* Searching of sampling point to avoid noise inducted by a commutation of other phase's switch */
    
    /* Check if sampling of detected current phases in the middle of PWM is possible.
       It depends on if the complementary duty cycle of the phase having a minimum complementary duty cycle 
       is greater than the Tafter time because in this case its commutation is sufficiently distant from the 
       middle of PWM and it doesn't induct any noise to Shunt voltages of the others phases */
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    { /* Crossing Point Searching */ 
      hDeltaDuty = (u16)(pBaseVars->hCntPhB - pBaseVars->hCntPhA);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhB)*2u)
      {
        hCntSmp = pBaseVars->hCntPhB - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC  */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhB + pDParams_str->hTafter;  /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        {
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;

          hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }
    
    /* Set JSQR register */
    pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;
    
    /* Set TIMx_CH4 value */
    TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 3.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect3(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;

  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */

    pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB;
  }
  else
  {/* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */ 
    
    uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phCA;
    
    /* Searching of sampling point to avoid noise inducted by a commutation of other phase's switch */
    
    /* Check if sampling of detected current phases in the middle of PWM is possible.
    It depends on if the complementary duty cycle of the phase having a minimum complementary duty cycle 
    is greater than the Tafter time because in this case its commutation is sufficiently distant from the 
    middle of PWM and it doesn't induct any noise to Shunt voltages of the others phases */
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    {/* Crossing Point Searching */
      hDeltaDuty = (u16)(pBaseVars->hCntPhB - pBaseVars->hCntPhC);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhB)*2u) 
      {
        hCntSmp = pBaseVars->hCntPhB - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC  */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhB + pDParams_str->hTafter; /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        {
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;
          
         hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }
    /* Set JSQR register */
    pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;    

    /* Set TIMx_CH4 value */
    TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 4.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect4(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  
  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */
    
    pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB;
  }
  else
  {
    /* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */ 
    
    uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phBA;
    
    /* Searching of sampling point to avoid noise inducted by a commutation of other phase's switch */
    
    /* Check if sampling of detected current phases in the middle of PWM is possible.
       It depends on if the complementary duty cycle of the phase having a minimum complementary duty cycle 
       is greater than the Tafter time because in this case its commutation is sufficiently distant from the 
       middle of PWM and it doesn't induct any noise to Shunt voltages of the others phases */    
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    {/* Crossing Point Searching */
      hDeltaDuty = (u16)(pBaseVars->hCntPhC - pBaseVars->hCntPhB);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhC)*2u)
      {
        hCntSmp = pBaseVars->hCntPhC - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC  */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhC + pDParams_str->hTafter; /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        {
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;
          
          hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }
    
    /* Set JSQR register */
    pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;
    
    /* Set TIMx_CH4 value */
    TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 5.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect5(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;

  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */

    pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB; 
  }
  else
  {
    /* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */ 

     uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phAB;
    /* Searching of sampling point to avoid noise inducted by a commutation of other phase's switch */
    
    /* Check if sampling of detected current phases in the middle of PWM is possible.
       It depends on if the complementary duty cycle of the phase having a minimum complementary duty cycle 
       is greater than the Tafter time because in this case its commutation is sufficiently distant from the 
       middle of PWM and it doesn't induct any noise to Shunt voltages of the others phases */      
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    { /* Crossing Point Searching */
      hDeltaDuty = (u16)(pBaseVars->hCntPhC - pBaseVars->hCntPhA);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhC)*2u) 
      {
        hCntSmp = pBaseVars->hCntPhC - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC  */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhC + pDParams_str->hTafter; /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        {
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;         
          
          hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }
    
  /* Set JSQR register */
  pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;    
    
  /* Set TIMx_CH4 value */
  TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  Configure the ADC for the current sampling related to sector 6.
*         It means set the sampling point via TIMx_Ch4 value and polarity
*         ADC sequence length and channels.
*         And call the WriteTIMRegisters method.
* @param  this related object of class CPWMC
* @retval none
*/
static uint16_t R3_1_F30X_SetADCSampPointSect6(CPWMC this)
{
  uint16_t hCntSmp, hDeltaDuty;
  Vars_t *pBaseVars = &((_CPWMC) this)->Vars_str;
  pDParams_t pDParams_str =  ((_DCR3_1_F30X_PWMC)(((_CPWMC) this)->DerivedClass))->pDParams_str;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;

  /* Check if sampling AB in the middle of PWM is possible */
  if (((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhB) > pDParams_str->hTafter) &&
      ((uint16_t)(PWM_PERIOD-pBaseVars->hCntPhC) > pDParams_str->hTafter))
  {
    TIMx->CCR4 = (uint32_t)(PWM_PERIOD) - 1u;
    ((_CPWMC)this)->Vars_str.hSector = SECTOR_5; /* Dummy just for the GetPhaseCurrent */

    pLocalVars_Str->wADC1_JSQR = pLocalVars_Str->wADC_JSQR_phAB;
  }
  else
  {
    /* In this case it is necessary to convert phases with Maximum and variable complementary duty cycle.*/
    
    /* ADC Injected sequence configuration. The stator phase with minimum value of complementary
    duty cycle is set as first. In every sector there is always one phase with maximum complementary duty,
    one with minimum complementary duty and one with variable complementary duty. In this case, phases 
    with variable complementary duty and with maximum duty are converted and the first will be always 
    the phase with variable complementary duty cycle */ 

    uint32_t wADC_JSQR_2phase = pLocalVars_Str->wADC_JSQR_phCB;
    /* Searching of sampling point to avoid noise inducted by a commutation of other phase's switch */
    
    /* Check if sampling of detected current phases in the middle of PWM is possible.
       It depends on if the complementary duty cycle of the phase having a minimum complementary duty cycle 
       is greater than the Tafter time because in this case its commutation is sufficiently distant from the 
       middle of PWM and it doesn't induct any noise to Shunt voltages of the others phases */    
    
    if ((u16)(PWM_PERIOD-pBaseVars->hCntPhA) > pDParams_str->hTafter)
    {
      hCntSmp = PWM_PERIOD - 1u;
    }
    else
    {/* Crossing Point Searching */
      hDeltaDuty = (u16)(pBaseVars->hCntPhA - pBaseVars->hCntPhC);
      
      /* Definition of crossing point */
      if (hDeltaDuty > (u16)(PWM_PERIOD-pBaseVars->hCntPhA)*2u) 
      {
        hCntSmp = pBaseVars->hCntPhA - pDParams_str->hTbefore; /* hTbefore = 2*Ts + Tc, where Ts = Sampling time of ADC, Tc = Conversion Time of ADC  */
      }
      else
      {
        hCntSmp = pBaseVars->hCntPhA + pDParams_str->hTafter; /* hTafter = DT + max(Trise, Tnoise) */
        
        if (hCntSmp >= PWM_PERIOD)
        {
          /* It must be changed the trigger direction from positive to negative 
             to sample after middle of PWM*/
          wADC_JSQR_2phase &= JSQR_EDGE_CLEAR_Mask;
          wADC_JSQR_2phase |= (uint32_t )ADC_ExternalTrigInjecEventEdge_FallingEdge;
          
          hCntSmp = (2u * PWM_PERIOD) - hCntSmp - 1u;
        }
      }
    }

    /* Set JSQR register */
    pLocalVars_Str->wADC1_JSQR = wADC_JSQR_2phase;    
    
    /* Set TIMx_CH4 value */
    TIMx->CCR4 = hCntSmp;
  }
  return R3_1_F30X_WriteTIMRegisters(this);
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  It contains the TIMx Update event interrupt
* @param  this: related object of class CPWMC
* @retval none
*/
static void *R3_1_F30X_IRQHandler(void *this, unsigned char flag)
{
  pVars_t pVars_str = &CLASS_VARS;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  if (flag == 2u)
  {
    if (DCLASS_VARS.BrakeActionLock == FALSE)
    {
      if ((pDParams_str->LowSideOutputs)== ES_GPIO)
      {
        GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_RESET);
        GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
        GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
      }
    }
    DCLASS_VARS.OverCurrentFlag = TRUE;
  }
  else if (flag == 3u)
  {
    DCLASS_PARAMS->TIMx->BDTR |= TIM_OSSIState_Enable;
    DCLASS_VARS.OverVoltageFlag = TRUE;
    DCLASS_VARS.BrakeActionLock = TRUE;
  }
  else
  {
  }
  return &(pVars_str->bMotor);
}

/**
* @brief  Execute a regular conversion using ADCx. 
*         The function is not re-entrant (can't executed twice at the same time)
* @param  this related object of class CPWMC
* @retval It returns converted value or oxFFFF for conversion error
*/
static uint16_t R3_1_F30X_ExecRegularConv(CPWMC this, uint8_t bChannel)
{
  pDVars_t pDVars_str = &DCLASS_VARS;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  ADC_TypeDef* ADCx = pDParams_str->regconvADCx;
  
  ADCx->SQR1 = (uint32_t)(bChannel) << 6;
  
  ADCx->DR;
  ADCx->CR = ADC_CR_ADSTART;
  
  /* Wait until end of regular conversion */
  while ((ADCx->ISR & ADC_ISR_EOC) == 0u)
  {
  }
  
  pDVars_str->hRegConv = (uint16_t)(ADCx->DR);
  return (pDVars_str->hRegConv);
}

/**
* @brief  It sets the specified sampling time for the specified ADC channel
*         on ADCx. It must be called once for each channel utilized by user
* @param  ADC channel, sampling time
* @retval none
*/
static void R3_1_F30X_ADC_SetSamplingTime(CPWMC this, ADConv_t ADConv_struct)
{ 
  pDParams_t pDParams_str = DCLASS_PARAMS;
  uint32_t tmpreg2 = 0u;
  uint8_t ADC_Channel = ADConv_struct.Channel;
  uint8_t ADC_SampleTime = ADConv_struct.SamplTime;
  
  /* Channel sampling configuration */
  /* if ADC_Channel_10 ... ADC_Channel_18 is selected */
  if (ADC_Channel > ADC_Channel_9)
  {
    uint32_t wAux,wAux2;
    /* Get the old register value */
    /* Calculate the mask to clear */
    wAux = ADC_SMPR2_SMP10;
    wAux2 = 3u * ((uint32_t)(ADC_Channel) - 10u);
    tmpreg2 =  wAux << wAux2;
    /* Clear the old channel sample time */
    pDParams_str->regconvADCx->SMPR2 &= ~tmpreg2;
    /* Calculate the mask to set */
    wAux = (uint32_t)(ADC_SampleTime);
    pDParams_str->regconvADCx->SMPR2 |=  wAux << wAux2;
    
  }
  else /* ADC_Channel include in ADC_Channel_[0..9] */
  {
    uint32_t wAux,wAux2;
    /* Get the old register value */
    /* Calculate the mask to clear */
    wAux = ADC_SMPR1_SMP1;
    wAux2 = 3u * ((uint32_t)(ADC_Channel) - 1u);
    tmpreg2 =  wAux << wAux2;
    /* Clear the old channel sample time */
    pDParams_str->regconvADCx->SMPR1 &= ~tmpreg2;
    /* Calculate the mask to set */
    wAux = (uint32_t)(ADC_SampleTime);
    wAux2 = 3u * ((uint32_t)(ADC_Channel));
    pDParams_str->regconvADCx->SMPR1 |= wAux << wAux2;
  }
}
/**
* @brief  It is used to check if an overcurrent occurred since last call.
* @param  this related object of class CPWMC
* @retval uint16_t It returns MC_BREAK_IN whether an overcurrent has been 
*                  detected since last method call, MC_NO_FAULTS otherwise.
*/
static uint16_t R3_1_F30X_IsOverCurrentOccurred(CPWMC this)
{
  pDVars_t pDVars_str = &DCLASS_VARS;
  uint16_t retVal = MC_NO_FAULTS;
  
  if (pDVars_str->OverVoltageFlag == TRUE)
  {
    retVal = MC_OVER_VOLT;
    pDVars_str->OverVoltageFlag = FALSE;
  }
  
  if (pDVars_str->OverCurrentFlag == TRUE )
  {
    retVal |= MC_BREAK_IN;
    pDVars_str->OverCurrentFlag = FALSE;
  }
  
  return retVal;
}

/**
* @brief  It is used to configure the analog output used for protection 
*         thresholds.
* @param  DAC_Channel: the selected DAC channel. 
*          This parameter can be:
*            @arg DAC_Channel_1: DAC Channel1 selected
*            @arg DAC_Channel_2: DAC Channel2 selected
* @param  hDACVref Value of DAC reference expressed as 16bit unsigned integer.
*         Ex. 0 = 0V 65536 = VDD_DAC.
* @retval none
*/

/**
  * @brief  It is an internal function used to compute the GPIO Source 
  *         value starting from GPIO pin value. The GPIO Source value 
  *         is used for AF remapping.
  * @param  GPIO_Pin Pin value to be converted.
  * @retval uint16_t The GPIO pin source value converted.
  */
static uint16_t F30X_GPIOPin2Source(uint16_t GPIO_Pin)
{
  uint16_t GPIO_Sourcex = 0u;
  
  while (GPIO_Pin != 0x01u)
  {
    GPIO_Pin = GPIO_Pin >> 1u;
    GPIO_Sourcex++;
  }
  return GPIO_Sourcex;
}

static uint32_t R3_1_F30X_ADC_InjectedChannelConfig(ADC_TypeDef* ADCx, uint8_t ADC_Channel, uint8_t Rank, uint8_t ADC_SampleTime, uint8_t SequencerLength, uint16_t ADC_ExternalTriggerInjectedPolarity, uint16_t ADC_ExternalTriggerInjected)
{
  uint32_t tmpreg1 = 0u, tmpreg2 = 0u, tmpregA = 0u;
  uint32_t wAux,wAux2;
  
  /*  ADC_InjectedSequencerLengthConfig(ADCx,1); */
  tmpregA = ADCx->JSQR;
  /* Clear the old injected sequnence lenght JL bits */
  tmpregA &= ~(uint32_t)ADC_JSQR_JL;
  /* Set the injected sequnence lenght JL bits */
  tmpregA |= ((uint32_t)(SequencerLength) - 1u); /* first value is sequencer lenght */
  
  /* Disable the selected ADC conversion on external event */
  tmpregA &= ~ADC_JSQR_JEXTEN;
  tmpregA |= ADC_ExternalTriggerInjectedPolarity; 
 
  /* Disable the selected ADC conversion on external event */
  tmpregA &= ~ADC_JSQR_JEXTSEL;
  tmpregA |= ADC_ExternalTriggerInjected;

  /* Channel sampling configuration */
  /* if ADC_Channel_10 ... ADC_Channel_18 is selected */
  if (ADC_Channel > ADC_Channel_9)
  {
    /* Get the old register value */
    tmpreg1 = ADCx->SMPR2;
    /* Calculate the mask to clear */
    wAux = ADC_SMPR2_SMP10;
    wAux2 = 3u * ((uint32_t)(ADC_Channel) - 10u);
    tmpreg2 = wAux << wAux2;
    /* Clear the old channel sample time */
    tmpreg1 &= ~tmpreg2;
    /* Calculate the mask to set */
    wAux = (uint32_t)(ADC_SampleTime);
    tmpreg2 = wAux << wAux2;
    /* Set the new channel sample time */
    tmpreg1 |= tmpreg2;
    /* Store the new register value */
    ADCx->SMPR2 = tmpreg1;
  }
  else if (ADC_Channel != 0u)
  {
    /* Get the old register value */
    tmpreg1 = ADCx->SMPR1;
    /* Calculate the mask to clear */
    wAux = ADC_SMPR1_SMP0;
    wAux2 = 3u * (uint32_t)(ADC_Channel);
    tmpreg2 =  wAux << wAux2;
    /* Clear the old channel sample time */
    tmpreg1 &= ~tmpreg2;
    /* Calculate the mask to set */
    wAux = (uint32_t)ADC_SampleTime;
    wAux2 = 3u * (uint32_t)(ADC_Channel);
    tmpreg2 =  wAux << wAux2;
    /* Set the new channel sample time */
    tmpreg1 |= tmpreg2;
    /* Store the new register value */
    ADCx->SMPR1 = tmpreg1;
  }
  else
  {
  }

  /* Rank configuration */
  /* Get the old register value */
  tmpreg1 = tmpregA;
  /* Calculate the mask to clear */
  wAux = ADC_JSQR_JSQ1;
  wAux2 = 6u * ((uint32_t)(Rank) - 1u);
  tmpreg2 = wAux << wAux2;
  /* Clear the old SQx bits for the selected rank */
  tmpreg1 &= ~tmpreg2;
  /* Calculate the mask to set */
  wAux = ADC_Channel;
  wAux2 = 6u * (uint32_t)(Rank) + 2u;
  tmpreg2 = wAux << wAux2;
  /* Set the SQx bits for the selected rank */
  tmpreg1 |= tmpreg2;
  /* Store the new register value */
  
  return (tmpreg1);
}


/**
  * @brief  Fills each TIM_BDTRInitStruct member with its default value.
  * @param  TIM_BDTRInitStruct: pointer to a TIM_BDTRInitTypeDef structure which
  *         will be initialized.
  * @retval None
  */
static void TIM_BDTRStructInit_MC(TIM_BDTRInitTypeDef_MC* TIM_BDTRInitStruct)
{
  /* Set the default configuration */
  TIM_BDTRInitStruct->TIM_OSSRState = TIM_OSSRState_Disable;
  TIM_BDTRInitStruct->TIM_OSSIState = TIM_OSSIState_Disable;
  TIM_BDTRInitStruct->TIM_LOCKLevel = TIM_LOCKLevel_OFF;
  TIM_BDTRInitStruct->TIM_DeadTime = 0x00u;
  TIM_BDTRInitStruct->TIM_Break = TIM_Break_Disable;
  TIM_BDTRInitStruct->TIM_BreakPolarity = TIM_BreakPolarity_Low;
  TIM_BDTRInitStruct->TIM_AutomaticOutput = TIM_AutomaticOutput_Disable;
  TIM_BDTRInitStruct->TIM_Break2 = TIM_Break2_Disable;
  TIM_BDTRInitStruct->TIM_Break2Polarity = TIM_Break2Polarity_Low;
  TIM_BDTRInitStruct->TIM_Break1Filter = 0x00u;
  TIM_BDTRInitStruct->TIM_Break2Filter = 0x00u;
}

/**
  * @brief  Configures the Break feature, dead time, Lock level, OSSI/OSSR State
  *         and the AOE(automatic output enable).
  * @param  TIMx: where x can be  1, 8, 15, 16 or 17 to select the TIM 
  * @param  TIM_BDTRInitStruct: pointer to a TIM_BDTRInitTypeDef_MC structure that
  *         contains the BDTR Register configuration  information for the TIM peripheral.
  * @retval None
  */
static void TIM_BDTRConfig_MC(TIM_TypeDef* TIMx, TIM_BDTRInitTypeDef_MC *TIM_BDTRInitStruct)
{
  /* Check the parameters */
#ifndef MISRA_C_2004_BUILD
  assert_param(IS_TIM_LIST6_PERIPH(TIMx));
  assert_param(IS_TIM_OSSR_STATE(TIM_BDTRInitStruct->TIM_OSSRState));
  assert_param(IS_TIM_OSSI_STATE(TIM_BDTRInitStruct->TIM_OSSIState));
  assert_param(IS_TIM_LOCK_LEVEL(TIM_BDTRInitStruct->TIM_LOCKLevel));
  assert_param(IS_TIM_BREAK_STATE(TIM_BDTRInitStruct->TIM_Break));
  assert_param(IS_TIM_BREAK_POLARITY(TIM_BDTRInitStruct->TIM_BreakPolarity));
  assert_param(IS_TIM_AUTOMATIC_OUTPUT_STATE(TIM_BDTRInitStruct->TIM_AutomaticOutput));
  assert_param(IS_TIM_BREAK2_STATE(TIM_BDTRInitStruct->TIM_Break2));
  assert_param(IS_TIM_BREAK2_POLARITY(TIM_BDTRInitStruct->TIM_Break2Polarity));
  assert_param(IS_TIM_BREAK1_FILTER(TIM_BDTRInitStruct->TIM_Break1Filter));
  assert_param(IS_TIM_BREAK2_FILTER(TIM_BDTRInitStruct->TIM_Break2Filter));
#endif
  
  /* Set the Lock level, the Break enable Bit and the Polarity, the OSSR State,
     the OSSI State, the dead time value and the Automatic Output Enable Bit */
  TIMx->BDTR = (uint32_t)TIM_BDTRInitStruct->TIM_OSSRState | TIM_BDTRInitStruct->TIM_OSSIState |
    TIM_BDTRInitStruct->TIM_LOCKLevel | TIM_BDTRInitStruct->TIM_DeadTime |
      TIM_BDTRInitStruct->TIM_Break | TIM_BDTRInitStruct->TIM_BreakPolarity |
        TIM_BDTRInitStruct->TIM_AutomaticOutput|TIM_BDTRInitStruct->TIM_Break2 |
          TIM_BDTRInitStruct->TIM_Break2Polarity|((uint32_t)TIM_BDTRInitStruct->TIM_Break1Filter << 16) |
            ((uint32_t)TIM_BDTRInitStruct->TIM_Break2Filter << 20);
}

/**
* @brief  It is used to set the PWM mode for R/L detection.
* @param  this related object of class CPWMC
* @param  hDuty to be applied in u16
* @retval none
*/
static void R3_1_F30X_RLDetectionModeEnable(CPWMC this)
{
  pVars_t pVars_str = &CLASS_VARS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  
  ADC_InjectedInitTypeDef ADC_InjectedInitStruct;
  
  if (pVars_str->RLDetectionMode == FALSE)
  {
    /*  Channel1 configuration */
    TIM_SelectOCxM(TIMx, TIM_Channel_1, TIM_OCMode_PWM1);
    TIM_CCxCmd(TIMx, TIM_Channel_1, TIM_CCx_Enable);
    TIM_CCxNCmd(TIMx, TIM_Channel_1, TIM_CCxN_Disable);
    
    TIM_SetCompare1(TIMx, 0u);
    
    /*  Channel2 configuration */
    if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
    {
      TIM_SelectOCxM(TIMx, TIM_Channel_2, TIM_OCMode_Active);
      TIM_CCxCmd(TIMx, TIM_Channel_2, TIM_CCx_Disable);
      TIM_CCxNCmd(TIMx, TIM_Channel_2, TIM_CCxN_Enable);
    }
    else if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      TIM_SelectOCxM(TIMx, TIM_Channel_2, TIM_OCMode_Inactive);
      TIM_CCxCmd(TIMx, TIM_Channel_2, TIM_CCx_Enable);
      TIM_CCxNCmd(TIMx, TIM_Channel_2, TIM_CCxN_Disable);
    }
    else
    {
    }
    
    /*  Channel3 configuration */
    TIM_SelectOCxM(TIMx, TIM_Channel_3, TIM_OCMode_PWM2);
    TIM_CCxCmd(TIMx, TIM_Channel_3, TIM_CCx_Disable);
    TIM_CCxNCmd(TIMx, TIM_Channel_3, TIM_CCxN_Disable);
    
    
    /* Set Update as TRGO of TIM1 */
    TIM_SelectOutputTrigger(TIMx,TIM_TRGOSource_Update);
		
   /* Configuration of ADC sequence of two Phase B current values for during RL Detection Mode*/     
   ADC_InjectedInitStruct.ADC_ExternalTrigInjecConvEvent = ADC_ExternalTrigInjecConvEvent_0;
   ADC_InjectedInitStruct.ADC_ExternalTrigInjecEventEdge = ADC_ExternalTrigInjecEventEdge_RisingEdge;
   ADC_InjectedInitStruct.ADC_NbrOfInjecChannel =2u;
   
   /*Phase B currents sequence -----------------------------------------------*/
   ADC_InjectedInitStruct.ADC_InjecSequence1 = pDParams_str->bIbChannel;  
   ADC_InjectedInitStruct.ADC_InjecSequence2 = pDParams_str->bIbChannel;
   ADC_InjectedInitStruct.ADC_InjecSequence3 = 0u;
   ADC_InjectedInitStruct.ADC_InjecSequence4 = 0u;
   ADC_InjectedChannelSampleTimeConfig(ADC1,pDParams_str->bIbChannel,pDParams_str->b_IbSamplingTime);
	 
  /* ADC1 Injected discontinuous mode activation. 
   * This is important because permits to convert first current value of ADC1 Injected Sequence at 
   * the first Update-Trigger event and wait until the second Update-Trigger event happens to start 
   * the second ADC1 Injected conversion, then only at the end of the second conversion JEOS Interrupt 
   * event is generated.  
   */
   ADC_InjectedDiscModeCmd(ADC1,ENABLE);
	 
   /*NB: the following istruction doesn't write the JSQR register of ADC1 but 
   * writes into the variable of the pDVars_str structure Class the JSQR value that
   * will be used in the future functions */
   pDVars_str->wADC_JSQR_RL_Detection_phB = SingleADC_InjectedConfig(ADC1, &ADC_InjectedInitStruct);    
  }
  
  ((_CPWMC)this)->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_RLGetPhaseCurrents;
  ((_CPWMC)this)->Methods_str.pPWMC_TurnOnLowSides = &R3_1_F30X_RLTurnOnLowSides;
  ((_CPWMC)this)->Methods_str.pPWMC_SwitchOnPWM = &R3_1_F30X_RLSwitchOnPWM;
  ((_CPWMC)this)->Methods_str.pPWMC_SwitchOffPWM = &R3_1_F30X_RLSwitchOffPWM;
  
  pVars_str->RLDetectionMode = TRUE;
}

/**
* @brief  It is used to disable the PWM mode during RL Detection Mode.
* @param  this related object of class CPWMC
* @retval none
*/
static void R3_1_F30X_RLDetectionModeDisable(CPWMC this)
{
  pVars_t pVars_str = &CLASS_VARS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  
  if (pVars_str->RLDetectionMode == TRUE)
  {
    /* Repetition Counter of TIM1 User value reactivation BEGIN*/
    
    /* The folowing while cycles ensure the identification of the positive counting mode of TIM1 
     * for correct reactivation of Repetition Counter value of TIM1.*/
    
    /* Wait the change of Counter Direction of TIM1 from Up-Direction to Down-Direction*/
    while ((TIM1->CR1 & DIR_MASK) == 0u)
    {
    }
    /* Wait the change of Counter Direction of TIM1 from Down-Direction to Up-Direction.*/
    while ((TIM1->CR1 & DIR_MASK) == DIR_MASK)
    {
    }
    
    /* TIM1 Repetition Counter reactivation to the User Value */
    TIMx->RCR = pDParams_str->bRepetitionCounter;
    /* Repetition Counter of TIM1 User value reactivation END*/
    
    
    /*  Channel1 configuration */
    TIM_SelectOCxM(TIMx, TIM_Channel_1, TIM_OCMode_PWM1);
    TIM_CCxCmd(TIMx, TIM_Channel_1, TIM_CCx_Enable);
    
    if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_1, TIM_CCxN_Enable);
    }
    else if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_1, TIM_CCxN_Disable);
    }
    else
    {
    }
    
    TIM_SetCompare1(TIMx, (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1);
    
    /*  Channel2 configuration */
    TIM_SelectOCxM(TIMx, TIM_Channel_2, TIM_OCMode_PWM1);
    TIM_CCxCmd(TIMx, TIM_Channel_2, TIM_CCx_Enable);
    
    if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_2, TIM_CCxN_Enable);
    }
    else if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_2, TIM_CCxN_Disable);
    }
    else
    {
    }
    
    TIM_SetCompare2(TIMx, (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1);
    
    /*  Channel3 configuration */
    TIM_SelectOCxM(TIMx, TIM_Channel_3, TIM_OCMode_PWM1);
    TIM_CCxCmd(TIMx, TIM_Channel_3, TIM_CCx_Enable);
    
    if ((pDParams_str-> LowSideOutputs)== LS_PWM_TIMER)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_3, TIM_CCxN_Enable);
    }
    else if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      TIM_CCxNCmd(TIMx, TIM_Channel_3, TIM_CCxN_Disable);
    }
    else
    {
    }
    
    TIM_SetCompare3(TIMx, (uint32_t)(pDVars_str->Half_PWMPeriod) >> 1);

    /* Set channel 4 as TRGO (Center TRIGGER - Overflow of TIM1)*/
    TIM_SelectOutputTrigger(TIMx,TIM_TRGOSource_OC4Ref);
		
    /* ADC1 Injected discontinuous mode disable */
    ADC_InjectedDiscModeCmd(ADC1,DISABLE);
       
    ((_CPWMC)this)->Methods_str.pPWMC_GetPhaseCurrents = &R3_1_F30X_GetPhaseCurrents;
    ((_CPWMC)this)->Methods_str.pPWMC_TurnOnLowSides = &R3_1_F30X_TurnOnLowSides;
    ((_CPWMC)this)->Methods_str.pPWMC_SwitchOnPWM = &R3_1_F30X_SwitchOnPWM;
    ((_CPWMC)this)->Methods_str.pPWMC_SwitchOffPWM = &R3_1_F30X_SwitchOffPWM;
    
    pVars_str->RLDetectionMode = FALSE;
  }
}

/**
* @brief  It is used to set the PWM dutycycle during RL Detection Mode.
* @param  this related object of class CPWMC
* @param  hDuty to be applied in u16
* @retval It returns the code error 'MC_FOC_DURATION' if any, 'MC_NO_ERROR' 
*         otherwise. These error codes are defined in MC_type.h
*/
static uint16_t R3_1_F30X_RLDetectionModeSetDuty(CPWMC this, uint16_t hDuty)
{
  Vars_t *pVars_Str = &CLASS_VARS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  DVars_t *pLocalVars_Str = &DCLASS_VARS;
  TIM_TypeDef*  TIMx = pDParams_str->TIMx;
  uint16_t hAux;
  
  uint32_t val = ((uint32_t)(PWM_PERIOD) * (uint32_t)(hDuty)) >> 16;
  pVars_Str->hCntPhA = (uint16_t)(val);
  
  /* JSQR ADC1 resgister writing. The sequence configuration values are set into
   * the R3_1_F30X_RLDetectionModeEnable function*/
  pDVars_str->ADCx_1->JSQR = pLocalVars_Str->wADC_JSQR_RL_Detection_phB;      

  /* TIM1 Channel 1 Duty Cycle configuration. 
   * In RL Detection mode only the Up-side device of Phase A are controlled 
   * while the Phase B up-side device is always open.*/
  TIMx->CCR1 = ((_CPWMC) this)->Vars_str.hCntPhA;
  
  /* Limit for update event */
  /* Check the status flag. If an update event has occurred before to set new
  values of regs the FOC rate is too high */
  if (TIMx->SR & TIM_FLAG_Update)
  {
    hAux = MC_FOC_DURATION;
  }
  else
  {
    hAux = MC_NO_ERROR;
  }
  if (((_CPWMC) this)->Vars_str.SWerror == 1u)
  {
    hAux = MC_FOC_DURATION;
    ((_CPWMC) this)->Vars_str.SWerror = 0u;
	  }
  return hAux;
}

#if defined (CCMRAM)
#if defined (__ICCARM__)
#pragma location = ".ccmram"
#elif defined (__CC_ARM)
__attribute__((section ("ccmram")))
#endif
#endif
/**
* @brief  It computes and return latest converted motor phase currents motor
*         during RL detection phase
* @param  this: related object of class CR3F30X_PWMC
* @retval Ia and Ib current in Curr_Components format
*/
static void R3_1_F30X_RLGetPhaseCurrents(CPWMC this,Curr_Components* pStator_Currents)
{
  pDVars_t pDVars_str = &DCLASS_VARS;
  int32_t wAux;
  int16_t hCurrA = 0, hCurrB = 0;
  
  /* Clear the flag to indicate the start of FOC algorithm*/
  DCLASS_PARAMS->TIMx->SR = (uint16_t)(~TIM_FLAG_Update);
  
  wAux = (int32_t)(pDVars_str->wPhaseBOffset);
  wAux -= (int32_t)(pDVars_str->ADCx_1->JDR1);
  
  /* Check saturation */
  if (wAux > S16_MIN)
  {
    if (wAux < S16_MAX)
    {
    }
    else
    {
      wAux = S16_MAX;
    }
  }
  else
  {
    wAux = S16_MIN;
  }
  /* First value read of Phase B*/
  hCurrA = (int16_t)(wAux);                     
  
  wAux = (int32_t)(pDVars_str->wPhaseBOffset);
  wAux -= (int32_t)(pDVars_str->ADCx_1->JDR2);
  
  /* Check saturation */
  if (wAux > S16_MIN)
  {
    if (wAux < S16_MAX)
    {
    }
    else
    {
      wAux = S16_MAX;
    }
  }
  else
  {
    wAux = S16_MIN;
  }
  /* Second value read of Phase B*/  
  hCurrB = (int16_t)(wAux);                   

  
  pStator_Currents->qI_Component1 = hCurrA;
  pStator_Currents->qI_Component2 = hCurrB;
}

/**
  * @brief  It turns on low sides switches. This function is intended to be 
  *         used for charging boot capacitors of driving section. It has to be 
  *         called each motor start-up when using high voltage drivers.
  *         This function is specific for RL detection phase.
  * @param  this: related object of class CR3F30X_PWMC
  * @retval none
  */
static void R3_1_F30X_RLTurnOnLowSides(CPWMC this)
{  
  pDParams_t pDParams_str = DCLASS_PARAMS;
  TIM_TypeDef* TIMx = DCLASS_PARAMS->TIMx;
  
  /*Turn on the phase A low side switch */
  TIMx->CCR1 = 0u;

  /* Clear Update Flag */
  TIM_ClearFlag(TIMx, (uint16_t)(TIM_FLAG_Update));
  
  /* Wait until next update */
  while (TIM_GetFlagStatus(TIMx,TIM_FLAG_Update)==RESET)
  {}
  
  /* Main PWM Output Enable */
  TIMx->BDTR |= TIM_BDTR_MOE;
  
  if ((pDParams_str->LowSideOutputs)== ES_GPIO)
  {
    GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_SET);
    GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
    GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
  }
  return; 
}


/**
* @brief  It enables PWM generation on the proper Timer peripheral
*         This function is specific for RL detection phase.
* @param  this: related object of class CR3F30X_PWMC
* @retval none
*/
static void R3_1_F30X_RLSwitchOnPWM(CPWMC this)
{
  TIM_TypeDef* TIMx = DCLASS_PARAMS->TIMx;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  
  /* The folowing while cycles ensure the identification of the nergative counting mode of TIM1 
   * for correct modification of Repetition Counter value of TIM1.*/
  
  /* Wait the change of Counter Direction of TIM1 from Down-Direction to Up-Direction*/
  
  while ((TIM1->CR1 & DIR_MASK) ==DIR_MASK)
  {
  }
  /* Wait the change of Counter Direction of TIM1 from Up-Direction to Down-Direction*/
  while ((TIM1->CR1 & DIR_MASK) ==0u)
  {
  }
  /* Set Repetition counter to zero */
  TIMx->RCR = 0u;
  
  
  TIMx->CCR1 = 1u;
    
  /* JSQR ADC1 resgister writing. The sequence configuration values are set into
   * the R3_1_F30X_RLDetectionModeEnable function*/
  pDVars_str->ADCx_1->JSQR = pDVars_str->wADC_JSQR_RL_Detection_phB;
  
  TIMx->SR = (uint16_t)(~TIM_FLAG_Update); /* Clear flag to wait next update */
  
  while (TIM_GetFlagStatus(TIMx,TIM_FLAG_Update) == RESET)
  {}
  
  /* Main PWM Output Enable */
  TIMx->BDTR |= TIM_OSSIState_Enable; 
  TIMx->BDTR |= TIM_BDTR_MOE;
  
  if ((pDParams_str->LowSideOutputs)== ES_GPIO)
  {
    if ((TIMx->CCER & TIMxCCER_MASK_CH123) != 0u)
    {
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_SET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_SET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
    }
    else
    {
      /* It is executed during calibration phase the EN signal shall stay off */
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
    }
  }
  return; 
}


/**
* @brief  It disables PWM generation on the proper Timer peripheral acting on 
*         MOE bit
*         This function is specific for RL detection phase.
* @param  this: related object of class CR3F30X_PWMC
* @retval none
*/
static void R3_1_F30X_RLSwitchOffPWM(CPWMC this)
{
  pDParams_t pDParams_str = DCLASS_PARAMS;
  pDVars_t pDVars_str = &DCLASS_VARS;
  TIM_TypeDef* TIMx = pDParams_str->TIMx;
  
  /* Main PWM Output Disable */
  if (DCLASS_VARS.BrakeActionLock == TRUE)
  {
  }
  else
  {
    TIMx->BDTR &= ~((uint32_t)(TIM_OSSIState_Enable));
    
    if ((pDParams_str->LowSideOutputs)== ES_GPIO)
    {
      GPIO_WriteBit(pDParams_str->hCh1NPort, pDParams_str->hCh1NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh2NPort, pDParams_str->hCh2NPin, Bit_RESET);
      GPIO_WriteBit(pDParams_str->hCh3NPort, pDParams_str->hCh3NPin, Bit_RESET);
    }
  }
  TIMx->BDTR &= (uint32_t)~TIM_BDTR_MOE;
  
  /* ADC_ITConfig(ADCx, ADC_IT_JEOS, DISABLE);*/
  pDVars_str->ADCx_1->IER &= (~(uint32_t)ADC_IT_JEOS);
  
  /* Flushing JSQR queue of context by setting JADSTP = 1 (JQM)=1 */
  pDVars_str->ADCx_1->CR |= ADC_CR_JADSTP;
 
  pDVars_str->ADCx_1->JSQR = R3_1_F30X_ADC_InjectedChannelConfig(pDVars_str->ADCx_1, 0u, 1u, 0u, 2u, ADC_ExternalTrigInjecEventEdge_RisingEdge, pDVars_str->ADC_ExternalTriggerInjected);
  pDVars_str->ADCx_1->CR |= ADC_CR_JADSTART;
  
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Disable);
  /* Set CC4 as PWM mode 2 (default) */
  TIMx->CCMR2 &= CCMR2_CH4_DISABLE;
  TIMx->CCMR2 |= CCMR2_CH4_PWM2;
  TIMx->CCR4 = 0xFFFFu;
  TIMx->CCR4 = 0x0u;
  TIM_OC4PreloadConfig(TIMx, TIM_OCPreload_Enable);
  
  while (ADC_GetFlagStatus(pDVars_str->ADCx_1,ADC_FLAG_JEOS)==RESET)
  {}
  
  /* ADCx_1 Injected conversions end interrupt enabling */
  ADC_ClearFlag(pDVars_str->ADCx_1, ADC_FLAG_JEOS);
  ADC_ITConfig(pDVars_str->ADCx_1, ADC_IT_JEOS, ENABLE);  
  return;
}

/**
  * @brief  Initializes the ADCx peripheral according to the specified parameters
  *         in the ADC_InitStruct.
  * @param  ADCx: where x can be 1, 2, 3 or 4 to select the ADC peripheral.
  * @param  ADC_InjectInitStruct: pointer to an ADC_InjecInitTypeDef structure that contains
  *         the configuration information for the specified ADC injected channel.
  * @retval None
  */
static uint32_t SingleADC_InjectedConfig(ADC_TypeDef* ADCx, ADC_InjectedInitTypeDef* ADC_InjectedInitStruct)
{
  uint32_t tmpreg1 = 0u;
  
  /*---------------------------- ADCx JSQR Configuration -----------------*/
  /* Get the ADCx JSQR value */
  tmpreg1 = ADCx->JSQR;
  
  /* Clear L bits */
  tmpreg1 &= JSQR_CLEAR_Mask;

  /* Configure ADCx: Injected channel sequence length, external trigger, 
     external trigger edge and sequences
  */
  tmpreg1 = (uint32_t) (((uint32_t)(ADC_InjectedInitStruct->ADC_NbrOfInjecChannel) - 1u) |
                         ADC_InjectedInitStruct->ADC_ExternalTrigInjecConvEvent |         
                         ADC_InjectedInitStruct->ADC_ExternalTrigInjecEventEdge |
                         (uint32_t)((ADC_InjectedInitStruct->ADC_InjecSequence1) << 8) |
                         (uint32_t)((ADC_InjectedInitStruct->ADC_InjecSequence2) << 14) |
                         (uint32_t)((ADC_InjectedInitStruct->ADC_InjecSequence3) << 20) |
                         (uint32_t)((ADC_InjectedInitStruct->ADC_InjecSequence4) << 26));
   
  return tmpreg1;  
}

/**
* @brief  It disables PWM generation on the proper Timer peripheral acting on 
*         MOE bit
*         This function is specific for RL detection phase.
* @param  this: related object of class CR3F30X_PWMC
* @retval none
*/
static void R3_1_F30X_COMPInit(CPWMC this)
{
  GPIO_InitTypeDef GPIO_InitStructure;
  pDParams_t pDParams_str = DCLASS_PARAMS;
  pF30XCOMPParams_t pDOCPA_COMPParams_str = pDParams_str->pOCPA_COMPParams;
  pF30XCOMPParams_t pDOCPB_COMPParams_str = pDParams_str->pOCPB_COMPParams;
  pF30XCOMPParams_t pDOCPC_COMPParams_str = pDParams_str->pOCPC_COMPParams;
  pF30XCOMPParams_t pDOVP_COMPParams_str  = pDParams_str->pOVP_COMPParams;
  
  /* Over current protection phase A */
  if (pDOCPA_COMPParams_str)
  {
    COMP_InitTypeDef COMP_InitStruct;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    /* NonInverting input*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_Pin = pDOCPA_COMPParams_str->hNonInvertingInput_GPIO_PIN;
    GPIO_Init(pDOCPA_COMPParams_str->hNonInvertingInput_GPIO_PORT, &GPIO_InitStructure);
    GPIO_PinLockConfig(pDOCPA_COMPParams_str->hNonInvertingInput_GPIO_PORT,
                       pDOCPA_COMPParams_str->hNonInvertingInput_GPIO_PIN);
    
    /* Inverting input*/
    if (pDOCPA_COMPParams_str->bInvertingInput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Pin = pDOCPA_COMPParams_str->hInvertingInput_GPIO_PIN;
      GPIO_Init(pDOCPA_COMPParams_str->hInvertingInput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinLockConfig(pDOCPA_COMPParams_str->hInvertingInput_GPIO_PORT,
                         pDOCPA_COMPParams_str->hInvertingInput_GPIO_PIN);
    }
    else
    {
      if (pDOCPA_COMPParams_str->wInvertingInput == COMP_InvertingInput_DAC1OUT1)
      {
        R3_1_F30X_SetAOReferenceVoltage(DAC_Channel_1, (uint16_t)(pDParams_str->hDAC_OCP_Threshold));
      }
      else if (pDOCPA_COMPParams_str->wInvertingInput == COMP_InvertingInput_DAC1OUT2)
      {
        R3_1_F30X_SetAOReferenceVoltage(DAC_Channel_2, (uint16_t)(pDParams_str->hDAC_OCP_Threshold));
      }
      else
      {
      }
    }
    
    /* Wait to stabilize DAC voltage */
    {
      volatile uint16_t waittime = 0u;
      for(waittime=0u;waittime<1000u;waittime++)
      {
      }
    }
    
    /* Output */
    if (pDOCPA_COMPParams_str->bOutput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
      GPIO_InitStructure.GPIO_Pin = pDOCPA_COMPParams_str->hOutput_GPIO_PIN;
      GPIO_Init(pDOCPA_COMPParams_str->hOutput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinAFConfig(pDOCPA_COMPParams_str->hOutput_GPIO_PORT,
                       F30X_GPIOPin2Source(pDOCPA_COMPParams_str->hOutput_GPIO_PIN),
                       pDOCPA_COMPParams_str->bOutput_GPIO_AF);
      GPIO_PinLockConfig(pDOCPA_COMPParams_str->hOutput_GPIO_PORT,
                         pDOCPA_COMPParams_str->hOutput_GPIO_PIN);
    }
    
    COMP_InitStruct.COMP_InvertingInput = pDOCPA_COMPParams_str->wInvertingInput;
    COMP_InitStruct.COMP_NonInvertingInput = pDOCPA_COMPParams_str->wNonInvertingInput;
    COMP_InitStruct.COMP_Output = pDOCPA_COMPParams_str->wOutput;
    COMP_InitStruct.COMP_OutputPol = pDOCPA_COMPParams_str->wOutputPol;
    COMP_InitStruct.COMP_BlankingSrce = COMP_BlankingSrce_None; 
    COMP_InitStruct.COMP_Hysteresis = COMP_Hysteresis_Low;
    COMP_InitStruct.COMP_Mode = pDOCPA_COMPParams_str->wMode;
    COMP_Init(pDOCPA_COMPParams_str->wSelection,&COMP_InitStruct);
    COMP_Cmd(pDOCPA_COMPParams_str->wSelection,ENABLE);
    COMP_LockConfig(pDOCPA_COMPParams_str->wSelection);
  }
  
  /* Over current protection phase B */
  if (pDOCPB_COMPParams_str)
  {
    COMP_InitTypeDef COMP_InitStruct;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    /* NonInverting input*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_Pin = pDOCPB_COMPParams_str->hNonInvertingInput_GPIO_PIN;
    GPIO_Init(pDOCPB_COMPParams_str->hNonInvertingInput_GPIO_PORT, &GPIO_InitStructure);
    GPIO_PinLockConfig(pDOCPB_COMPParams_str->hNonInvertingInput_GPIO_PORT,
                       pDOCPB_COMPParams_str->hNonInvertingInput_GPIO_PIN);
    
    /* Inverting input*/
    if (pDOCPB_COMPParams_str->bInvertingInput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Pin = pDOCPB_COMPParams_str->hInvertingInput_GPIO_PIN;
      GPIO_Init(pDOCPB_COMPParams_str->hInvertingInput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinLockConfig(pDOCPB_COMPParams_str->hInvertingInput_GPIO_PORT,
                         pDOCPB_COMPParams_str->hInvertingInput_GPIO_PIN);
    }
    
    /* Output */
    if (pDOCPB_COMPParams_str->bOutput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
      GPIO_InitStructure.GPIO_Pin = pDOCPB_COMPParams_str->hOutput_GPIO_PIN;
      GPIO_Init(pDOCPB_COMPParams_str->hOutput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinAFConfig(pDOCPB_COMPParams_str->hOutput_GPIO_PORT,
                       F30X_GPIOPin2Source(pDOCPB_COMPParams_str->hOutput_GPIO_PIN),
                       pDOCPB_COMPParams_str->bOutput_GPIO_AF);
      GPIO_PinLockConfig(pDOCPB_COMPParams_str->hOutput_GPIO_PORT,
                         pDOCPB_COMPParams_str->hOutput_GPIO_PIN);
    }
    
    COMP_InitStruct.COMP_InvertingInput = pDOCPB_COMPParams_str->wInvertingInput;
    COMP_InitStruct.COMP_NonInvertingInput = pDOCPB_COMPParams_str->wNonInvertingInput;
    COMP_InitStruct.COMP_Output = pDOCPB_COMPParams_str->wOutput;
    COMP_InitStruct.COMP_OutputPol = pDOCPB_COMPParams_str->wOutputPol;
    COMP_InitStruct.COMP_BlankingSrce = COMP_BlankingSrce_None; 
    COMP_InitStruct.COMP_Hysteresis = COMP_Hysteresis_Low;
    COMP_InitStruct.COMP_Mode = pDOCPB_COMPParams_str->wMode;
    COMP_Init(pDOCPB_COMPParams_str->wSelection,&COMP_InitStruct);
    COMP_Cmd(pDOCPB_COMPParams_str->wSelection,ENABLE);
    COMP_LockConfig(pDOCPB_COMPParams_str->wSelection);
  }
  
  /* Over current protection phase C */
  if (pDOCPC_COMPParams_str)
  {
    COMP_InitTypeDef COMP_InitStruct;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    /* NonInverting input*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_Pin = pDOCPC_COMPParams_str->hNonInvertingInput_GPIO_PIN;
    GPIO_Init(pDOCPC_COMPParams_str->hNonInvertingInput_GPIO_PORT, &GPIO_InitStructure);
    GPIO_PinLockConfig(pDOCPC_COMPParams_str->hNonInvertingInput_GPIO_PORT,
                       pDOCPC_COMPParams_str->hNonInvertingInput_GPIO_PIN);
    
    /* Inverting input*/
    if (pDOCPC_COMPParams_str->bInvertingInput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Pin = pDOCPC_COMPParams_str->hInvertingInput_GPIO_PIN;
      GPIO_Init(pDOCPC_COMPParams_str->hInvertingInput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinLockConfig(pDOCPC_COMPParams_str->hInvertingInput_GPIO_PORT,
                         pDOCPC_COMPParams_str->hInvertingInput_GPIO_PIN);
    }
    
    /* Output */
    if (pDOCPC_COMPParams_str->bOutput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
      GPIO_InitStructure.GPIO_Pin = pDOCPC_COMPParams_str->hOutput_GPIO_PIN;
      GPIO_Init(pDOCPC_COMPParams_str->hOutput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinAFConfig(pDOCPC_COMPParams_str->hOutput_GPIO_PORT,
                       F30X_GPIOPin2Source(pDOCPC_COMPParams_str->hOutput_GPIO_PIN),
                       pDOCPC_COMPParams_str->bOutput_GPIO_AF);
      GPIO_PinLockConfig(pDOCPC_COMPParams_str->hOutput_GPIO_PORT,
                         pDOCPC_COMPParams_str->hOutput_GPIO_PIN);
    }
    
    COMP_InitStruct.COMP_InvertingInput = pDOCPC_COMPParams_str->wInvertingInput;
    COMP_InitStruct.COMP_NonInvertingInput = pDOCPC_COMPParams_str->wNonInvertingInput;
    COMP_InitStruct.COMP_Output = pDOCPC_COMPParams_str->wOutput;
    COMP_InitStruct.COMP_OutputPol = pDOCPC_COMPParams_str->wOutputPol;
    COMP_InitStruct.COMP_BlankingSrce = COMP_BlankingSrce_None; 
    COMP_InitStruct.COMP_Hysteresis = COMP_Hysteresis_Low;
    COMP_InitStruct.COMP_Mode = pDOCPC_COMPParams_str->wMode;
    COMP_Init(pDOCPC_COMPParams_str->wSelection,&COMP_InitStruct);
    COMP_Cmd(pDOCPC_COMPParams_str->wSelection,ENABLE);
    COMP_LockConfig(pDOCPC_COMPParams_str->wSelection);
  }
  
  /* Over voltage protection */
  if (pDOVP_COMPParams_str)
  {
    COMP_InitTypeDef COMP_InitStruct;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    /* NonInverting input*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_Pin = pDOVP_COMPParams_str->hNonInvertingInput_GPIO_PIN;
    GPIO_Init(pDOVP_COMPParams_str->hNonInvertingInput_GPIO_PORT, &GPIO_InitStructure);
    GPIO_PinLockConfig(pDOVP_COMPParams_str->hNonInvertingInput_GPIO_PORT,
                       pDOVP_COMPParams_str->hNonInvertingInput_GPIO_PIN);
    
    /* Inverting input*/
    if (pDOVP_COMPParams_str->bInvertingInput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Pin = pDOVP_COMPParams_str->hInvertingInput_GPIO_PIN;
      GPIO_Init(pDOVP_COMPParams_str->hInvertingInput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinLockConfig(pDOVP_COMPParams_str->hInvertingInput_GPIO_PORT,
                         pDOVP_COMPParams_str->hInvertingInput_GPIO_PIN);
    }
    else
    {
      if (pDOVP_COMPParams_str->wInvertingInput == COMP_InvertingInput_DAC1OUT1)
      {
        R3_1_F30X_SetAOReferenceVoltage(DAC_Channel_1, (uint16_t)(pDParams_str->hDAC_OVP_Threshold));
      }
      else if (pDOVP_COMPParams_str->wInvertingInput == COMP_InvertingInput_DAC1OUT2)
      {
        R3_1_F30X_SetAOReferenceVoltage(DAC_Channel_2, (uint16_t)(pDParams_str->hDAC_OVP_Threshold));
      }
      else
      {
      }
    }
    
    /* Wait to stabilize DAC voltage */
    {
      volatile uint16_t waittime = 0u;
      for(waittime=0u;waittime<1000u;waittime++)
      {
      }
    }
    
    /* Output */
    if (pDOVP_COMPParams_str->bOutput_MODE == EXT_MODE)
    {
      GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
      GPIO_InitStructure.GPIO_Pin = pDOVP_COMPParams_str->hOutput_GPIO_PIN;
      GPIO_Init(pDOVP_COMPParams_str->hOutput_GPIO_PORT, &GPIO_InitStructure);
      GPIO_PinAFConfig(pDOVP_COMPParams_str->hOutput_GPIO_PORT,
                       F30X_GPIOPin2Source(pDOVP_COMPParams_str->hOutput_GPIO_PIN),
                       pDOVP_COMPParams_str->bOutput_GPIO_AF);
      GPIO_PinLockConfig(pDOVP_COMPParams_str->hOutput_GPIO_PORT,
                         pDOVP_COMPParams_str->hOutput_GPIO_PIN);
    }
    
    COMP_InitStruct.COMP_InvertingInput = pDOVP_COMPParams_str->wInvertingInput;
    COMP_InitStruct.COMP_NonInvertingInput = pDOVP_COMPParams_str->wNonInvertingInput;
    COMP_InitStruct.COMP_Output = pDOVP_COMPParams_str->wOutput;
    COMP_InitStruct.COMP_OutputPol = pDOVP_COMPParams_str->wOutputPol;
    COMP_InitStruct.COMP_BlankingSrce = COMP_BlankingSrce_None; 
    COMP_InitStruct.COMP_Hysteresis = COMP_Hysteresis_Low;
    COMP_InitStruct.COMP_Mode = pDOVP_COMPParams_str->wMode;
    COMP_Init(pDOVP_COMPParams_str->wSelection,&COMP_InitStruct);
    COMP_Cmd(pDOVP_COMPParams_str->wSelection,ENABLE);
    COMP_LockConfig(pDOVP_COMPParams_str->wSelection);
  }
}

/**
* @brief  It is used to configure the analog output used for protection 
*         thresholds.
* @param  DAC_Channel: the selected DAC channel. 
*          This parameter can be:
*            @arg DAC_Channel_1: DAC Channel1 selected
*            @arg DAC_Channel_2: DAC Channel2 selected
* @param  hDACVref Value of DAC reference expressed as 16bit unsigned integer.
*         Ex. 0 = 0V 65536 = VDD_DAC.
* @retval none
*/
static void R3_1_F30X_SetAOReferenceVoltage(uint32_t DAC_Channel, uint16_t hDACVref)
{ 
  DAC_InitTypeDef DAC_InitStructure;
  
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
  
  /* DAC Configuration */
  DAC_InitStructure.DAC_Trigger = DAC_Trigger_Software;
  DAC_InitStructure.DAC_WaveGeneration = DAC_WaveGeneration_None;
  DAC_InitStructure.DAC_Buffer_Switch = DAC_BufferSwitch_Enable;
  DAC_InitStructure.DAC_LFSRUnmask_TriangleAmplitude=DAC_TriangleAmplitude_1;
  DAC_Init(DAC1, DAC_Channel, &DAC_InitStructure);
  
  if (DAC_Channel == DAC_Channel_2)
  {
    DAC_SetChannel2Data(DAC1, DAC_Align_12b_L,hDACVref);
  }
  else
  {
    DAC_SetChannel1Data(DAC1, DAC_Align_12b_L,hDACVref);
  }
  
  /* Enable DAC Channel */
  DAC_SoftwareTriggerCmd(DAC1, DAC_Channel,ENABLE);
  DAC_Cmd(DAC1, DAC_Channel, ENABLE);
}

/**
  * @}
  */
  
/**
  * @}
  */

/**
  * @}
  */

/******************* (C) COPYRIGHT 2016 STMicroelectronics *****END OF FILE****/