# Clocks STM32G474 build

This folder contains a bare-metal STM32G474 compile target for the Clocks
module using the STM32 backend from JaszczurHAL.

Configure and build from the Clocks directory:

```bash
cmake -S STM32 -B .build/stm32 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/../../../libraries/JaszczurHAL/stm32_lib/toolchain_stm32g474.cmake"

cmake --build .build/stm32 -j$(nproc)
```

Outputs:

- `.build/stm32/Clocks_STM32G474.elf`
- `.build/stm32/Clocks_STM32G474.bin`
- `.build/stm32/Clocks_STM32G474.hex`

The current `hardwareConfig.h` pin map is intentionally reused. It is enough to
prove that the module compiles, but it is not a validated STM32G474 board pin
assignment.
