################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_button.c \
../Core/Src/app_cmd_rx.c \
../Core/Src/app_hal_callbacks.c \
../Core/Src/app_protocol.c \
../Core/Src/app_ring_buffer.c \
../Core/Src/app_rtos.c \
../Core/Src/app_tasks.c \
../Core/Src/app_timestamp.c \
../Core/Src/freertos.c \
../Core/Src/main.c \
../Core/Src/stm32f7xx_hal_msp.c \
../Core/Src/stm32f7xx_hal_timebase_tim.c \
../Core/Src/stm32f7xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f7xx.c 

OBJS += \
./Core/Src/app_button.o \
./Core/Src/app_cmd_rx.o \
./Core/Src/app_hal_callbacks.o \
./Core/Src/app_protocol.o \
./Core/Src/app_ring_buffer.o \
./Core/Src/app_rtos.o \
./Core/Src/app_tasks.o \
./Core/Src/app_timestamp.o \
./Core/Src/freertos.o \
./Core/Src/main.o \
./Core/Src/stm32f7xx_hal_msp.o \
./Core/Src/stm32f7xx_hal_timebase_tim.o \
./Core/Src/stm32f7xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f7xx.o 

C_DEPS += \
./Core/Src/app_button.d \
./Core/Src/app_cmd_rx.d \
./Core/Src/app_hal_callbacks.d \
./Core/Src/app_protocol.d \
./Core/Src/app_ring_buffer.d \
./Core/Src/app_rtos.d \
./Core/Src/app_tasks.d \
./Core/Src/app_timestamp.d \
./Core/Src/freertos.d \
./Core/Src/main.d \
./Core/Src/stm32f7xx_hal_msp.d \
./Core/Src/stm32f7xx_hal_timebase_tim.d \
./Core/Src/stm32f7xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f7xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F746xx -c -I../Core/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_button.cyclo ./Core/Src/app_button.d ./Core/Src/app_button.o ./Core/Src/app_button.su ./Core/Src/app_cmd_rx.cyclo ./Core/Src/app_cmd_rx.d ./Core/Src/app_cmd_rx.o ./Core/Src/app_cmd_rx.su ./Core/Src/app_hal_callbacks.cyclo ./Core/Src/app_hal_callbacks.d ./Core/Src/app_hal_callbacks.o ./Core/Src/app_hal_callbacks.su ./Core/Src/app_protocol.cyclo ./Core/Src/app_protocol.d ./Core/Src/app_protocol.o ./Core/Src/app_protocol.su ./Core/Src/app_ring_buffer.cyclo ./Core/Src/app_ring_buffer.d ./Core/Src/app_ring_buffer.o ./Core/Src/app_ring_buffer.su ./Core/Src/app_rtos.cyclo ./Core/Src/app_rtos.d ./Core/Src/app_rtos.o ./Core/Src/app_rtos.su ./Core/Src/app_tasks.cyclo ./Core/Src/app_tasks.d ./Core/Src/app_tasks.o ./Core/Src/app_tasks.su ./Core/Src/app_timestamp.cyclo ./Core/Src/app_timestamp.d ./Core/Src/app_timestamp.o ./Core/Src/app_timestamp.su ./Core/Src/freertos.cyclo ./Core/Src/freertos.d ./Core/Src/freertos.o ./Core/Src/freertos.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32f7xx_hal_msp.cyclo ./Core/Src/stm32f7xx_hal_msp.d ./Core/Src/stm32f7xx_hal_msp.o ./Core/Src/stm32f7xx_hal_msp.su ./Core/Src/stm32f7xx_hal_timebase_tim.cyclo ./Core/Src/stm32f7xx_hal_timebase_tim.d ./Core/Src/stm32f7xx_hal_timebase_tim.o ./Core/Src/stm32f7xx_hal_timebase_tim.su ./Core/Src/stm32f7xx_it.cyclo ./Core/Src/stm32f7xx_it.d ./Core/Src/stm32f7xx_it.o ./Core/Src/stm32f7xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f7xx.cyclo ./Core/Src/system_stm32f7xx.d ./Core/Src/system_stm32f7xx.o ./Core/Src/system_stm32f7xx.su

.PHONY: clean-Core-2f-Src

