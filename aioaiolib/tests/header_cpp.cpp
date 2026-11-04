#include "AIOAIO.h"
#include <type_traits>

static_assert(std::is_same_v<TWideChar, uint16_t>, "UTF-16 stays independent of wchar_t");
static_assert(std::is_same_v<TUNative, uint32_t>, "fixed count ABI");

/* Compile the overloads and default arguments, but never execute them. */
void aioaio_compile_cpp_convenience(TBoardIndex board, double *volts,
                                    uint32_t *raw, uint32_t *got)
{
    ADC_SetRange(board);
    ADC_SetDifferential(board);
    ADC_GetImmediateScan(board, volts);
    ADC_GetImmediateScan(board, raw);
    ADC_GetData(board, 1, got, volts);
    ADC_GetData(board, 1, got, raw);
    DAC_Output(board, 0, 1.0);
    DAC_Output(board, 0, TUInt64{1});
}
