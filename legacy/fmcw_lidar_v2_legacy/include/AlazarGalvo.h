/**
 * @file
 *
 * @author Alazar Technologies Inc
 *
 * @copyright Copyright (c) 2006-2023 Alazar Technologies Inc. All Rights
 * Reserved.  Unpublished - rights reserved under the Copyright laws
 * of the United States And Canada.
 * This product contains confidential information and trade secrets
 * of Alazar Technologies Inc. Use, disclosure, or reproduction is
 * prohibited without the prior express written permission of Alazar
 * Technologies Inc
 */

#ifndef ALAZARGALVO_H
#define ALAZARGALVO_H

#if defined(__cplusplus)
#    define EXTERN_C_BEGIN extern "C" {
#    define EXTERN_C_END }
#else
#    define EXTERN_C_BEGIN
#    define EXTERN_C_END
#endif

#ifndef EXPORT
#    define EXPORT
#endif

#include "AlazarApi.h"

#include "stdint.h"

EXTERN_C_BEGIN

/// \brief Setup Galvo output ramps to be used during an acquisiton.
///
/// This Galvo API is designed to be used instead of other Galvo APIs. Using
/// other Galvo APIs between calls to AlazarBeforeAsyncRead and
/// AlazarAbortAsyncRead can lead to undefined behavior.
///
/// This call must be made before calling AlazarBeforeAsyncRead.
///
/// Galvo module will start outputting ramps when AlazarStartCapture is called
/// and stop when AlazarAbortAsyncRead is called.
///
/// User should call AlazarConfigureAuxIO with mode AUX_IN_TRIGGER_ENABLE. Doing
/// so, the acquistion of each buffer will be synchronized to the BFrameIndex
/// within the Gavlo ramp.
///
/// If the acquisition duration is greater than PointsPerCScan, the Galvo module
/// will loop back to the start of the C-Scan.
///
/// Using this API will internally update the Galvo pattern memory when user
/// calls AlazarwaitAsyncBufferComplete. It is important that the cumulative
/// number of written Galvo ramps remains greater than the total number of
/// captured records to ensure valid ramps. If user does not call
/// AlazarWaitAsyncBufferComplete at sufficient rate, an ApiGalvoError will be
/// generated.
///
/// Call AlazarGalvoSetAcquisitionRamps(NULL,NULL,0,0,0); to revert to normal
/// usage.
///
/// \param handle          Handle to board
/// \param xData           Pointer to U16 data to be outputed on Galvo X output
/// \param yData           Pointer to U16 data to be outputed on Galvo Y output
/// \param PointsPerRamp   Number of galvo positions per ramp
/// \param PointsPerCScan  Number of galvo positions per C-scan
/// \param BFrameIndex     B-Frame index in ramp. PointsPerRamp - BFrameIndex
///                        must be equal to recordsPerBuffer defined in \ref
///                        AlazarBeforeAsyncRead
///
///  @returns A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoSetAcquisitionRamps(
    HANDLE handle, const uint16_t *xData, const uint16_t *yData,
    U32 PointsPerRamp, U32 PointsPerCScan, U32 BFrameIndex);

/// \brief Identifies a slot in pattern memory
typedef enum {
    GALVO_PATTERN_SLOT_1 = 1, ///< <c>1</c>
    GALVO_PATTERN_SLOT_2 = 2, ///< <c>2</c>
    GALVO_PATTERN_SLOT_3 = 3, ///< <c>3</c>
    GALVO_PATTERN_SLOT_4 = 4, ///< <c>4</c>
    GALVO_PATTERN_SLOT_5 = 5, ///< <c>5</c>
} GALVO_PATTERN_SLOT;

/// \brief Writes data into a slot in pattern memory
///
/// \param handle        Handle to board
/// \param slot          Slot to write data to
/// \param data          Pointer to an array of 32-bit words to write to the
///                      slot
/// \param size_words   Number of 32-bit words to write to the memory slot
/// \param offset_words  Offset in 32-bit words from the start of the memory
///                      slot
///
/// \returns A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPatternSlotWrite(HANDLE handle,
                                               GALVO_PATTERN_SLOT slot,
                                               const uint32_t *data,
                                               int32_t size_words,
                                               int32_t offset_words);

/// \brief Reads data from a slot in pattern memory
///
/// \param handle      Handle to board
/// \param slot        Slot to read data from
/// \param data        Pointer to an array that the function will write data to
/// \param size_words  Number of 32-bit words to read from the memory slot
///
/// \returns A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPatternSlotRead(HANDLE handle,
                                              GALVO_PATTERN_SLOT slot,
                                              uint32_t *data,
                                              int32_t size_words);

/// \brief Returns the number of sequences in sequence memory supported by a
/// given device.
///
/// \param handle Handle to board to query the information for
/// \param count  Number of sequences supported by the device
///
/// \returns A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoSequenceGetCount(HANDLE handle, int32_t *count);

/// \brief Writes a sequence to sequency memory
///
/// \param handle         Handle to board
/// \param seq_idx        0-based index of the sequence to write
/// \param slot           Pattern memory slot that this sequence should play
/// \param repetitions    Number of times that the sequence should repeat the
///                       slot. Set to zero for infinite repetitions.
/// \param start_idx      First index to play from the slot.
/// \param bframe_idx     Index of the start of B-Frame.
/// \param end_idx        Last index to play from the slot.
/// \param record_length  The record length to use at this sequence index.
/// \param is_last        Set to true if this sequence is the last in the list
///                       of sequences to play.
/// \param starts_in_park  Set to `true` if this sequence's pattern starts at
///                        the park position
/// \param ends_in_park   Set to `true` if this sequence's pattern ends at the
///                       park position
///
/// \note  The following must hold for the sequence to be valid: `start_idx <=
///        bframe_idx <= end_idx`.
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoSequenceWrite(
    HANDLE handle, int32_t seq_idx, GALVO_PATTERN_SLOT slot,
    int32_t repetitions, int32_t start_idx, int32_t bframe_idx, int32_t end_idx,
    int32_t record_length, bool is_last, bool starts_in_park,
    bool ends_in_park);

/// \brief Reads a sequence from sequence memory
///
/// \param handle         Handle to board
/// \param seq_idx        0-based index of the sequence to read
/// \param slot           Pattern memory slot that this sequence should play
/// \param repetitions    Number of times that the sequence should repeat the
///                       slot. Zero means "infinite repetitions".
/// \param start_idx      First index to play from the slot.
/// \param bframe_idx     Index of the start of B-Frame.
/// \param end_idx        Last index to play from the slot.
/// \param record_length  The record length used at this sequence index
/// \param is_last        True if this sequence is the last in the list of
///                       sequences to play.
/// \param starts_in_park  True if this sequence's pattern starts at the park
///                        position
/// \param ends_in_park   True if this sequence's pattern ends at the park
///                       position
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoSequenceRead(
    HANDLE handle, int32_t seq_idx, GALVO_PATTERN_SLOT *slot,
    int32_t *repetitions, int32_t *start_idx, int32_t *bframe_idx,
    int32_t *end_idx, int32_t *record_length, bool *is_last,
    bool *starts_in_park, bool *ends_in_park);

/// \brief Queries the size of a pattern memory slot
///
/// \param handle      Handle to board
/// \param slot        The slot to query the size of
/// \param size_words  Outputs the size of the slot in words
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoSlotGetSize(HANDLE handle,
                                          GALVO_PATTERN_SLOT slot,
                                          int32_t *size_words);

/// \brief Starts the playback of sequences. The first sequence to be played is
/// at index zero.
///
/// \param handle       Handle to board
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPlaybackStart(HANDLE handle);

/// \brief "Jumps" playback to the specified sequence. Can be called only when
/// playback is currently running.
///
/// \param handle       Handle to board
/// \param seq_idx      Sequence to jump to
///
/// \note  This function should only be called when playback is running on an
///        infinite repetition sequence, otherwise results may be unpredictable.
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPlaybackJumpToSequence(HANDLE handle,
                                                     int32_t seq_idx);

/// \brief Returns the index of the sequence that is currently being played.
///
/// \param handle       Handle to board
/// \param seq_idx      Return parameter for the sequence index
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPlaybackGetCurrentSequence(HANDLE handle,
                                                         int32_t *seq_idx);

/// \brief Stops playback of sequences.
///
/// \param handle       Handle to board
///
/// \return A ::RETURN_CODE return code indicating success or failure.
RETURN_CODE EXPORT AlazarGalvoPlaybackStop(HANDLE handle);

/// \brief Sets the current park position.
///
/// \param handle   Handle to board
/// \param x  Position on the X axis
/// \param y  Position on the Y axis
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoSetParkPosition(HANDLE handle, uint16_t x,
                                              uint16_t y);

/// \brief Queries the current park position.
///
/// \param handle   Handle to board
/// \param x Pass a pointer to an unsigned 16-bit integer. The function sets
///          this value to the current position on the X axis.
/// \param y Pass a pointer to an unsigned 16-bit integer. The function sets
///          this value to the current position on the Y axis.
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoGetParkPosition(HANDLE handle, uint16_t *x,
                                              uint16_t *y);

/// \brief Updates the `starts_in_park` and `ends_in_park` flags of a device's
/// sequence memory
///
/// This function checks the pattern memory element that corresponds to the
/// first and last points of each sequence, and sets the `starts_in_park` and
/// `ends_in_park` flag if these points equals the park device's park position.
///
/// \see ::AlazarGalvoGetParkPosition
///
/// \param handle                Handle to board
/// \param modified_flags_count  Outputs the number of flags that have been
///                              modified by this function
/// \param total_flags_count     Outputs the total number of flags that are set
///                              across the entire sequence memory.
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoUpdateParkFlags(HANDLE handle,
                                              int32_t *modified_flags_count,
                                              int32_t *total_flags_count);

/// \brief Runs a set of tests on the sequence memory of a given device
///
/// This function is a helper meant to be called after pattern memory and
/// sequence memory have been fully initialized, just before starting the
/// acquisition. This function returns ::ApiSuccess if all the tests pass.
/// Otherwise, it returns an error, and prints more information in the ATSApi
/// log file.
///
/// In what follows, start, B-frame and end indices are noted \f$s_i\f$,
/// \f$b_i\f$ and \f$e_i\f$ respectively.
///
/// At the moment, this function tests the following:
///  - all the `ends_in_park` flag are set appropriately.
///  - all the `starts_in_park` flag are set appropriately.
///  - there is at least one sequence marked `is_last`.
///  - every sequence marked `is_last` also has `ends_in_park` set.
///  - all *active* sequences (i.e. sequences where \f$b_i \in [s_i, e_i]\f$ and
///    with at least one repetiton)
///    respect \f$e_i \geq s_i + 16\f$.
///  - For all active sequences, \f$b_i\f$ and \f$e_i\f$ are inferior to the
///    sequence's slot size.
///  - For all active sequences, \f$b_i \neq s_i\f$.
///
/// \param handle  Target Handle to board
RETURN_CODE EXPORT AlazarGalvoValidateSequenceMemory(HANDLE handle);

/// \brief Reads the value of the feedback ADCs on a board
///
/// The trigger of the device must be running for the ADC to collect data.
///
/// This function should not be used to query the feedback ADC values during an
/// acquisition, because the exact time at which the values returned were
/// sampled is not well-defined. During an acquisition, the record footers of
/// the acquired data should be used instead.
///
/// \param handle Handle to board
/// \param x      Outputs the X feedback ADC value
/// \param y      Outputs the Y feedback ADC value
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoFeedbackAdcRead(HANDLE handle, int32_t *x,
                                              int32_t *y);

/// \brief Reads the current analog ADC output values
///
/// This function is useful durring application development and for testing
/// purposes to query the state that the analog DACs are currently at.
///
/// \param handle Handle to board
/// \param x      Outputs the X analog DAC value
/// \param y      Outputs the Y analog DAC value
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoGetCurrentAnalogOutput(HANDLE handle, uint32_t *x,
                                                     uint32_t *y);

/// Represents the state of the B-scan mode of a board.
typedef enum {
    GALVO_BSCAN_MODE_OFF = 0, ///< <c>0</c>  Activates the B-scan mode
    GALVO_BSCAN_MODE_ON = 1,  ///< <c>1</c>  Deactivates the B-scan mode
} GALVO_BSCAN_MODE;

/// \brief Sets the B-scan mode for a board
///
/// \param handle Handle to board
/// \param mode   B-scan mode to select.
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoBscanModeSet(HANDLE handle,
                                           GALVO_BSCAN_MODE mode);

/// \brief Queries the B-scan mode of a board
///
/// \param handle Handle to board
/// \param mode   Outputs the mode of the device
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoBscanModeGet(HANDLE handle,
                                           GALVO_BSCAN_MODE *mode);

/// Describes the mode used by a board to determine the number of
/// A-lines that constitute a B-scan.
///
/// The standard technique corresponds to the traditional
/// `AUX_IN_TRIGGER_ENABLE` mode of the ATSApi. In this mode, the value for
/// `recordsPerBuffer` passed to AlazarBeforeAsyncRead() is used to determine
/// the number of A-lines per B-scan.
///
/// The custom technique is used for more advanced configurations, where the
/// value passed to AlazarBeforeAsyncRead() and the actual number of A-lines per
/// B-scan need to differ.
typedef enum {
    GALVO_ALINES_PER_BSCAN_MODE_STANDARD = 0, ///< <c>0</c>
    GALVO_ALINES_PER_BSCAN_MODE_CUSTOM = 1,   ///< <c>1</c>
} GALVO_ALINES_PER_BSCAN_MODE;

/// \brief Sets the A-lines per B-scan mode, along with the custom count.
///
/// \param handle            Handle to board
/// \param mode              Mode used by the board to count A-lines in B-scans
/// \param alines_per_bscan  Count of A-lines per B-scan that gets used if the
///                          selected mode is custom
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT AlazarGalvoAlinesPerBscanSet(
    HANDLE handle, GALVO_ALINES_PER_BSCAN_MODE mode, int32_t alines_per_bscan);

/// \brief Gets the A-lines per B-scan mode, along with the custom count.
///
/// \param handle            Handle to board
/// \param mode              Outputs the mode used by the board to count A-lines
///                          in B-scans
/// \param alines_per_bscan  Outputs the Count of A-lines per B-scan that gets
///                          used if the selected mode is custom
///
/// \return A ::RETURN_CODE return code indicating success or failure
RETURN_CODE EXPORT
AlazarGalvoAlinesPerBscanGet(HANDLE handle, GALVO_ALINES_PER_BSCAN_MODE *mode,
                             int32_t *alines_per_bscan);

EXTERN_C_END

#endif // ALAZARGALVO_H
