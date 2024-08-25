################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (10.3-2021.10)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/Users/samuelmpoloka/nanopb/build-tests/legacy_cmake_relpath/simple.c 

OBJS += \
./nanopb/build-tests/legacy_cmake_relpath/simple.o 

C_DEPS += \
./nanopb/build-tests/legacy_cmake_relpath/simple.d 


# Each subdirectory must supply rules for building sources it contributes
nanopb/build-tests/legacy_cmake_relpath/simple.o: /Users/samuelmpoloka/nanopb/build-tests/legacy_cmake_relpath/simple.c nanopb/build-tests/legacy_cmake_relpath/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -I../LWIP/App -I../LWIP/Target -I../Middlewares/Third_Party/LwIP/src/include -I../Middlewares/Third_Party/LwIP/system -I../Drivers/BSP/Components/lan8742 -I../Middlewares/Third_Party/LwIP/src/include/netif/ppp -I../Middlewares/Third_Party/LwIP/src/include/lwip -I../Middlewares/Third_Party/LwIP/src/include/lwip/apps -I../Middlewares/Third_Party/LwIP/src/include/lwip/priv -I../Middlewares/Third_Party/LwIP/src/include/lwip/prot -I../Middlewares/Third_Party/LwIP/src/include/netif -I../Middlewares/Third_Party/LwIP/src/include/compat/posix -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/net -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/sys -I../Middlewares/Third_Party/LwIP/src/include/compat/stdc -I../Middlewares/Third_Party/LwIP/system/arch -I../Middlewares/Third_Party/LwIP/src/apps/http -I/Users/samuelmpoloka/nanopb -I/Users/samuelmpoloka/nanopb/generator -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-nanopb-2f-build-2d-tests-2f-legacy_cmake_relpath

clean-nanopb-2f-build-2d-tests-2f-legacy_cmake_relpath:
	-$(RM) ./nanopb/build-tests/legacy_cmake_relpath/simple.d ./nanopb/build-tests/legacy_cmake_relpath/simple.o ./nanopb/build-tests/legacy_cmake_relpath/simple.su

.PHONY: clean-nanopb-2f-build-2d-tests-2f-legacy_cmake_relpath

