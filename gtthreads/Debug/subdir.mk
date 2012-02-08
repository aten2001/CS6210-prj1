################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../gt_kthread.c \
../gt_pq.c \
../gt_scheduler.c \
../gt_scheduler_cfs.c \
../gt_scheduler_pcs.c \
../gt_scheduler_switcher.c \
../gt_signal.c \
../gt_spinlock.c \
../gt_thread.c \
../gt_uthread.c \
../gt_uthread_attr.c 

OBJS += \
./gt_kthread.o \
./gt_pq.o \
./gt_scheduler.o \
./gt_scheduler_cfs.o \
./gt_scheduler_pcs.o \
./gt_scheduler_switcher.o \
./gt_signal.o \
./gt_spinlock.o \
./gt_thread.o \
./gt_uthread.o \
./gt_uthread_attr.o 

C_DEPS += \
./gt_kthread.d \
./gt_pq.d \
./gt_scheduler.d \
./gt_scheduler_cfs.d \
./gt_scheduler_pcs.d \
./gt_scheduler_switcher.d \
./gt_signal.d \
./gt_spinlock.d \
./gt_thread.d \
./gt_uthread.d \
./gt_uthread_attr.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -DDEBUG -O0 -g3 -pedantic -Wall -c -fmessage-length=0 -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


