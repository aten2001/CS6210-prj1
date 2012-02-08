################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../rb_tree/misc.c \
../rb_tree/red_black_tree.c \
../rb_tree/stack.c \
../rb_tree/test_red_black_tree.c 

OBJS += \
./rb_tree/misc.o \
./rb_tree/red_black_tree.o \
./rb_tree/stack.o \
./rb_tree/test_red_black_tree.o 

C_DEPS += \
./rb_tree/misc.d \
./rb_tree/red_black_tree.d \
./rb_tree/stack.d \
./rb_tree/test_red_black_tree.d 


# Each subdirectory must supply rules for building sources it contributes
rb_tree/%.o: ../rb_tree/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -DDEBUG -O0 -g3 -pedantic -Wall -c -fmessage-length=0 -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


