/**
 * @file
 *
 * @author Alazar Technologies Inc
 *
 * @copyright Copyright (c) 2006-2022 Alazar Technologies Inc. All Rights
 * Reserved.  Unpublished - rights reserved under the Copyright laws
 * of the United States And Canada.
 * This product contains confidential information and trade secrets
 * of Alazar Technologies Inc. Use, disclosure, or reproduction is
 * prohibited without the prior express written permission of Alazar
 * Technologies Inc
 *
 * This file defines all the error codes for the AlazarTech SDK
 */
#ifndef __ALAZARERROR_H
#define __ALAZARERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @cond INTERNAL_DECLARATIONS
 */
#define API_RETURN_CODE_STARTS 0x200 /* Starting return code */
/**
 *  @endcond
 */

/**
 *  @brief API functions return codes.
 */
enum RETURN_CODE {
    /// <c>512</c>  The operation completed without error
    ApiSuccess = API_RETURN_CODE_STARTS,

    /// <c>513</c>  The operation failed
    ApiFailed = 513,

    /// <c>514</c>  Access denied
    ApiAccessDenied = 514,

    /// <c>515</c>  Channel selection is unavailable
    ApiDmaChannelUnavailable = 515,

    /// <c>516</c>  Channel selection in invalid
    ApiDmaChannelInvalid = 516,

    /// <c>517</c>  Channel selection is invalid
    ApiDmaChannelTypeError = 517,

    /// <c>518</c>  A data transfer is in progress. This error code indicates
    /// that the
    /// current action cannot be performed while an acquisition is in progress.
    /// It also returned by AlazarPostAsyncBuffer() if this function is called
    /// with an invalid DMA buffer.
    ApiDmaInProgress = 518,

    /// <c>519</c>  DMA transfer is finished
    ApiDmaDone = 519,

    /// <c>520</c>  DMA transfer was paused
    ApiDmaPaused = 520,

    /// <c>521</c>  DMA transfer is not paused
    ApiDmaNotPaused = 521,

    /// <c>522</c>  A DMA command is invalid
    ApiDmaCommandInvalid = 522,

    /// <c>531</c>  One of the parameters of the function is NULL and should not
    /// be
    ApiNullParam = 531,

    /// <c>533</c>  This function is not supported by the API. Consult the
    /// manual for more
    /// information.
    ApiUnsupportedFunction = 533,

    /// <c>534</c>  Invalid PCI space
    ApiInvalidPciSpace = 534,

    /// <c>535</c>  Invalid IOP space
    ApiInvalidIopSpace = 535,

    /// <c>536</c>  Invalid size passed as argument to the function
    ApiInvalidSize = 536,

    /// <c>537</c>  Invalid address
    ApiInvalidAddress = 537,

    /// <c>538</c>  Invalid access type requested
    ApiInvalidAccessType = 538,

    /// <c>539</c>  Invalid index
    ApiInvalidIndex = 539,

    /// <c>543</c>  Invalid register
    ApiInvalidRegister = 543,

    /// <c>550</c>  Access for configuration failed
    ApiConfigAccessFailed = 550,

    /// <c>551</c>  Invalid device information
    ApiInvalidDeviceInfo = 551,

    /// <c>552</c>  No active driver for the board. Please ensure that a driver
    /// is installed
    ApiNoActiveDriver = 552,

    /// <c>553</c>  There were not enough system resources to complete this
    /// operation. The
    /// most common reason of this return code is using too many DMA buffers, or
    /// using DMA buffers that are too big. Please try reducing the number of
    /// buffers posted to the board at any time, and/or try reducing the DMA
    /// buffer sizes.
    ApiInsufficientResources = 553,

    /// <c>556</c>  The API has not been properly initialized for this function
    /// call. Please
    /// review one of the code samples from the ATS-SDK to confirm that API
    /// calls are made in the right order.
    ApiNotInitialized = 556,

    /// <c>558</c>  Power state requested is not valid
    ApiInvalidPowerState = 558,

    /// <c>559</c>  The operation cannot be completed because the device is
    /// powered down.
    /// For example, this error code is output if the computer enters
    /// hiberanation while an acquisition is running.
    ApiPowerDown = 559,

    /// <c>561</c>  The API call is not valid with this channel selection.
    ApiNotSupportThisChannel = 561,

    /// <c>562</c>  The function has requested no action to be taken
    ApiNoAction = 562,

    /// <c>563</c>  HotSwap is not supported
    ApiHSNotSupported = 563,

    /// <c>565</c>  Vital product data not enabled
    ApiVpdNotEnabled = 565,

    /// <c>567</c>  Offset argument is not valid
    ApiInvalidOffset = 567,

    /// <c>569</c>  Timeout on the PCI bus
    ApiPciTimeout = 569,

    /// <c>572</c>  Invalid handle passed as argument
    ApiInvalidHandle = 572,

    /// <c>573</c>  The buffer passed as argument is not ready to be called with
    /// this API.
    /// This error code is most often seen is the order of buffers posted to the
    /// board is not respected when querying them.
    ApiBufferNotReady = 573,

    /// <c>574</c>  Generic invalid parameter error. Check the function's
    /// documentation for
    /// more information about valid argument values.
    ApiInvalidData = 574,

    /// <c>575</c>
    ApiDoNothing = 575,

    /// <c>576</c>  Unable to lock buffer and build SGL list
    ApiDmaSglBuildFailed = 576,

    /// <c>577</c>  Power management is not supported
    ApiPMNotSupported = 577,

    /// <c>578</c>  Invalid driver version
    ApiInvalidDriverVersion = 578,

    /// <c>579</c>  The operation did not finish during the timeout interval.
    /// try the
    /// operation again, or abort the acquisition.
    ApiWaitTimeout = 579,

    /// <c>580</c>  The operation was cancelled.
    ApiWaitCanceled = 580,

    /// <c>581</c>  The buffer used is too small. Try increasing the buffer
    /// size.
    ApiBufferTooSmall = 581,

    /// <c>582</c>  The board overflowed its internal (on-board) memory. Try
    /// reducing the
    /// sample rate, reducing the number of enabled channels. Also ensure that
    /// DMA buffer size is between 1 MB and 8 MB.
    ApiBufferOverflow = 582,

    /// <c>583</c>  The buffer passed as argument is not valid.
    ApiInvalidBuffer = 583,

    /// <c>584</c>  The number of reocrds per buffer passed as argument is
    /// invalid.
    ApiInvalidRecordsPerBuffer = 584,

    /// <c>585</c>  An asynchronous I/O operation was successfully started on
    /// the
    /// board. It will be completed when sufficient trigger events are supplied
    /// to the board to fill the buffer.
    ApiDmaPending,

    /// <c>586</c>  The buffer is too large for the driver or operating system
    /// to prepare
    /// for scatter-gather DMA transfer. Try reducing the size of each buffer,
    /// or reducing the number of buffers queued by the application.
    ApiLockAndProbePagesFailed = 586,

    // The buffer requires too many descriptors for DMA. Try reducing the size
    // of each bueffer.
    ApiMaxDescriptorsExceeded = 587,

    // The buffer length is not a multiple of the burst length. Specify a buffer
    // length that is a multiple of the burst length or change the burst length.
    ApiInvalidBufferBurstLengthAlignment = 588,

    /// This buffer is the last in the current acquisition
    /// <c>589</c>  This buffer is the last in the current acquisition
    ApiTransferComplete = 589,

    /// <c>590</c>  The on-board PLL circuit could not lock. If the acquisition
    /// used an
    /// internal sample clock, this might be a symptom of a hardware problem;
    /// contact AlazarTech. If the acquisition used an external 10 MHz PLL
    /// signal, please make sure that the signal is fed in properly.
    ApiPllNotLocked = 590,

    /// <c>591</c>  The requested acquisition is not possible with two channels.
    /// This can be
    /// due to the sample rate being too fast for DES boards, or to the number
    /// of samples per record being too large. Try reducing the number of
    /// samples per channel, or switching to single channel mode.
    ApiNotSupportedInDualChannelMode = 591,

    /// <c>592</c>  The requested acquisition is not possible with four
    /// channels. This can
    /// be due to the sample rate being too fast for DES boards, or to the
    /// number of samples per record being too large. Try reducing the number of
    /// samples per channel, or switching to single channel mode.
    ApiNotSupportedInQuadChannelMode = 592,

    /// <c>593</c>  A file read or write error occured.
    ApiFileIoError = 593,

    /// <c>594</c>  The requested ADC clock frequency is not supported.
    ApiInvalidClockFrequency = 594,

    /// <c>595</c>  Invalid skip table passed as argument
    ApiInvalidSkipTable = 595,

    /// <c>596</c>  This DSP module is not valid for the current operation.
    ApiInvalidDspModule = 596,

    /// <c>597</c>  Dual-edge sampling mode is only supported in signel-channel
    /// mode. Try
    /// disabling dual-edge sampling (lowering the sample rate if using internal
    /// clock), or selecting only one channel.
    ApiDESOnlySupportedInSingleChannelMode = 597,

    /// <c>598</c>  Successive API calls of the same acuqiisiton have received
    /// inconsistent
    /// acquisition channel masks.
    ApiInconsistentChannel = 598,

    /// <c>599</c>  DSP acquisition was run with a finite number of records per
    /// acqusiition.
    /// Set this value to inifinite.
    ApiDspFiniteRecordsPerAcquisition = 599,

    /// <c>600</c>  Not enough NPT footers in the buffer for extraction
    ApiNotEnoughNptFooters = 600,

    /// <c>601</c>  Invalid NPT footer found
    ApiInvalidNptFooter = 601,

    /// <c>602</c>  OCT ignore bad clock is not supported
    ApiOCTIgnoreBadClockNotSupported = 602,

    /// <c>603</c>  The requested number of records in a single-port acquisition
    /// exceeds the
    /// maximum supported by the digitizer. Use dual-ported AutoDMA to acquire
    /// more records per acquisition.
    ApiError1 = 603,

    /// <c>604</c>  The requested number of records in a single-port acquisition
    /// exceeds the maximum supported by the digitizer.
    ApiError2 = 604,

    /// <c>605</c>  No trigger is detected as part of the OCT ignore bad clock
    /// feature.
    ApiOCTNoTriggerDetected = 605,

    /// <c>606</c>  Trigger detected is too fast for the OCT ignore bad clock
    /// feature.
    ApiOCTTriggerTooFast = 606,

    /// <c>607</c>  There was a network-related issue. Make sure that the
    /// network connection
    /// and settings are correct.
    ApiNetworkError = 607,

    /// <c>608</c>  On-FPGA FFT cannot support FFT that large. Try reducing the
    /// FFT
    /// size, or querying the maximum FFT size with AlazarDSPGetInfo()
    ApiFftSizeTooLarge = 608,

    /// <c>609</c>  GPU returned an error. See log for more information
    ApiGPUError = 609,

    /// <c>610</c>  This board only supports this acquisition mode in FIFO only
    /// streaming
    /// mode. Please set the \ref ADMA_FIFO_ONLY_STREAMING flag in
    /// AlazarBeforeAsyncRead().
    ApiAcquisitionModeOnlySupportedInFifoStreaming = 610,

    /// <c>611</c>  This board does not support sample interleaving in
    /// traditional
    /// acquisition mode. Please refer to the SDK guide for more information.
    ApiInterleaveNotSupportedInTraditionalMode = 611,

    /// <c>612</c>  This board does not support record headers. Please refer to
    /// the SDK
    /// guide for more information.
    ApiRecordHeadersNotSupported = 612,

    /// <c>613</c>  This board does not support record footers. Please refer to
    /// the SDK
    /// guide for more information.
    ApiRecordFootersNotSupported = 613,

    /// <c>614</c>  The number of different DMA buffers posted exceeds the limit
    /// set with
    /// AlazarConfigureFastBufferLock(). Either disable fast buffer locking, or
    /// confirm that the value passed to AlazarConfigureFastBufferLock() is
    /// respected.
    ApiFastBufferLockCountExceeded = 614,

    /// <c>615</c>  The operation could not complete because the system is in an
    /// invalid
    /// state. You may safely retry the call that returned this error.
    ApiInvalidStateDoRetry = 615,

    /// <c>616</c>  The operation could not complete because the system is in an
    /// invalid
    /// state. You may safely retry the call that returned this error.
    ApiInvalidInputRange = 616,

    /// <c>617</c>  The operation could not complete because the system is in a
    /// busy
    /// state. You may safely retry the call that returned this error.
    ApiBusy = 617,

    /// <c>618</c>  Under Linux, ATSApi looks for an `ALAZARRCPATH` or a `HOME`
    /// environment
    /// variable to know where to store the .alazarrc configuration file. This
    /// error indicates that none of these variables were identified.
    ApiNoRcPathVariable = 618,

    /// <c>619</c>  This board requires an option to be active to perform
    /// on-FPGA FFTs.
    ApiOnFpgaFftOptionMissing = 619,

    /// <c>620</c>  The PLL of the reference clock was not locked.
    ApiReferenceClockPllNotLocked = 620,

    /// <c>621</c>  The JESD link PLL on ADC is not locked.
    ApiAdcJesdPllNotLocked = 621,

    /// <c>622</c>  The JESD link on ADC is not locked.
    ApiAdcJesdLinkNotLocked = 622,

    /// <c>623</c>  The JESD link on FPGA is not locked.
    ApiFpgaJesdLinkNotLocked = 623,

    /// <c>624</c>  The library does not support FPGA versions under 4.07 for
    /// ATS9364.
    /// Frimware must be updated.
    ApiInvalidFirmwareForATS9364 = 624,

    /// <c>625</c>  The hardware is no longer detected in the system.
    ApiHardwareRemoved = 625,

    /// <c>626</c>  The library does not support FPGA versions under 6.0 for
    /// ATS4001.
    /// Frimware must be updated.
    ApiInvalidFirmwareForATS4001 = 626,

    /// <c>627</c>  Invalid license for running the desired software.
    ApiInvalidLicense = 627,

    /// <c>628</c>  Error with the galvo controller.
    ApiGalvoError = 628,

    /// @cond INTERNAL_DECLARATIONS
    /// --------- Add error codes above this comment --------
    RETURN_CODE_LAST,
    ApiLastError = RETURN_CODE_LAST,

    /// @{ Unused return code
    ApiDmaManReady = 523,
    ApiDmaManNotReady = 524,
    ApiDmaInvalidChannelPriority = 525,
    ApiDmaManCorrupted = 526,
    ApiDmaInvalidElementIndex = 527,
    ApiDmaNoMoreElements = 528,
    ApiDmaSglInvalid = 529,
    ApiDmaSglQueueFull = 530,
    ApiInvalidBusIndex = 532,
    ApiMuNotReady = 540,
    ApiMuFifoEmpty = 541,
    ApiMuFifoFull = 542,
    ApiDoorbellClearFailed = 544,
    ApiInvalidUserPin = 545,
    ApiInvalidUserState = 546,
    ApiEepromNotPresent = 547,
    ApiEepromTypeNotSupported = 548,
    ApiEepromBlank = 549,
    ApiObjectAlreadyAllocated = 554,
    ApiAlreadyInitialized = 555,
    ApiBadConfigRegEndianMode = 557,
    ApiFlybyNotSupported = 560,
    ApiVPDNotSupported = 564,
    ApiNoMoreCap = 566,
    ApiBadPinDirection = 568,
    ApiDmaChannelClosed = 570,
    ApiDmaChannelError = 571,
    /// @}

    /// @endcond
};

/// @cond INTERNAL_DECLARATIONS
typedef enum RETURN_CODE RETURN_CODE;
/// @endcond

#ifdef __cplusplus
}
#endif

#endif //__ALAZARERROR_H
