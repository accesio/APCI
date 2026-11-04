#include "AIOAIO.h"

_Static_assert(sizeof(TUInt8) == 1, "8-bit API type");
_Static_assert(sizeof(TUInt16) == 2, "16-bit API type");
_Static_assert(sizeof(TUInt32) == 4, "32-bit API type");
_Static_assert(sizeof(TUInt64) == 8, "64-bit API type");
_Static_assert(sizeof(TBoardIndex) == 8, "board index ABI");
_Static_assert(sizeof(TUNative) == 4, "legacy native count remains 32-bit");
_Static_assert(sizeof(TWideChar) == 2, "UTF-16 ABI");
_Static_assert(sizeof(TLongBool) == 4, "boolean ABI");

/* Function pointer assignments check the board-oriented C contract without
 * calling the library or accessing a board. */
TWinErrCode (*aioaio_c_rate)(TBoardIndex, TDouble) = ADC_SetRate;
TWinErrCode (*aioaio_c_scan)(TBoardIndex, TPUInt32, TRangeCode) = ADC_GetImmediateScanRaw;
TWinErrCode (*aioaio_c_block)(TBoardIndex, TUNative) = ADC_AcquireBlock;
TWinErrCode (*aioaio_c_get)(TBoardIndex, TUInt32, TPUInt32, TPUInt32) = ADC_GetDataRaw;
