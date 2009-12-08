/** @file

  Copyright (c) 2008-2009, Apple Inc. All rights reserved.
  
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "CpuDxe.h" 
#include <Library/CacheMaintenanceLib.h>

VOID
ExceptionHandlersStart (
  VOID
  );

VOID
ExceptionHandlersEnd (
  VOID
  );

VOID
CommonExceptionEntry (
  VOID
  );

VOID
AsmCommonExceptionEntry (
  VOID
  );


EFI_EXCEPTION_CALLBACK  gExceptionHandlers[MAX_ARM_EXCEPTION + 1];
EFI_EXCEPTION_CALLBACK  gDebuggerExceptionHandlers[MAX_ARM_EXCEPTION + 1];



/**
  This function registers and enables the handler specified by InterruptHandler for a processor 
  interrupt or exception type specified by InterruptType. If InterruptHandler is NULL, then the 
  handler for the processor interrupt or exception type specified by InterruptType is uninstalled. 
  The installed handler is called once for each processor interrupt or exception.

  @param  InterruptType    A pointer to the processor's current interrupt state. Set to TRUE if interrupts
                           are enabled and FALSE if interrupts are disabled.
  @param  InterruptHandler A pointer to a function of type EFI_CPU_INTERRUPT_HANDLER that is called
                           when a processor interrupt occurs. If this parameter is NULL, then the handler
                           will be uninstalled.

  @retval EFI_SUCCESS           The handler for the processor interrupt was successfully installed or uninstalled.
  @retval EFI_ALREADY_STARTED   InterruptHandler is not NULL, and a handler for InterruptType was
                                previously installed.
  @retval EFI_INVALID_PARAMETER InterruptHandler is NULL, and a handler for InterruptType was not
                                previously installed.
  @retval EFI_UNSUPPORTED       The interrupt specified by InterruptType is not supported.

**/
EFI_STATUS
RegisterInterruptHandler (
  IN EFI_EXCEPTION_TYPE             InterruptType,
  IN EFI_CPU_INTERRUPT_HANDLER      InterruptHandler
  )
{
  if (InterruptType > MAX_ARM_EXCEPTION) {
    return EFI_UNSUPPORTED;
  }

  if ((InterruptHandler != NULL) && (gExceptionHandlers[InterruptType] != NULL)) {
    return EFI_ALREADY_STARTED;
  }

  gExceptionHandlers[InterruptType] = InterruptHandler;

  return EFI_SUCCESS;
}


/**
  This function registers and enables the handler specified by InterruptHandler for a processor 
  interrupt or exception type specified by InterruptType. If InterruptHandler is NULL, then the 
  handler for the processor interrupt or exception type specified by InterruptType is uninstalled. 
  The installed handler is called once for each processor interrupt or exception.

  @param  InterruptType    A pointer to the processor's current interrupt state. Set to TRUE if interrupts
                           are enabled and FALSE if interrupts are disabled.
  @param  InterruptHandler A pointer to a function of type EFI_CPU_INTERRUPT_HANDLER that is called
                           when a processor interrupt occurs. If this parameter is NULL, then the handler
                           will be uninstalled.

  @retval EFI_SUCCESS           The handler for the processor interrupt was successfully installed or uninstalled.
  @retval EFI_ALREADY_STARTED   InterruptHandler is not NULL, and a handler for InterruptType was
                                previously installed.
  @retval EFI_INVALID_PARAMETER InterruptHandler is NULL, and a handler for InterruptType was not
                                previously installed.
  @retval EFI_UNSUPPORTED       The interrupt specified by InterruptType is not supported.

**/
EFI_STATUS
RegisterDebuggerInterruptHandler (
  IN EFI_EXCEPTION_TYPE             InterruptType,
  IN EFI_CPU_INTERRUPT_HANDLER      InterruptHandler
  )
{
  if (InterruptType > MAX_ARM_EXCEPTION) {
    return EFI_UNSUPPORTED;
  }

  if ((InterruptHandler != NULL) && (gDebuggerExceptionHandlers[InterruptType] != NULL)) {
    return EFI_ALREADY_STARTED;
  }

  gDebuggerExceptionHandlers[InterruptType] = InterruptHandler;

  return EFI_SUCCESS;
}



VOID
EFIAPI
CommonCExceptionHandler (
  IN     EFI_EXCEPTION_TYPE           ExceptionType,
  IN OUT EFI_SYSTEM_CONTEXT           SystemContext
  )
{
  BOOLEAN Dispatched = FALSE;
  
  if (ExceptionType <= MAX_ARM_EXCEPTION) {
    if (gDebuggerExceptionHandlers[ExceptionType]) {
      //
      // If DebugSupport hooked the interrupt call the handler. This does not disable 
      // the normal handler.
      //
      gDebuggerExceptionHandlers[ExceptionType] (ExceptionType, SystemContext);
      Dispatched = TRUE;
    }
    if (gExceptionHandlers[ExceptionType]) {
      gExceptionHandlers[ExceptionType] (ExceptionType, SystemContext);
      Dispatched = TRUE;
    }
  }

  if (Dispatched) {
    //
    // We did work so this was an expected ExceptionType
    //
    return;
  }
  
  if (ExceptionType == EXCEPT_ARM_SOFTWARE_INTERRUPT) {
    //
    // ARM JTAG debuggers some times use this vector, so it is not an error to get one
    //
    return;
  }

  //
  // Code after here is the default exception handler...
  //
  DEBUG ((EFI_D_ERROR, "Exception %d from %08x\n", ExceptionType, SystemContext.SystemContextArm->PC));
  ASSERT (FALSE);

}



EFI_STATUS
InitializeExceptions (
  IN EFI_CPU_ARCH_PROTOCOL    *Cpu
  )
{
  EFI_STATUS           Status;
  UINTN                Offset;
  UINTN                Length;
  UINTN                Index;
  BOOLEAN              Enabled;
  EFI_PHYSICAL_ADDRESS Base;

  //
  // Disable interrupts
  //
  Cpu->GetInterruptState (Cpu, &Enabled);
  Cpu->DisableInterrupt (Cpu);

  //
  // Initialize the C entry points for interrupts
  //
  for (Index = 0; Index <= MAX_ARM_EXCEPTION; Index++) {
    Status = RegisterInterruptHandler (Index, NULL);
    ASSERT_EFI_ERROR (Status);
    
    Status = RegisterDebuggerInterruptHandler (Index, NULL);
    ASSERT_EFI_ERROR (Status);
  }
  
  //
  // Copy an implementation of the ARM exception vectors to 0x0.
  //
  Length = (UINTN)ExceptionHandlersEnd - (UINTN)ExceptionHandlersStart;

  //
  // Reserve space for the exception handlers
  //
  Base = (EFI_PHYSICAL_ADDRESS)PcdGet32 (PcdCpuVectorBaseAddress);
  Status = gBS->AllocatePages (AllocateAddress, EfiBootServicesCode, EFI_SIZE_TO_PAGES (Length), &Base);
  // If the request was for memory that's not in the memory map (which is often the case for 0x00000000
  // on embedded systems, for example, we don't want to hang up.  So we'll check here for a status of 
  // EFI_NOT_FOUND, and continue in that case.
  if (EFI_ERROR(Status) && (Status != EFI_NOT_FOUND)) {
  ASSERT_EFI_ERROR (Status);
  }

  CopyMem ((VOID *)(UINTN)PcdGet32 (PcdCpuVectorBaseAddress), (VOID *)ExceptionHandlersStart, Length);

  //
  // Patch in the common Assembly exception handler
  //
  Offset = (UINTN)CommonExceptionEntry - (UINTN)ExceptionHandlersStart;
  *(UINTN *) ((UINT8 *)(UINTN)PcdGet32 (PcdCpuVectorBaseAddress) + Offset) = (UINTN)AsmCommonExceptionEntry;

  // Flush Caches since we updated executable stuff
  InvalidateInstructionCacheRange((VOID *)PcdGet32(PcdCpuVectorBaseAddress), Length);

  if (Enabled) {
    // 
    // Restore interrupt state
    //
    Status = Cpu->EnableInterrupt (Cpu);
  }

  return Status;
}