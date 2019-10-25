/*******************************************************************************
**                                                                            **
**  Copyright (C) AUTOSarZs olc (2019)		                                  **
**                                                                            **
**  All rights reserved.                                                      **
**                                                                            **
**  This document contains proprietary information belonging to AUTOSarZs     **
**  olc . Passing on and copying of this document, and communication          **
**  of its contents is not permitted without prior written authorization.     **
**                                                                            **
********************************************************************************
**                                                                            **
**  FILENAME     : Can.c         			                                  **
**                                                                            **
**  VERSION      : 1.0.0                                                      **
**                                                                            **
**  DATE         : 2019-09-22                                                 **
**                                                                            **
**  VARIANT      : Variant PB                                                 **
**                                                                            **
**  PLATFORM     : TIVA C		                                              **
**                                                                            **
**  AUTHOR       : AUTOSarZs-DevTeam	                                      **
**                                                                            **
**  VENDOR       : AUTOSarZs OLC	                                          **
**                                                                            **
**                                                                            **
**  DESCRIPTION  : CAN Driver source file                                     **
**                                                                            **
**  SPECIFICATION(S) : Specification of CAN Driver, AUTOSAR Release 4.3.1     **
**                                                                            **
**  MAY BE CHANGED BY USER : no                                               **
**                                                                            **
*******************************************************************************/


/*****************************************************************************************/
/*                                   Include Common headres                              */
/*****************************************************************************************/

/*****************************************************************************************/
/*                                   Include Other  headres                              */
/*****************************************************************************************/

#include "Det.h"
// core_cm4.h is included to use NVIC_SystemReset Function in Assertion
#include "core_cm4.h"

/*****************************************************************************************/
/*                                   Include Component headres                           */
/*****************************************************************************************/
#include "hw_can.h"
#include "Can_lib.h"
#include "Can.h"
#include "irq.h"
#include "Timer0A.h"
#include "Det.h"
#include "CanIf_cbk.h"
/*****************************************************************************************/
/*                                   Local Macro Definition                              */
/*****************************************************************************************/

#define valid      1
#define invalid    0

/*****************************************************************************************/
/*                                   Local Definition                                    */
/*****************************************************************************************/
/*    Description        :   
 */

/*****************************************************************************************/
/*                                   Local types Definition                              */
/*****************************************************************************************/

/*    Type Description      : 	Struct to map each receive software meesage object with the number
 its configured hardware message objects in the HW FIFO
 HRHId     	: 	SW meesage objectId from CANIF
 StartMessageId  : 	ID of the first hardware message object in the HW FIFO
 StartMessageId  : 	ID of the last  hardware message object in the HW FIFO    */
typedef struct {
	uint8 HRHId;
	uint8 StartMessageId;
	uint8 EndMessageId;
} str_MessageObjAssignedToHRH;

/*    Type Description      : 	Struct to map each transmit software meesage object with the number
                                its configured hardware message objects in the HW FIFO 
		        HTHId     	: 	SW message objectId from CANIF
			StartMessageId  : 	ID of the  hardware meesage object in the HW    */
typedef struct
{
	uint8 HTHId;              
	uint8 MessageId;      
}str_MessageObjAssignedToHTH;
/*****************************************************************************************/
/*                                Exported Variables Definition                          */
/*****************************************************************************************/
/*    Type Description        :                                                          */
/*    Type range              :                                                          */

/*****************************************************************************************/
/*                                Local Variables Definition                             */
/*****************************************************************************************/
/*    Type Description       : variable to save Module state                             */
/*                             After power-up/reset, the Can module shall be in the state*/
/*                                CAN_UNINIT.   (SRS_BSW_00406)                          */
/*    Requirment              : SRS                                                      */
static Can_ModuleStateType ModuleState = CAN_UNINIT;

/*    Type Description        : array to save Controllers state                          */
/*    Requirment              : SWS                                                      */
static Can_ControllerStateType ControllerState[MAX_CONTROLLERS_NUMBER] = {
		CAN_CS_UNINIT, CAN_CS_UNINIT };

/*    Type Description        : array to save times of Disabling Controller Interrupts   */
/*    Requirment              : SWS                                                      */
static uint8 DisableCnt[MAX_CONTROLLERS_NUMBER];

/*    Type Description        : Pointer to save Module configration parameters           */
/*    Requirment              : SWS                                                      */
static Can_ConfigType* Global_ConfigType = NULL_PTR;

/*    Type Description        : array to save Number of poosible baudrate configration 
 for each controllers                                     */
static uint8 ControllerBaudrateConfigNum[MAX_CONTROLLERS_NUMBER] = {
		MAX_BAUDRATE_CONFIGS_CONTROLLER_0, MAX_BAUDRATE_CONFIGS_CONTROLLER_1 };

/* Type Description  :    Struct                                                          */
/* to save each start and end Hardware message object ID                                  */
/* of a specific software HRH(message object)                                             */
/* in case of HRH the number of  Hardware message objects >= 1                            */
static str_MessageObjAssignedToHRH MessageObjAssignedToHRH[CAN_HRH_NUMBER];

/* Type Description  :    Struct                                                          */
/* assign each software HTH to a hardware message object                                  */
static str_MessageObjAssignedToHTH MessageObjAssignedToHTH[CAN_HTH_NUMBER];
/** ***************************************************************************************/


static uint8 bClrPendingInt;
static Can_ErrorStateType Can_ErrorStateType_0;

/*

 STATIC Can_HwType MsgObject [MAX_NO_OF_OBJECTS] ;

 but if init function did its job we can use it from the configurations itself
 */
static Can_ConfigType * g_Config_Ptr;
static uint8 Can_DriverState = CAN_INITIALIZED; /// assume that init function has initialized it !
static tCANMsgObject *psMsgObject[CAN_CONTROLLER_ALLOWED_MESSAGE_OBJECTS];
static PduIdType Saved_swPduHandle;

uint8 HTH_Semaphore[MAX_NO_OF_OBJECTS] = {0}; //// simulation of a semaphore by a sad global variable to protect HTH or generally all tha HOH



/* This is a mapping between interrupt number (for the peripheral interrupts
   only) and the register that contains the interrupt enable for that interrupt.*/
static const uint32 g_pui32EnRegs[] =
{
		NVIC_EN0, NVIC_EN1, NVIC_EN2, NVIC_EN3, NVIC_EN4
};


/*****************************************************************************************/
/*                                   Local Function Declaration                          */
/*****************************************************************************************/

/*****************************************************************************************/
/*                                   Local Function Definition                           */
/*****************************************************************************************/


/*****************************************************************************************/
/*    Function Description    :  this function sets the baud rate                        */
/*    Parameter in            :  BaseAddress : required controller base address 
                                 BRConfig    : pointer to baudrate configration  
																 CanCpuClock : Value of CanCpu Clock in Hz               */
/*    Parameter inout         :                                                          */
/*    Parameter out           :                                                          */
/*    Return value            :                                                          */
/*    Requirment              :                                                          */
/*****************************************************************************************/

static void SetControllerBaudrate(uint32 BaseAddress,CanControllerBaudrateConfig* BRConfig,McuClockReferencePoint CanCpuClock)
{
	uint32 ui32BitReg = 0;
	uint32 ui32BaudRatePrescaler;
	uint32 n;
	
			/*
        To set the bit timing register, the controller must be placed in init
        mode (if not already), and also configuration change bit enabled.  State
        of the init bit must be saved so it can be restored at the end.
        Write accesses to the CANBIT register are allowed if the INIT bit is 1.*/
		/* CAN_CTL_CCE     Write accesses to the CANBIT register are allowed if the INIT bit is 1. */
		HWREG(BaseAddress + CAN_O_CTL) |= CAN_CTL_CCE; 	/* set INIT_BIT and CCE */


		/* 	Set the bit fields of the bit timing register
		 *	according to Tiva C TM4C123GH6PM page 1058
		 * 	TSEG2 		= Phase2 - 1
		 *	TSEG1 		= Prop + Phase1 - 1
		 *	SJW 		= SJW - 1
		 *	BRP =BRP */

		/* 	Time Segment After Sample Point (TSEG2)
		0x00-0x07: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
		So, for example, the reset value of 0x2 means that 3 (2+1) bit time quanta are defined for Phase2
		The bit time quanta is defined by the BRP field.*/
		ui32BitReg  = 	(((((BRConfig->CanControllerSeg2) - 1) << CAN_BIT_TSEG2_S)) & CAN_BIT_TSEG2_M);

		/* Time Segment Before Sample Point (TSEG1)
        0x00-0x0F: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
        So, for example, the reset value of 0x3 means that 4 (3+1) bit time quanta are defined for Phase1 .
        The bit time quanta is defined by the BRP field. */
		ui32BitReg |= ((((BRConfig->CanControllerPropSeg + BRConfig ->CanControllerSeg1) - 1) <<CAN_BIT_TSEG1_S) & CAN_BIT_TSEG1_M);

		/*(Re)Synchronization Jump Width (SJW) :
        0x00-0x03:The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
        During the start of frame (SOF), if the CAN controller detects a phase error (misalignment), it can adjust the length of TSEG2 or TSEG1 by the
        value in SJW. So the reset value of 0 adjusts the length by 1 bit time quanta*/
		ui32BitReg |= (((BRConfig->CanControllerSyncJumpWidth - 1) << CAN_BIT_SJW_S) & CAN_BIT_SJW_M);


		/*
		 * Baud rate Prescaler = tq * CAN Clock
		 * bit time =  n * tq
		 *          =[Sync + Prop + Phase1 + Phase2] أ— tq
		 *  Syn =1 all the time
		 * 1/Frequency(rate) =n*Tq
		 * 1/Frequency(rate)*n=Tq
		 * Baud rate Prescaler = CAN Clock*(1/(n*baud rate))
		 * Baud rate Prescaler = CAN Clock*(1/(n*baud rate))
		 */
		n = BRConfig->CanControllerSeg2 + BRConfig->CanControllerSeg1 + 1+ BRConfig->CanControllerPropSeg ;

		/*
		 * CanControllerBaudRate :-Specifies the baudrate of the controller in kbps
		 * Baud Rate Prescaler (BRP)
       The value by which the oscillator frequency is divided for generating the bit time quanta. The bit time is built up from a multiple of this quantum.
       0x00-0x03F: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
       BRP defines the number of CAN clock periods that make up 1 bit time quanta, so the reset value is 2 bit time quanta (1+1).
       The CANBRPE register can be used to further divide the bit time */

		ui32BaudRatePrescaler = ((CanCpuClock)/(((BRConfig->CanControllerBaudRate)*1000)*n));
		ui32BitReg |= ((ui32BaudRatePrescaler) - 1) & CAN_BIT_BRP_M;


		/* Set CANBIT Register */
		/* Register 4: CAN Bit Timing (CANBIT) :-
		 *             This register is used to program the bit width and bit quantum. Values are programmed to the system
                   clock frequency. This register is write-enabled by setting the CCE and INIT bits in the CANCTL register */
		HWREG(BaseAddress + CAN_O_BIT) = ui32BitReg;


		/* Set the divider upper bits in the extension register. */
		/* Baud Rate Prescaler Extension (BRPE)
       0x00-0x0F: Extend the BRP bit in the CANBIT register to values up to
       1023. The actual interpretation by the hardware is one more than the
       value programmed by BRPE (MSBs) and BRP (LSBs). */

		if (ui32BaudRatePrescaler < MaxValue_BRPE  && ui32BaudRatePrescaler > MinValue_BRPE)
		{
			HWREG(BaseAddress + CAN_O_BRPE) =((ui32BaudRatePrescaler - 1) >> 6) & CAN_BRPE_BRPE_M;
		}

		/* reset CCE to disable access to CANBIT register*/
		HWREG(BaseAddress + CAN_O_CTL) &= ~CAN_CTL_CCE;
}	

/*****************************************************************************************/
/*                                   Global Function Definition                          */
/*****************************************************************************************/

/*****************************************************************************************/
/*    Function Description    : This function initializes the module                     */
/*    Parameter in            : const Can_ConfigType* Config                             */
/*    Parameter inout         : none                                                     */
/*    Parameter out           : none                                                     */
/*    Return value            : none                                                     */
/*    Requirment              : SWS_Can_00223                                            */
/*    Notes                   : Can_Init() dosn't set  clock for controllers according   */
/*                                to [SWS_Can_00407] 									 */
/*								#If the register can affect several hardware modules and */
/*								if it is not an I/O register it shall be initialized by  */
/*								the MCU driver. 										 */
/*                              #One-time writable registers that require initialization */
/*								directly after reset shall be initialized by the startup */
/*								code #Tm4c123gh microcontroller doesn't support			 */
/*								Multiplexed Transmission So it's not configured          */
/*				  			                                                             */
/*																                         */
/*****************************************************************************************/
void Can_Init( const Can_ConfigType* Config)
{
	/*variable to count controllers number                           */
	uint8   controllerId; 
  /*variable to count SW HOH number                                */	
	uint8   HOHCount;  
  /*variable to save controller BaseAddres                         */	
	uint32  BaseAddress;  
  /*variable to save the number of hardware messages n each SW HOH */	
	uint8   HwObjectCount;   
  /*array to count the used hardware messages for each controller
	   can't exceed 32 "Hardware specification"                      */	
	uint8   UsedHWMessageObjt[MAX_CONTROLLERS_NUMBER] = {0};      
  /*pointer to point to the default BaudRate configuration         */
	CanControllerBaudrateConfig* BRConfig ;         


#if(CanDevErrorDetect==STD_ON)
	/*  Report error CAN_E_PARAM_POINTER API called with wrong parameter */
	if(Config==NULL_PTR)
	{
		Det_ReportError( CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_INIT_API_ID, CAN_E_PARAM_POINTER);
	}

	/*   The function Can_Init shall raise the error CAN_E_TRANSITION if the driver is not in
	 *   state CAN_UNINIT  [SWS_Can_00174]
	 */
	if(ModuleState!=CAN_UNINIT)
	{
		Det_ReportError( CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_INIT_API_ID, CAN_E_TRANSITION);
	}
#endif

	/*
	 *Save the configration pointer to be used in other APIs
	 */
	Global_ConfigType=(Can_ConfigType*)Config;

	/*
	 *   Loop to initialize all controllers configured to be used in the Module
	 */
	for(controllerId=0;controllerId<USED_CONTROLLERS_NUMBER;controllerId+=1)
	{
#if(CanDevErrorDetect==STD_ON)
		/*
		 *   The function Can_Init shall raise the error CAN_E_TRANSITION
		 *   if the Can controller is not in state CAN_CS_UNINIT [SWS_Can_00408]
		 */
		if(ControllerState[controllerId]!=CAN_CS_UNINIT)
		{
			Det_ReportError( CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_INIT_API_ID, CAN_E_TRANSITION);
		}
#endif
		/*
		 *    Save Current controller BaseAddress
		 */
		BaseAddress=Config->CanConfigSetRef->CanControllerRef[controllerId].CanControllerBaseAddress ;

		/*
					[SWS_Can_00245]   The function Can_Init shall initialize all CAN controllers 
           according to their configuration.
		 */
		HWREG(BaseAddress + CAN_O_CTL) = (CAN_CTL_INIT );

		/*
		 *	Set Baud rate fo each controller
		 */
		
		/*Save current baudrate Configurations*/
		BRConfig=&(Global_ConfigType->CanConfigSetRef->CanControllerRef[controllerId].\
		           CanControllerDefaultBaudrate[DEFAULT_BAUDRATE_CONFIGRATION_ID]); 
    /*Call static function SetControllerBaudrate to Set baud rate */		
		SetControllerBaudrate(BaseAddress,BRConfig,*(Global_ConfigType->CanConfigSetRef->\
		                                 CanControllerRef[controllerId].CanCpuClockRef));
	
		/*[SWS_Can_00259]  The function Can_Init shall set all CAN controllers
             in the state STOPPED*/
		ControllerState[controllerId]=CAN_CS_STOPPED ;
	}

	/*
	 * Loop to Configure Hardware message objects to be Transmit or receive
	 */
	for(HOHCount=0;HOHCount<CAN_HOH_NUMBER;HOHCount+=1)
	{
	
		/* Save the BaseAddress of the controller this Hardware Object Belongs to*/		
		BaseAddress=Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanControllerRef->CanControllerBaseAddress ;
		/*Fetch the current HOH Controller ID*/
		controllerId=Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanControllerRef->CanControllerId;
		HWREG(BaseAddress + CAN_O_IF1CMSK) |= (CAN_IF1CMSK_WRNRD | CAN_IF1CMSK_ARB | CAN_IF1CMSK_CONTROL );

		/*
		 * Configuration for transmit message object type HTH
		 */
		if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanObjectType==TRANSMIT)
		{
			HWREG(BaseAddress + CAN_O_IF1ARB2) &=~ CAN_IF1ARB2_MSGVAL;              /*must be cleared before configuration*/
			HWREG(BaseAddress + CAN_O_IF1MCTL) |= (CAN_IF1MCTL_TXIE );              
			HWREG(BaseAddress + CAN_O_IF1ARB2) |= CAN_IF1ARB2_DIR;                  /*transmit*/
			/*
			 * Configuration for 11-bits Standard ID type
			 */
			if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanIdType==STANDARD)
			{
				HWREG(BaseAddress + CAN_O_IF1ARB2) &= ~CAN_IF1ARB2_XTD;             /*Standard*/
			}
			/*
			 * Configuration for 29-bits Extended ID type
			 */
			else if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanIdType==EXTENDED)
			{
				HWREG(BaseAddress + CAN_O_IF1ARB2) |= CAN_IF1ARB2_XTD;                 /*Extended*/
			}
			HWREG(BaseAddress + CAN_O_IF1ARB2) |= CAN_IF1ARB2_MSGVAL ;               /*set as valid message object*/

			/*increament to save the number of occupied hardware objects*/ 			 
		   UsedHWMessageObjt[controllerId]++;
			
		#if(CanDevErrorDetect==STD_ON)
			if(UsedHWMessageObjt[controllerId]>CAN_CONTROLLER_ALLOWED_MESSAGE_OBJECTS)
			{
				/*Report error as number of used hardware message objects exceeded limit*/
				Det_ReportError( CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_INIT_API_ID, CAN_E_INIT_FAILED);
			}
			else
			{
			}
		#endif
		    MessageObjAssignedToHTH[HOHCount].HTHId=Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanObjectId ;
			MessageObjAssignedToHTH[HOHCount].MessageId=UsedHWMessageObjt[controllerId];
			HWREG(BaseAddress + CAN_O_IF1CRQ)   = UsedHWMessageObjt[controllerId];
		}
		/*
		 * Configuration for receive message object type HRH
		 */
		else if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanObjectType==RECEIVE)
		{

			/* [SWS_CAN_00489]  The CAN driver shall support controllers which implement a
			  hardware FIFO. The size of the FIFO is configured via "CanHwObjectCount". */
			/*To prevent loss of received messages, some controllers support a FIFO built
      from a set of hardware objects, while on other controllers  */
			HwObjectCount=Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanHwObjectCount;

			/*Loop to configure all hardware objects in the FIFO to Configure one HRH*/
			while(HwObjectCount--)
			{
				HWREG(BaseAddress + CAN_O_IF2CMSK) |= (CAN_IF2CMSK_WRNRD | CAN_IF2CMSK_ARB | CAN_IF2CMSK_CONTROL | CAN_IF2CMSK_MASK);
				HWREG(BaseAddress + CAN_O_IF2MCTL) |= (CAN_IF2MCTL_UMASK |CAN_IF2MCTL_RXIE|CAN_IF2MCTL_DLC_M);
				HWREG(BaseAddress + CAN_O_IF2ARB2) &=~ CAN_IF2ARB2_MSGVAL;                   /*must be cleared before configuration*/
				HWREG(BaseAddress + CAN_O_IF2ARB2) &=~ CAN_IF2ARB2_DIR;                      /*Receive*/
				if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanIdType==STANDARD)
				{
					/*   When using an 11-bit identifier, [12:2]bits are used for bits
					 *   [10:0] of the ID.
					 */
					HWREG(BaseAddress + CAN_O_IF2ARB2)&=~ CAN_IF2ARB2_XTD;
					HWREG(BaseAddress + CAN_O_IF2ARB2)|= (CAN_IF2ARB2_ID_STANDARD) & ((Config->CanConfigSetRef-> \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterCode)<<2);
					HWREG(BaseAddress + CAN_O_IF2MSK2) = (CAN_IF2MSK2_IDMSK_STANDARD) & ((Config->CanConfigSetRef-> \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterMask)<<2);
				}
				else if(Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanIdType==EXTENDED)
				{
					/*
					 *   When using a 29-bit identifier,  12:0 bits are used for bits [28:16] of the
					 *   ID. The field in the CANIFn MSK1&ARB1 register are used for bits [15:0]
					 * */
					HWREG(BaseAddress + CAN_O_IF2ARB2)|= CAN_IF2ARB2_XTD; /*Extended ID*/

					/*
					 * set ID in the arbitration register
					 * */
					HWREG(BaseAddress + CAN_O_IF2ARB1)|= (CAN_IF2ARB1_ID_M) & (Config->CanConfigSetRef->    \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterCode);
					HWREG(BaseAddress + CAN_O_IF2ARB2) = (CAN_IF2ARB2_ID_M) & ((Config->CanConfigSetRef->    \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterMask)>>0xF);
					/*Set Mask filter*/
					HWREG(BaseAddress + CAN_O_IF2MSK1) = (CAN_IF2MSK1_IDMSK_M) & (Config->CanConfigSetRef->  \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterMask);
					HWREG(BaseAddress + CAN_O_IF2MSK2) = (CAN_IF2MSK2_IDMSK_M) & ((Config->CanConfigSetRef->  \
							CanHardwareObjectRef[HOHCount].CanHwFilterRef->CanHwFilterMask)>>0xF);
				}
				/*set as valid message object*/
				HWREG(BaseAddress + CAN_O_IF2ARB2) |= CAN_IF2ARB2_MSGVAL ;

				/*increament to save the number of occupied hardware objects */
				UsedHWMessageObjt[controllerId]++;

			#if(CanDevErrorDetect==STD_ON)
				if(UsedHWMessageObjt[controllerId]>CAN_CONTROLLER_ALLOWED_MESSAGE_OBJECTS)
				{
					/*Report error as the number of occupied hardware message objects exceeded limit 32*/
					Det_ReportError(CAN_MODULE_ID , CAN_INSTANCE_ID, CAN_INIT_API_ID, CAN_E_INIT_FAILED);
				}
				else
        {
				}
			#endif
				if(HwObjectCount==0)  /*End of FIFO Reached*/
				{
					/*Set cuurent hardware message as the last one in FIFO */
			    	HWREG(BaseAddress + CAN_O_IF2MCTL)|= CAN_IF2MCTL_EOB ;
					/*Map the Current Software HRH with its hardware messages used in the buffer*/
					MessageObjAssignedToHRH[HOHCount].HRHId=Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount].CanObjectId;
					/*Save the ID of the first hardware message object used in the FIFO*/
					MessageObjAssignedToHRH[HOHCount].StartMessageId=UsedHWMessageObjt[controllerId]- Config->CanConfigSetRef->CanHardwareObjectRef[HOHCount]\
							.CanHwObjectCount +1 ;
					/*Save the ID of the last hardware message object used in the FIFO*/
					MessageObjAssignedToHRH[HOHCount].EndMessageId =UsedHWMessageObjt[controllerId];
				}
				/*Save the ID of the  hardware message object used in the register IF2CRQ*/
				HWREG(BaseAddress + CAN_O_IF2CRQ)   = UsedHWMessageObjt[controllerId];
			}
		}
	}

	/*[SWS_Can_00246]  ? The function Can_Init shall change the module state to 
     CAN_READY, after initializing all controllers inside  the
		 HWUnit. (SRS_SPAL_12057, SRS_Can_01041) */	 
	ModuleState = CAN_READY ;
}

#ifdef CanSetBaudrateApi
/*****************************************************************************************/
/*    Function Description    :The function service shall set the baud rate configuration*/
/*                             of the CAN controller .Depending on necessary baud rate   */
/*                             modifications the controller might have to reset          */
/*    Parameter in            : uint8 Controller, uint16 BaudRateConfigID                */
/*    Parameter inout         : none                                                     */
/*    Parameter out           : none                                                     */
/*    Return value            : Std_ReturnType                                           */
/*    Requirment              : SWS_Can_00491                                            */
/*****************************************************************************************/
Std_ReturnType Can_SetBaudrate(uint8 Controller, uint16 BaudRateConfigID) {
	/* local variable declarations */
	uint32 ui32BitReg = 0;
	uint32 ui32BaudRatePrescaler;
	uint32 n;
	uint32 ui32Base;
	CanControllerBaudrateConfig* current_baudrate =
			&(Global_ConfigType->CanConfigSetRef->CanControllerRef[Controller].CanControllerDefaultBaudrate[BaudRateConfigID]);

	/* fetch base address */
	ui32Base =
			Global_ConfigType->CanConfigSetRef->CanControllerRef[Controller].CanControllerBaseAddress;

#if(CanDevErrorDetect==STD_ON) /* DET notifications */

	/* [SWS_CAN_00492] If development error detection for the Can module is enabled
	 The function Can_SetBaudrate shall raise the error CAN_E_UNINIT and return
	 E_NOT_OK if the driver is not yet initialized.*/
	if (CAN_E_UNINIT == ControllerState[Controller]
			|| NULL_PTR == Global_ConfigType
			|| (!(HWREG(ui32Base + CAN_O_CTL) & CAN_CTL_INIT))) {
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_SETBAUDRATE_API_ID,
				CAN_E_UNINIT);
		return E_NOT_OK;
	}
#endif
	/* [SWS_CAN_00494]If development error detection for the Can module is enabled
	 the function Can_SetBaudrate shall raise the error CAN_E_PARAM_CONTROLLER
	 and return E_NOT_OK if the parameter Controller is out of range. */
#if(CanDevErrorDetect==STD_ON)
	if (Controller > CAN_CONTROLLERS_NUMBER) {
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_SETBAUDRATE_API_ID,
				CAN_E_PARAM_CONTROLLER);
		return E_NOT_OK;
	}
#endif

#if(CanDevErrorDetect==STD_ON)	
	/* [SWS_CAN_00493] If development error detection for the Can module is enabled:
	 The function Can_SetBaudrate shall raise the error CAN_E_PARAM_BAUDRATE
	 and return E_NOT_OK if the parameter BaudRateConfigID has an invalid value*/

	if (BaudRateConfigID > ControllerBaudrateConfigNum[Controller]) {
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CAN_SETBAUDRATE_API_ID,
				CAN_E_PARAM_BAUDRATE);
		return E_NOT_OK;
	}

#endif

	/*  [SWS_Can_00256] If the call of Can_SetBaudrate() would cause a re-initialization
	 of the CAN Controller and the CAN Controller is not in state STOPPED, it shall return E_NOT_OK.*/
	if (CAN_CS_STOPPED == ControllerState[Controller]) {

		/*
		 To set the bit timing register, the controller must be placed in init
		 mode (if not already), and also configuration change bit enabled.  State
		 of the init bit must be saved so it can be restored at the end.
		 Write accesses to the CANBIT register are allowed if the INIT bit is 1.*/
		/* CAN_CTL_CCE     Write accesses to the CANBIT register are allowed if the INIT bit is 1. */
		HWREG(ui32Base + CAN_O_CTL) |= CAN_CTL_CCE; /* set INIT_BIT and CCE */

		/* 	Set the bit fields of the bit timing register
		 *	according to Tiva C TM4C123GH6PM page 1058
		 * 	TSEG2 		= Phase2 - 1
		 *	TSEG1 		= Prop + Phase1 - 1
		 *	SJW 		= SJW - 1
		 *	BRP =BRP */

		/* 	Time Segment After Sample Point (TSEG2)
		 0x00-0x07: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
		 So, for example, the reset value of 0x2 means that 3 (2+1) bit time quanta are defined for Phase2
		 The bit time quanta is defined by the BRP field.*/
		ui32BitReg = (((((current_baudrate->CanControllerSeg2) - 1)
				<< CAN_BIT_TSEG2_S)) & CAN_BIT_TSEG2_M);

		/* Time Segment Before Sample Point (TSEG1)
		 0x00-0x0F: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
		 So, for example, the reset value of 0x3 means that 4 (3+1) bit time quanta are defined for Phase1 .
		 The bit time quanta is defined by the BRP field. */
		ui32BitReg |= ((((current_baudrate->CanControllerPropSeg
				+ current_baudrate->CanControllerSeg1) - 1) << CAN_BIT_TSEG1_S)
				& CAN_BIT_TSEG1_M);

		/*(Re)Synchronization Jump Width (SJW) :
		 0x00-0x03:The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
		 During the start of frame (SOF), if the CAN controller detects a phase error (misalignment), it can adjust the length of TSEG2 or TSEG1 by the
		 value in SJW. So the reset value of 0 adjusts the length by 1 bit time quanta*/
		ui32BitReg |= (((current_baudrate->CanControllerSyncJumpWidth - 1)
				<< CAN_BIT_SJW_S) & CAN_BIT_SJW_M);

		/*
		 * Baud rate Prescaler = tq * CAN Clock
		 * bit time =  n * tq
		 *          =[Sync + Prop + Phase1 + Phase2] أ— tq
		 *  Syn =1 all the time
		 * 1/Frequency(rate) =n*Tq
		 * 1/Frequency(rate)*n=Tq
		 * Baud rate Prescaler = CAN Clock*(1/(n*baud rate))
		 * Baud rate Prescaler = CAN Clock*(1/(n*baud rate))
		 */
		n = current_baudrate->CanControllerSeg2
				+ current_baudrate->CanControllerSeg1 + 1
				+ current_baudrate->CanControllerPropSeg;

		/*
		 * CanControllerBaudRate :-Specifies the baudrate of the controller in kbps
		 * Baud Rate Prescaler (BRP)
		 The value by which the oscillator frequency is divided for generating the bit time quanta. The bit time is built up from a multiple of this quantum.
		 0x00-0x03F: The actual interpretation by the hardware of this value is such that one more than the value programmed here is used.
		 BRP defines the number of CAN clock periods that make up 1 bit time quanta, so the reset value is 2 bit time quanta (1+1).
		 The CANBRPE register can be used to further divide the bit time */

		ui32BaudRatePrescaler =
				((*(Global_ConfigType->CanConfigSetRef->CanControllerRef[Controller]).CanCpuClockRef)
						/ (((current_baudrate->CanControllerBaudRate) * 1000)
								* n));
		ui32BitReg |= ((ui32BaudRatePrescaler) - 1) & CAN_BIT_BRP_M;

		/* Set CANBIT Register */
		/* Register 4: CAN Bit Timing (CANBIT) :-
		 *             This register is used to program the bit width and bit quantum. Values are programmed to the system
		 clock frequency. This register is write-enabled by setting the CCE and INIT bits in the CANCTL register */HWREG(ui32Base + CAN_O_BIT) =
				ui32BitReg;

		/* Set the divider upper bits in the extension register. */
		/* Baud Rate Prescaler Extension (BRPE)
		 0x00-0x0F: Extend the BRP bit in the CANBIT register to values up to
		 1023. The actual interpretation by the hardware is one more than the
		 value programmed by BRPE (MSBs) and BRP (LSBs). */

		if (ui32BaudRatePrescaler < MaxValue_BRPE
				&& ui32BaudRatePrescaler > MinValue_BRPE)
				{
			HWREG(ui32Base + CAN_O_BRPE) = ((ui32BaudRatePrescaler - 1) >> 6)
					& CAN_BRPE_BRPE_M;
		}

		/* reset CCE to disable access to CANBIT register*/HWREG(ui32Base + CAN_O_CTL) &=
				~CAN_CTL_CCE;

		return E_OK; /* Service request accepted, setting of (new) baud rate started */

	} else {
		return E_NOT_OK; /* Service request not accepted*/
	}
}
#endif

/*****************************************************************************************/
/*    Requirment              : SWS_Can_00232                                            */
/*    Function Description    : This function enables all allowed interrupts.            */
/*    Parameter in            : Controller                                               */
/*    Parameter inout         : none                                                     */
/*    Parameter out           : none                                                     */
/*    Return value            : none                                                     */
/*    Reentrancy              : Reentrant Function                                       */
/*																                         */
/*****************************************************************************************/
void Can_EnableControllerInterrupts(uint8 Controller) {
	/* Critical Section to protect shared resources in a reentrant Function */
	irq_Disable();
#if(CanDevErrorDetect==STD_ON)
	
	/*  [SWS_Can_00209] The function Can_EnableControllerInterrupts shall raise the error CAN_E_UNINIT if
     *  the driver not yet initialized
     */
	if (CAN_UNINIT == ModuleState)
    {
        Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_EnableControllerInterrupts_Id,
				CAN_E_UNINIT);
    }

    /*  [SWS_Can_00210] The function Can_EnableControllerInterrupts shall raise the error
     *  CAN_E_PARAM_CONTROLLER if the parameter Controller is out of range
     */
	else if (Controller >= MAX_CONTROLLERS_NUMBER)
    {
        Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_EnableControllerInterrupts_Id,
				CAN_E_PARAM_CONTROLLER);
    }
#endif

    /*  [SWS_Can_00202] When Can_DisableControllerInterrupts has been called
     *  several times, Can_EnableControllerInterrupts must be called as many times before
     *  the interrupts are re-enabled
     */
    if (DisableCnt[Controller] > 1) {
        DisableCnt[Controller]--;
    }

    /*  [SWS_Can_00208] The function Can_EnableControllerInterrupts shall perform no
     *  action when Can_DisableControllerInterrupts has not been called before
     */
    else if (1 == DisableCnt[Controller]) {
        DisableCnt[Controller] = 0;

        /* [SWS_Can_00050] The function Can_EnableControllerInterrupts shall enable all
         * interrupts that must be enabled according the current software status
         */
		/*  Enable the first CAN Controller Interrupts */ 
        if (CAN0_ID == Controller)
            CANIntEnable(CAN0_BASE,
                    CAN_CTL_EIE | CAN_CTL_SIE | CAN_CTL_IE);
        /*  Enable the second CAN Controller Interrupts */ 
		else if (CAN1_ID == Controller)
            CANIntEnable(CAN1_BASE,
                    CAN_CTL_EIE | CAN_CTL_SIE | CAN_CTL_IE);
        else {
        }
    }
    /* End of Critical Section */
	irq_Enable();
}

/*****************************************************************************************/
/*    Requirement             : SWS_Can_00231                                            */
/*    Function Description    : This function disables all interrupts                    */
/*                            : for this CAN controller                                  */
/*    Parameter in            : Controller                                               */
/*    Parameter inout         : none                                                     */
/*    Parameter out           : none                                                     */
/*    Return value            : none                                                     */
/*    Reentrancy              : Reentrant Function                                       */
/*****************************************************************************************/
void Can_DisableControllerInterrupts(uint8 Controller) {
	/* Critical Section to protect shared resources in a reentrant Function */
	irq_Disable();
#if(CanDevErrorDetect==STD_ON)

    /*  [SWS_Can_00205] The function Can_DisableControllerInterrupts shall raise the error CAN_E_UNINIT if
     *  the driver not yet initialized
     */
	if (CAN_UNINIT == ModuleState)
    {
        Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_DisableControllerInterrupts_Id,
				CAN_E_UNINIT);
    }

    /*  [SWS_Can_00206] The function Can_DisableControllerInterrupts shall raise the error
     *  CAN_E_PARAM_CONTROLLER if the parameter Controller is out of range
     */
	else if (Controller >= MAX_CONTROLLERS_NUMBER)
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_DisableControllerInterrupts_Id,
				CAN_E_PARAM_CONTROLLER);
#endif

    /*  [SWS_Can_00202] When Can_DisableControllerInterrupts has been called
     *  several times, Can_EnableControllerInterrupts must be called as many times before
     *  the interrupts are re-enabled
     */
    if (0 == DisableCnt[Controller])
    {
        /*  [SWS_Can_00049] The function Can_DisableControllerInterrupts shall access the
         *  CAN controller registers to disable all interrupts for that CAN controller only, if
         *  interrupts for that CAN Controller are enabled
         */
        /*  Disable the first CAN Controller Interrupts */ 
        if (CAN0_ID == Controller)
            CANIntDisable(CAN0_BASE,
                    CAN_CTL_EIE | CAN_CTL_SIE | CAN_CTL_IE);
        /*  Disable the second CAN Controller Interrupts */ 
		else if (CAN1_ID == Controller)
            CANIntDisable(CAN1_BASE,
                    CAN_CTL_EIE | CAN_CTL_SIE | CAN_CTL_IE);
        else {
        }
    }
    DisableCnt[Controller]++;

    /* End of Critical Section */
	irq_Enable();
}

/*****************************************************************************************/
/*    Requirement             : SWS_Can_91002                                            */
/*    Function Description    : This function de-initializes the module                  */
/*    Parameter in            : none                                                     */
/*    Parameter inout         : none                                                     */
/*    Parameter out           : none                                                     */
/*    Return value            : none                                                     */
/*    Reentrancy              : Non Reentrant Function                                   */
/*****************************************************************************************/
void Can_DeInit(void) {
#if(CanDevErrorDetect==STD_ON)
    /*   The function Can_DeInit shall raise the error CAN_E_TRANSITION if the driver is not
     *   in state CAN_READY [SWS_Can_91011]
     *   [SWS_BSW_00232] Call to De-Initialization functions :
     *   The module De-Initialization function shall be called only if the module was initialized before
	 */
    if (ModuleState != CAN_READY)
    {
        Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_DeInit_Id, CAN_E_TRANSITION);
    }
    /*  The function Can_DeInit shall raise the error CAN_E_TRANSITION if any of the CAN
     *  controllers is in state STARTED [SWS_Can_91012]
     */
    if (CAN_CS_STARTED == ControllerState[0]
            || CAN_CS_STARTED == ControllerState[1])
    {
        Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, Can_DeInit_Id,
                CAN_E_TRANSITION);
    }
#endif
    /*  [SWS_Can_ 91009] The function Can_DeInit shall change the module state to
     *  CAN_UNINIT before de-initializing all controllers inside the HW unit
     *  [SWS_BSW_00072] Module state after De-Initialization function
     *  The state of a BSW Module shall be set accordingly at the beginning of the DeInitialization function
     */
    ModuleState = CAN_UNINIT;
	/*	Disable the first four bits in CAN Control Register in both controllers */
    CLR_BITS( HWREG(CAN0_BASE + CAN_O_CTL),0
             , CAN_CTL_INIT | CAN_CTL_IE | CAN_CTL_SIE | CAN_CTL_EIE );   // DeInit CAN controller0
    CLR_BITS( HWREG(CAN1_BASE + CAN_O_CTL),0
             , CAN_CTL_INIT | CAN_CTL_IE | CAN_CTL_SIE | CAN_CTL_EIE );  // DeInit CAN controller1
}

void Can_MainFunction_Read(void) {

#if((CanRxProcessing == POLLING ) || (CanRxProcessing == MIXED))
    Can_HwType Mailbox; // the varaible for Callback function RxIndication
    int obj_index;  /* variable to count object number */
    uint8 controllerId; /*variable to count controllers number*/
    /*
     *   Loop all controllers to get the new data
     */
    // TODO controlledID  base address not implemented here ?
    for (controllerId = 0; controllerId < CAN_CONTROLLERS_NUMBER;controllerId += 1)
    {
#if (CanDevErrorDetect == STD_ON)
        if (ControllerState[controllerId] == CAN_CS_UNINIT)
        {
            // report error to diag
      //      Det_ReportError(Can_MainFunction_Read_ID,CAN_CS_UNINIT);
            continue;
        }//End if
#endif //CanDevErrorDetect
        /*
         * loop for all the hardware object to get the only new avialables data in this object
         */
        // TODO psMsgObject shold be config inside init API
        for(obj_index = 0; obj_index < NUM_OF_HOH; obj_index++)
        {
            if(Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[obj_index].CanObjectType==receive)
            {
                /*
                 * Reads a CAN message from one of the message object buffers.
                 */
                CANMessageGet(controllerId, obj_index, psMsgObject[obj_index], bClrPendingInt);
                // check if this object have new data avialables
                if(( psMsgObject[obj_index]->ui32Flags & MSG_OBJ_NEW_DATA) == MSG_OBJ_NEW_DATA)
                {
                    //message ID
                    Mailbox.CanId = psMsgObject[obj_index]->ui32MsgID;
                    //hardware object that has new date
                    Mailbox.Hoh = obj_index;
                    // controller ID
                    Mailbox.ControllerId = controllerId;
                    // 2. inform CanIf using API below.
                    //TODO fill  PduInfoPt and protoype of can iF
                  //  CanIf_RxIndication(Mailbox, PduInfoPtr);//We need to ask how to access these vars
                }// END IF
            }
        }// end of object in this controller ID
    }// end loop for controllerId
#endif
}

//Can_MainFunction_BusOff_0 or Can_MainFunction_BusOff
void Can_MainFunction_BusOff(void) {
uint8 controllerId; /*variable to count controllers number*/
#if CanBusoffProcessing == POLLING
    /*
     *   Loop all controllers to get the new data
     */
    for (controllerId = 0; controllerId < CAN_CONTROLLERS_NUMBER;controllerId += 1)
    {
#if (CanDevErrorDetect == STD_ON)
        if (ControllerState[controllerId] == CAN_CS_UNINIT)
        {
            // report error to diag
            //Det_ReportError(Can_MainFunction_Bus_OFF_ID,CAN_CS_UNINIT);
            continue;
        }// end if
#endif //CanDevErrorDetect
        /*
         * Reads one of the controller status registers. and get bus off status
         */
        // TODO controlledID  base address not implemented here ?
        if(CANStatusGet(controllerId, CAN_STS_CONTROL) == CAN_STATUS_BUS_OFF)
        {
            // the call back function in CANIF that report the bus of state
     //       CanIf_ControllerBusOff(controllerId);
            // raise the BUS_OFF flag to upper Layer
            //TODO raising this error
            // Can_ErrorStateType[controllerId]= CAN_ERRORSTATE_BUSOFF;
        }// End bus_off state If
    }// End Loop
#endif
}


Std_ReturnType Can_Write(

Can_HwHandleType Hth, const Can_PduType* PduInfo) {

	Std_ReturnType returnVal = E_OK;

	/*
	 * check if you left the DevErrorDectect Open ..
	 */
#if (CanDevErrorDetect == TRUE)
	if (Can_DriverState == CAN_NOT_INITIALIZED)
	{
		//// call the DET function .. CAN_E_UNIT
		returnVal = E_NOT_OK;
	}

	else if (Hth > 64)
	{
		//// call the DET .... CAN_E_PARAM_HANDLE
		returnVal = E_NOT_OK;
	}
	else if (PduInfo == NULL_PTR)
	{
		//// call the DET .... CAN_E_PARAM_POINTER
		returnVal = E_NOT_OK;
	}
	else if (PduInfo->length > 64)
	{
		//// call the DET .... CAN_E_PARAM_DATA_LENGTH
		returnVal = E_NOT_OK;
	}
	else

#endif
	{

		/*
		 * if every thing went well after the error detection .. prepare the parameters of CanMessageSet() [search for it]
		 */
		uint32 uiBase;
		uint32 ui32ObjID;
		tCANMsgObject * psMsgObject;
		tMsgObjType eMsgType;

		if (HTH_Semaphore[Hth] == 0) //// 0 : no one uses it at the moment !

				{
			///// start protecting your shared stuff man !
			HTH_Semaphore[Hth] = 1; //// acquire me

			/*
			 * choosing what can controller .. to detect what is the base address
			 */

			/*
			 for (index = 0 ; index < MAX_NO_OF_OBJECTS ; index++ )
			 {
			 if (g_Config_Ptr->HardWareObject[index].CanObjectId == Hth )
			 {
			 switch (g_Config_Ptr->HardWareObject[index].CanContrlloerRef)
			 {
			 case 0 : uiBase = CAN0_BASE ; break ;
			 case 1 : uiBase = CAN1_BASE ; break ;
			 }

			 psMsgObject->ui32Flags = ( g_Config_Ptr->HardWareObject[index].CanIdType == STANDARD )? MSG_OBJ_NO_FLAGS : MSG_OBJ_EXTENDED_ID ; /// flags are not handled in this Api except for stand vs extend

			 }
			 }
			 */

			/*
			 * another method to decide what can controller to use.
			 * if the argument HTH is bigger that 32 .. CAN0 .. else CAN1
			 * and assign the message object id with a proper value
			 */

			if (Hth > 32) {
				uiBase = CAN1_BASE;
				ui32ObjID = Hth - 32;
			} else {
				uiBase = CAN0_BASE;
				ui32ObjID = Hth;
			}

			/// what is the type of the message id ?

			//psMsgObject->ui32Flags =
					//(g_Config_Ptr->HardWareObject[Hth].CanIdType == STANDARD ) ?
							//MSG_OBJ_NO_FLAGS : MSG_OBJ_EXTENDED_ID;

			// also from the can_id_type itself
			/*
			 psMsgObject->ui32Flags = (PduInfo->id & (1<<31))?  MSG_OBJ_EXTENDED_ID  : MSG_OBJ_NO_FLAGS  ;			check the most significant bit
			 */

			/*
			 * PDU .. message object itself
			 */

			psMsgObject->ui32MsgID =
					(PduInfo->id & (1 << 31) ) ?
							(PduInfo->id & 0x1fffffff ) :
							(PduInfo->id & 0x07FF ); //// 11 bits or 29 bits to go
			psMsgObject->ui32MsgLen = PduInfo->length;
			psMsgObject->pui8MsgData = PduInfo->sdu;
			psMsgObject->ui32MsgIDMask = 0; //  no filtering mode .. maybe later

			/*
			 * the type of the CAN_write() function is obviously Tx
			 */
			eMsgType = MSG_OBJ_TYPE_TX;

			/////// stop protecting your stuff man !
			HTH_Semaphore[Hth] = 0; //// release me

			/*
			 * check if the controller is busy
			 */

			if (HWREG(uiBase + CAN_O_IF1CRQ) & CAN_IF1CRQ_BUSY)
			{
				returnVal = CAN_BUSY;
			}

			else {

				/*
				 *
				 * call the Tivaware function now !
				 */
				CANMessageSet(uiBase, ui32ObjID, psMsgObject, eMsgType);
				Saved_swPduHandle = PduInfo->swPduHandle; //// save the
				returnVal = E_OK;

			}

		} else /////// CAN Is busy
		{
			returnVal = CAN_BUSY;
		}
	}

	return returnVal;
}

/*---------------------------------------------------------------------
 Function Name:  <Can_MainFunction_Mode>
 Service ID:     <0x0c>
 Description:
 - Scheduled function.
 - This function performs the polling of CAN controller mode transitions.
 - it implements the polling of CAN status register flags to detect transition of CAN Controller state.
 ----------------------------------------------------------------------*/
void Can_MainFunction_Mode(void) {
	uint8 ControllerIndex;
	uint32 ui32Base;

	for (ControllerIndex = 0; ControllerIndex < NO_OF_CONTROLLERS_IN_HW;
			ControllerIndex++) {
		switch (ControllerIndex) {
		case 0:
			ui32Base = CAN0_BASE;
			break;
		case 1:
			ui32Base = CAN1_BASE;
			break;
		}

		if (HWREG(ui32Base + CAN_O_CTL) & CAN_CTL_INIT) ////// the controller is in intialization state
				{
			if (ControllerState[ControllerIndex] == CAN_CS_SLEEP)
					{
				//CanIf_ControllerModeIndication(ControllerIndex, CAN_CS_SLEEP); /// callback function
			} else {
			//	CanIf_ControllerModeIndication(ControllerIndex, CAN_CS_STOPPED);
			}
		} else {
	       // if (ControllerState[ControllerIndex] == CAN_CS_UNINIT)
			//	CanIf_ControllerModeIndication(ControllerIndex, CAN_CS_UNINIT);

			//else
			//	CanIf_ControllerModeIndication(ControllerIndex, CAN_CS_STARTED);
		}
	}
}

/*[SWS_Can_00230]         */
/*Service name   : Can_SetControllerMode
 Syntax           : Std_ReturnType Can_SetControllerMode(uint8 Controller,Can_ControllerStateType Transition)
 Service ID[hex]  : 0x03
 Sync/Async       : Asynchronous
 Reentrancy       : Non Reentrant
 Parameters (in)  : Controller CAN controller for which the status shall be changed
 Transition Transition value to request new CAN controller state
 Parameters(inout):None
 Parameters (out) : None
 Return value:Std_ReturnType E_OK    : request accepted
 E_NOT_OK: request not accepted, a development error occurred
 Description: This function performs software triggered state transitions of the CAN controller State machine.*/

Std_ReturnType Can_SetControllerMode( uint8 Controller, Can_ControllerStateType Transition )
{
    /*initialize local variable to return (E_OK,E_NOT_OK) */
    Std_ReturnType ret = E_NOT_OK;

    uint8 Can_HWObjIndex=0;
    uint8 HOH_Index;
#if (CAN_DEV_ERROR_DETECT==STD_ON)

    /*[SWS_Can_00198] If development error detection for the Can module is enabled:
                  if the module is not yet initialized, the function Can_SetControllerMode shall raise
                  development error CAN_E_UNINIT and return E_NOT_OK.*/
    if ( ControllerState[Controller] == CAN_UNINIT )
    {
        Det_ReportError( CAN_MODULE_ID ,CAN_INSTANCE_ID,CAN_SET_CONTROLLER_MODE ,CAN_E_UNINIT );

        ret = E_NOT_OK;
    }

    /* [SWS_Can_00199] If development error detection for the Can module is enabled:
                   if the parameter Controller is out of range, the function Can_SetControllerMode
                   shall raise development error CAN_E_PARAM_CONTROLLER and return
                   E_NOT_OK.*/
    else if (Controller >= MAX_CONTROLLERS_NUMBER)
    {
        Det_ReportError( CAN_MODULE_ID ,CAN_INSTANCE_ID ,CAN_SET_CONTROLLER_MODE ,CAN_E_PARAM_CONTROLLER );
        ret = E_NOT_OK;
    }

    /* [SWS_Can_00200] If development error detection for the Can module is enabled:
                   if an invalid transition has been requested, the function Can_SetControllerMode shall
                   raise the error CAN_E_TRANSITION and return E_NOT_OK.*/
    else if (((Transition == CAN_CS_STARTED) && ( ControllerState[Controller]!=CAN_CS_STOPPED))||
            ((Transition == CAN_CS_SLEEP) &&  (ControllerState[Controller]!= CAN_CS_STOPPED)))
    {
        Det_ReportError( CAN_MODULE_ID ,CAN_INSTANCE_ID ,CAN_SET_CONTROLLER_MODE ,CAN_E_TRANSITION );
        ret = E_NOT_OK;
    }

    else

#endif
    {
        switch(Transition)
        {
        case CAN_CS_STARTED:
            /*[SWS_Can_00261] The function Can_SetControllerMode(CAN_CS_STARTED)
                  shall set the hardware registers in a way that makes the CAN controller participating
                  on the network.*/
            if(Controller==CAN0_ID)
            {

                /* [SWS_Can_00384] Each time the CAN controller state machine is triggered with the state
                   transition value CAN_CS_STARTED, the function Can_SetControllerMode shall re-initialise
                   the CAN controller with the same controller configuration set previously used by functions
                   Can_SetBaudrate or Can_Init.*/
                if(DisableCnt[0U]==0U)
                {
                    /* Enables individual CAN controller interrupt sources */
                    HWREG(CAN0_BASE + CAN_O_CTL) |= CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS;
                    /* Enable the general interrupt.*/
                    HWREG(g_pui32EnRegs[(INT_CAN0 - 16) / 32]) =(1 << ((INT_CAN0 - 16) & 31));
                    /* Enables the CAN controller for message processing.  Once enabled, the
                  controller automatically transmits any pending frames, and processes any
                  received frames.*/
                    HWREG(CAN0_BASE + CAN_O_CTL) &= ~CAN_CTL_INIT;
                    /*set the interrupt Enable in start mode flag      */
                    DisableCnt[Controller]=1U;
                }

                else
                {
                    /* Enables the CAN controller for message processing.  Once enabled, the
                  controller automatically transmits any pending frames, and processes any
                  received frames.*/
                    HWREG(CAN0_BASE + CAN_O_CTL) &= ~CAN_CTL_INIT;
                }

            }

            else if(Controller==CAN1_ID)
            {
                /*SWS_Can_00425] Enabling of CAN interrupts shall not be executed,
                  when CAN interrupts have been disabled by function Can_DisableControllerInterrupts.*/


                if(DisableCnt[1U]==0U)
                {
                    /* [SWS_Can_00261]  The function Can_SetControllerMode(CAN_CS_STARTED) shall set
                     the hardware registers in a way that makes the CAN controller participating
                      on the network.*/
                    /* Enables individual CAN controller interrupt sources */
                    HWREG(CAN1_BASE + CAN_O_CTL) |= CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS;
                    /* Enable the general interrupt.*/
                    HWREG(g_pui32EnRegs[(INT_CAN1 - 16) / 32]) =1 << ((INT_CAN1 - 16) & 31);
                    /* Enables the CAN controller for message processing.  Once enabled, the
                  controller automatically transmits any pending frames, and processes any
                  received frames.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) &= ~CAN_CTL_INIT;
                    /*set the interrupt Enable in start mode flag      */
                    DisableCnt[Controller]=1U;
                }

                else
                {
                    /* Enables the CAN controller for message processing.  Once enabled, the
                  controller automatically transmits any pending frames, and processes any
                  received frames.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) &= ~CAN_CTL_INIT;
                }


            }
            else
            {

            }
            /* setting the new mode to STARTED */
            ControllerState[Controller] = CAN_CS_STARTED;
            break;

        case CAN_CS_STOPPED:
            /* [SWS_Can_00197]  The function Can_SetControllerMode shall disable interrupts that are not
                    allowed in the new state. */
            if(Controller == CAN0_ID)
            {
                /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                     by function Can_disableControllerInterrupts.*/
                if(DisableCnt[0U]>=1U)
                {
                    HWREG(CAN0_BASE + CAN_O_CTL) |= CAN_CTL_INIT;


                }
                else
                {
                    /* [SWS_Can_00263] The function Can_SetControllerMode(CAN_CS_STOPPED) shall set the
                   bits inside the CAN hardware such that the CAN controller stops participating on the
                   network.*/

                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                     by function Can_disableControllerInterrupts.*/
                    HWREG(CAN0_BASE + CAN_O_CTL) |= CAN_CTL_INIT;
                    /* disable the CAN controller Interrupt     */
                    HWREG(CAN0_BASE + CAN_O_CTL) &= ~(CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
                    /* set interrupt disable in stop mode */
                    DisableCnt[Controller]=1U;

                }

                for(HOH_Index=0U;HOH_Index<NUM_OF_HOH;HOH_Index++)
                {
                     if(Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanObjectType == transmit)
                        {
                            while(Can_HWObjIndex<Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanHwObjectCount)
                            {

                                CAN0_IF1CRQ_R =((uint32)((uint32)Can_HWObjIndex));
                                CAN0_IF1MCTL_R&=((uint32)(~(uint32)0x100U));
                                CAN0_IF1CMSK_R &= ((uint32)(~(uint32)0x80U));
                                Can_HWObjIndex++;
                            }
                        }
                        else
                        {

                        }

            }

            }
            else if(Controller == CAN1_ID)
            {
                if(DisableCnt[1U]>=1U)
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                     by function Can_disableControllerInterrupts.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) |= CAN_CTL_INIT;


                }
                else
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                     by function Can_disableControllerInterrupts.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) |= CAN_CTL_INIT;

                    /* disable the CAN controller Interrupt     */
                    HWREG(CAN1_BASE + CAN_O_CTL) &= ~(CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
                    /* set interrupt disable in stop mode */
                    DisableCnt[Controller]=1U;

                }

            for(HOH_Index=0U;HOH_Index<NUM_OF_HOH;HOH_Index++)
            {

                    if(Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanObjectType==transmit)
                    {
                        while(Can_HWObjIndex<Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanHwObjectCount)
                        {

                            CAN1_IF1CRQ_R =((uint32)((uint32)Can_HWObjIndex));
                            CAN1_IF1MCTL_R&=((uint32)(~(uint32)0x100U));
                            CAN1_IF1CMSK_R &= ((uint32)(~(uint32)0x80U));
                            Can_HWObjIndex++;
                                                   }
                    }
                    else
                    {

                    }

            }
            }

            /* setting the new mode to STOPPED */
            ControllerState[Controller] = CAN_CS_STOPPED;

            break;
        case CAN_CS_SLEEP:
            /*[SWS_Can_00258] When the CAN hardware does not support sleep mode and is triggered to
                     transition into SLEEP state, the Can module shall emulate a logical SLEEP state from which it
                     returns only, when it is triggered by software to transition into STOPPED state.*/
            /*[SWS_Can_00404] The CAN hardware shall remain in state STOPPED, while the logical SLEEP state is active.
                      [SWS_Can_00290] If the CAN HW does not support a sleep mode, the function
                     Can_SetControllerMode(CAN_CS_SLEEP) shall set the CAN controller to the logical sleep mode.*/
            /*[SWS_Can_00197] The function Can_SetControllerMode shall disable interrupts that are not allowed in the new state. */
            if(Controller == CAN0_ID)
            {
                if(DisableCnt[0U]>=1U)
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                          by function Can_disableControllerInterrupts.*/
                    HWREG(CAN0_BASE + CAN_O_CTL) |= CAN_CTL_INIT;


                }
                else
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                          by function Can_disableControllerInterrupts.*/
                    HWREG(CAN0_BASE + CAN_O_CTL) |= CAN_CTL_INIT;

                    /* disable the CAN controller Interrupt     */
                    HWREG(CAN0_BASE + CAN_O_CTL) &= ~(CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
                    /* set interrupt disable in stop mode */
                    DisableCnt[Controller]=1U;

                }

                /*[SWS_Can_00282] The function Can_SetControllerMode(CAN_CS_STOPPED)
                                 shall cancel pending messages.*/


                for(HOH_Index=0U;HOH_Index<NUM_OF_HOH;HOH_Index++)
                {

                        if(Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanObjectType== transmit)
                        {
                            while(Can_HWObjIndex<Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanHwObjectCount)
                            {

                                CAN0_IF1CRQ_R =((uint32)((uint32)Can_HWObjIndex));
                                CAN0_IF1MCTL_R&=((uint32)(~(uint32)0x100U));
                                CAN0_IF1CMSK_R &= ((uint32)(~(uint32)0x80U));
                                Can_HWObjIndex++;
                            }
                        }
                        else
                        {

                        }

                }


            }
            else if(Controller == CAN1_ID)
            {
                if(DisableCnt[1U]>=1U)
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                          by function Can_disableControllerInterrupts.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) |= CAN_CTL_INIT;


                }
                else
                {
                    /*Disabling of CAN interrupts shall not be executed, when CAN interrupts have been disabled
                          by function Can_disableControllerInterrupts.*/
                    HWREG(CAN1_BASE + CAN_O_CTL) |= CAN_CTL_INIT;
                    /* disable the CAN controller Interrupt     */
                    HWREG(CAN1_BASE + CAN_O_CTL) &= ~(CAN_INT_MASTER | CAN_INT_ERROR | CAN_INT_STATUS);
                    /* set interrupt disable in stop mode */
                    DisableCnt[Controller]=1U;

                }


       /*[SWS_Can_00282] The function Can_SetControllerMode(CAN_CS_STOPPED)
            shall cancel pending messages.*/


            for(HOH_Index=0U;HOH_Index<NUM_OF_HOH;HOH_Index++)
            {

                    if(Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanObjectType==transmit)
                    {
                        while(Can_HWObjIndex<Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[HOH_Index].CanHwObjectCount)
                        {

                            CAN1_IF1CRQ_R =((uint32)((uint32)Can_HWObjIndex));
                            CAN1_IF1MCTL_R&=((uint32)(~(uint32)0x100U));
                            CAN1_IF1CMSK_R &= ((uint32)(~(uint32)0x80U));
                            Can_HWObjIndex++;
                        }
                    }
                    else
                    {

                    }
                    }

            }


            /* setting the new mode to SLEEP */
            ControllerState[Controller] = CAN_CS_SLEEP;


        }
    }


    /*[SWS_Can_00262] The function Can_SetControllerMode(CAN_CS_STARTED) shall wait for
                  limited time until the CAN controller is fully operational. Compare to SWS_Can_00398.*/

    /*[SWS_Can_00264] The function Can_SetControllerMode(CAN_CS_STOPPED) shall wait for
                  a limited time until the CAN controller is really switched off. Compare to SWS_Can_00398.*/
    Timer0A_Init();
    while((TIMER0_RIS_R & 0x1) == 0)
    {

    }
    /*clear TIMER0A timeout flag   */
    TIMER0_ICR_R = 0x01;
    /* return the value E_OK
                        E_NOT_OK*/
    return ret;
}


/**
This function checks if a wakeup has occurred for the given controller.
Service ID[hex]:0x11
Parameters (in): Controller --> Abstracted CanIf ControllerId which is assigned to a CAN controller, which is requested for ErrorState.
Parameters (out): ErrorStatePtr --> Pointer to a memory location, where the error state of the CAN controller will be stored
Return value:   Std_ReturnType  -->E_OK: Error state request has been accepted
                                   E_NOT_OK:Error state request has not been accepted
*/
Std_ReturnType Can_GetControllerErrorState(uint8 ControllerId,Can_ErrorStateType* ErrorStatePtr)
{
Std_ReturnType Return_type =E_OK;
#if(CAN_DEV_ERROR_DETECT==STD_ON)
/**
[SWS_Can_91006]  If development error detection for the Can module is enabled: if the parameter ControllerId is out of range,
the function Can_GetControllerErrorState shall raise development error CAN_E_PARAM_CONTROLLER and return E_NOT_OK.
*/
if(ControllerId >= MAX_CONTROLLERS_NUMBER )
{
    Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CANGetControllerErrorState, CAN_E_PARAM_CONTROLLER);
    return E_NOT_OK;
}
/**
[SWS_Can_91005]  If development error detection for the Can module is enabled: if the module is not yet initialized,
 the function Can_GetControllerErrorState shall raise development error CAN_E_UNINIT and return E_NOT_OK
*/
if(Can_ControllerMode[ControllerId] == CAN_CS_UNINIT)
{
    Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CANGetControllerErrorState, CAN_E_UNINIT);
    return E_NOT_OK;
}
/**
[SWS_Can_91007]  If development error detection for the Can module is enabled: if the parameter ErrorStatePtr is a null pointer,
 the function Can_GetControllerErrorState shall raise development error  CAN_E_PARAM_POINTER and return E_NOT_OK.
*/
if(ErrorStatePtr == NULL)
{
    Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID, CANGetControllerErrorState, CAN_E_PARAM_POINTER);
    return E_NOT_OK;
}

#endif
/**
[SWS_Can_91008] When the API Can_GetControllerErrorState()  is called with Controller Id as input parameter then Can driver shall read
the error state register of Can Controller and shall return the error status to upper layer
*/
         switch(ControllerId)
         {
            case  CAN0_ID :

                    if(CAN0_STS_R|CAN_STS_BOFF )
                     {
                         *ErrorStatePtr =CAN_ERRORSTATE_BUSOFF;
                     }
                     else
                     {
                      switch (CAN0_STS_R |CAN_STS_EPASS )
                      {

                          case CAN_ERRORSTATE_ACTIVE :
                          *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
                          break;
                          case  CAN_ERRORSTATE_PASSIVE:
                          *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
                          break;
                          default  :
                          Return_type =E_NOT_OK;
                          break;
                      }
                     }
                   break;
            case  CAN1_ID :
                    if(CAN1_STS_R|CAN_STS_BOFF )
                     {
                         *ErrorStatePtr =CAN_ERRORSTATE_BUSOFF;
                     }
                     else
                     {
                      switch (CAN1_STS_R |CAN_STS_EPASS )
                      {

                          case CAN_ERRORSTATE_ACTIVE :
                          *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
                          break;
                          case  CAN_ERRORSTATE_PASSIVE :
                          *ErrorStatePtr = CAN_ERRORSTATE_ACTIVE;
                          break;
                          default  :
                          Return_type =E_NOT_OK;
                          break;
                      }
                     }
                   break;
            default :
                 Return_type =E_NOT_OK;
                 break;
         }
return  Return_type;
}

Std_ReturnType Can_GetControllerMode(uint8 Controller,Can_ControllerStateType* ControllerModePtr) 
{
	Std_ReturnType Loc_Can_GetControllerMode_Ret = E_OK;

#if(CAN_DEV_ERROR_DETECT == STD_ON)
	if (CAN_CS_UNINIT==ControllerState[Controller])
	{
		Det_ReportError(CAN_MODULE_ID,CAN_INSTANCE_ID,Can_GetControllerMode_Id,CAN_E_UNINIT);
		Loc_Can_GetControllerMode_Ret = E_NOT_OK;
	}
	else
	{
	    /* Do Nothing */
	}

#endif

	if ( NUM_OF_CAN_CONTROLLERS <= Controller )
	{
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID,Can_GetControllerMode_Id, CAN_E_PARAM_CONTROLLER);
		Loc_Can_GetControllerMode_Ret = E_NOT_OK;
	} 
	else 
	{
	    /* Do Nothing */
	}
	if ( NULL == ControllerModePtr)
	{
		Det_ReportError(CAN_MODULE_ID, CAN_INSTANCE_ID,Can_GetControllerMode_Id, CAN_E_PARAM_POINTER);
		Loc_Can_GetControllerMode_Ret = E_NOT_OK;
	} 
	else
	{
	    /* Do Nothing */
	}

	if ( E_NOT_OK != Loc_Can_GetControllerMode_Ret)
	{

		*ControllerModePtr = ControllerState[Controller];
	}
	else
	{
	    /* Do Nothing */
	}

	return Loc_Can_GetControllerMode_Ret;

}

void Can_MainFunction_Write(void) 
{

#if ((CAN_TX_PROCESSING_0==POLLING_PROCESSING) || (CAN_TX_PROCESSING_1==POLLING_PROCESSING)\
	    || (CAN_TX_PROCESSING_0==MIXED_PROCESSING) || (CAN_TX_PROCESSING_1==MIXED_PROCESSING))

	uint8 index;
	PduIdType PduId; /* Stub variable */

	#if (CAN_CONTROLLER_0_ACTIVATION == STD_ON)
	
		/* Transfer the data in the CAN message object specified by
			the MNUM field in the CANIFnCRQ register into the CANIFn
			registers*/
		CLR_BIT_PERPHBAND(CAN0_IF1CMSK_A, CAN_IF1CMSK_WRNRD);
	
	#else 
		/*Do Nothing*/
	
	#endif 

	#if (CAN_CONTROLLER_1_ACTIVATION == STD_ON) 
	
		/* Transfer the data in the CAN message object specified by
			the MNUM field in the CANIFnCRQ register into the CANIFn
			registers*/
		CLR_BIT_PERPHBAND(CAN1_IF1CMSK_A, CAN_IF1CMSK_WRNRD);
	
	#else 
		/*Do Nothing*/
	
	#endif	

	#if (CAN_CONTROLLER_0_ACTIVATION == STD_ON || CAN_CONTROLLER_1_ACTIVATION == STD_ON)
		
		for (index = 0; index < NUM_OF_HOH; index++) 
		{
			if (TRANSMIT == Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[index].CanObjectType)
			{
				if (&Global_ConfigType->CanConfigSetRef->CanControllerRef[CONTROLLER_0_ID] == Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[index].CanControllerRef)
				{
					#if (CAN_TX_PROCESSING_0==POLLING_PROCESSING)

						CAN0_IF1CRQ_R=MessageObjAssignedToHOH[index].StartMessageId;

						if(GET_BIT_PERPHBAND(CAN0_IF1MCTL_A,CAN_IF1MCTL_TXRQST) == (uint32)0)
						{
							CanIf_TxConfirmation(PduId);

							/*The Can module shall call CanIf_TxConfirmation to indicate a
							 successful transmission.It shall either called by the TX-interrupt service routine
							 of the corresponding HW resource or inside the Can_MainFunction_Write in case of
							 polling mode.*/
						}

						else
						{
							/* Do Nothing */
						}
					
					#elif(CAN_TX_PROCESSING_0==MIXED_PROCESSING)

						if ( STD_ON == Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[index].CanHardwareObjectUsesPolling)
						{
														
							CAN0_IF1CRQ_R =	MessageObjAssignedToHTH[index].MessageId;

							if (GET_BIT_PERPHBAND(CAN0_IF1MCTL_A,CAN_IF1MCTL_TXRQST) == (uint32) 0) 
							{
								//CanIf_TxConfirmation(PduId);

								/*The Can module shall call CanIf_TxConfirmation to indicate a
								 successful transmission.It shall either called by the TX-interrupt service routine
								 of the corresponding HW resource or inside the Can_MainFunction_Write in case of
								 polling mode.*/
							}

							else 
							{
								/* Do Nothing */
							}
						}

						else 
						{
							/*Do Nothing */
						}	

					#else
					
					/*Do Nothing */
					#endif
				}

				else if (&Global_ConfigType->CanConfigSetRef->CanControllerRef[CONTROLLER_1_ID] == Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[index].CanControllerRef)
				{
					#if (CAN_TX_PROCESSING_1==POLLING_PROCESSING)
				
						CAN1_IF1CRQ_R=MessageObjAssignedToHOH[index].StartMessageId;

						if(GET_BIT_PERPHBAND(CAN1_IF1MCTL_A,CAN_IF1MCTL_TXRQST) == (uint32)0)
						{
							CanIf_TxConfirmation(PduId);

							/*The Can module shall call CanIf_TxConfirmation to indicate a
							 successful transmission.It shall either called by the TX-interrupt service routine
							 of the corresponding HW resource or inside the Can_MainFunction_Write in case of
							 polling mode.*/
						}

						else
						{
							/* Do Nothing */
						}
					
					#elif(CAN_TX_PROCESSING_1==MIXED_PROCESSING)

						if (STD_ON == Global_ConfigType->CanConfigSetRef->CanHardwareObjectRef[index].CanHardwareObjectUsesPolling)
						{
							
							CAN1_IF1CRQ_R =MessageObjAssignedToHTH[index].MessageId;

							if (GET_BIT_PERPHBAND(CAN1_IF1MCTL_A,CAN_IF1MCTL_TXRQST) == (uint32) 0) 
							{
								
								CanIf_TxConfirmation(PduId);

								/*The Can module shall call CanIf_TxConfirmation to indicate a
								 successful transmission.It shall either called by the TX-interrupt service routine
								 of the corresponding HW resource or inside the Can_MainFunction_Write in case of
								 polling mode.*/
							}

							else 
							{
								/* Do Nothing */
							}
						
						}

						else 
						{
							/*Do Nothing */
						}	

					#else
						/*Do Nothing */
					#endif
                }

                else
                {
						/* Do Nothing */
                }
            }

            else
            {
                /* Do Nothing */
            }
		}

	#else
			/* Do Nothing */
	#endif

#else
		/* Do Nothing */
#endif
}

/* Prototype for the function that is called when an invalid argument is passed
 * to an API.  This is only used when doing a DEBUG build.
 */
__attribute__((naked)) void assert_failed (char const *file, int line) {
    (void)file; /* avoid the "unused parameter" compiler warning */
    (void)line;    /* avoid the "unused parameter" compiler warning */
	/* For a production code , I think it is better to reset the system
	 * not to cause an infinite loop which is a denial service
	 * NVIC_SystemReset Function exists in "core_cm4.h" 
	 */
    NVIC_SystemReset(); /* reset the system */
}
