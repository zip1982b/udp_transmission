/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"
#include "pwm.h"
#include "lwip/udp.h"


//PIN SETTINGS FOR PWM
#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_MTDI_U
#define PWM_0_OUT_IO_NUM 12
#define PWM_0_OUT_IO_FUNC  FUNC_GPIO12

#define PWM_1_OUT_IO_MUX PERIPHS_IO_MUX_MTCK_U
#define PWM_1_OUT_IO_NUM 13
#define PWM_1_OUT_IO_FUNC  FUNC_GPIO13

#define PWM_NUM_CHANNEL_NUM    2  //number of PWM Channels

xTaskHandle xPWM_Task1Handle;	//дескриптор - идентификатор задачи1
xTaskHandle xPWM_Task2Handle;	//дескриптор - идентификатор задачи2

portBASE_TYPE xStatusTask1;	//результат создания задачи 1
portBASE_TYPE xStatusTask2;	//результат создания задачи 2

typedef struct PWM_Param_t {
	uint8 pwm_channel;
}PWM_Param;


/* Объявление двух структур PWM_Param */
PWM_Param xPWM1_Param;
PWM_Param xPWM2_Param;



/*
void gpio_isr (uint32 mask, void* arg)
{
	printf("Interrupt now\r\n");
	 delay += delay_increment;
	 if (delay == 1)
		 delay_increment = 1;
	 if (delay == 20)
		 delay_increment = -1;
	 // "rearm" the interrupt
	 uint32_t gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	 GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status );
}
*/

//callback function UDP RECEIVE
/*
Эта функция будет вызываться LwIP для каждого отдельного входящего пакета,
который необходимо обработать программой. Всё это анализируется и упаковывается в аккуратные структуры.
Особый интерес, объект pubf представляет собой буфер полезной нагрузки пакета.
Этот объект динамически выделяется LwIP для хранения входящего пакета,
лишенного каких-либо служебных данных протокола.
Поскольку пакет может быть пустым, необходимо проверить, что LwIP действительно выделил pbuf,
указав, что его указатель не является NULL.
Полезная нагрузка, содержащаяся в объекте pbuf, хранится в виде байтового массива.
*/
void zhan_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
  if (p != NULL){
	uint8_t data = (uint8_t) ((uint8_t*)p->payload)[0]; // преобразование к типу uint8_t из байтового массива
	pbuf_free(p); // освобождаем буффер
	printf("recv data:%d\n", data);
	//printf("IP:%d\n", addr);
	if (data == 1){
		  vTaskResume(xPWM_Task1Handle); // Задача готова к работе
	  }
	if (data == 0){
		  vTaskSuspend(xPWM_Task1Handle);// Приостановить задачу
	  }
	  
	char reply[]="Received X";
	reply[9] = data + 48;
	struct pbuf *reply_pbuf;
	reply_pbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(reply), PBUF_RAM);
	memcpy(reply_pbuf->payload, reply, sizeof(reply));
	udp_sendto(pcb, reply_pbuf, addr, 8888);
	pbuf_free(reply_pbuf);
	  
	  
	}
	
	
}




// Task for PWM
void vPWM_Task(void *pvParameters)
{
	printf("welcome to vPWM_Task!\r\n");
	volatile PWM_Param *pxPWM;
	/* Преобразование типа void* к типу PWM_Param* */
	pxPWM = (PWM_Param *) pvParameters;
	printf("send param channel:%d\n", pxPWM->pwm_channel);
	int i = 512;
	while (1) {
		pwm_set_duty(i, pxPWM->pwm_channel); //Set duty to specific channel
		pwm_start();   //Call this: every time you change duty/period
		vTaskDelay(1000/portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}
/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    printf("SDK version:%s\n", system_get_sdk_version());
    printf("PWM TEST !!!!\r\n");
	printf("UDP TEST !!!!\r\n");
	conn_ap_init();
	
	
    uint32 io_info[][3] = {
            { PWM_0_OUT_IO_MUX, PWM_0_OUT_IO_FUNC, PWM_0_OUT_IO_NUM }, //Channel 0
            { PWM_1_OUT_IO_MUX, PWM_1_OUT_IO_FUNC, PWM_1_OUT_IO_NUM }, //Channel 1
    };

	u32 duty[2] = {512, 512}; //Max duty cycle is 1023
	pwm_init(1000, duty, PWM_NUM_CHANNEL_NUM, io_info);
	
	// заполнение структуры - нулевой канал
	xPWM1_Param.pwm_channel = 0;
	xPWM2_Param.pwm_channel = 1;
	
	xStatusTask1 = xTaskCreate(vPWM_Task, "PWM_Task1", 256, &xPWM1_Param, 2, &xPWM_Task1Handle);
	xStatusTask2 = xTaskCreate(vPWM_Task, "PWM_Task2", 256, &xPWM2_Param, 2, &xPWM_Task2Handle);
	
	if (xStatusTask1 == pdPASS) {
		printf("PWM_Task1 is created\r\n");
	}
	else {
		printf("PWM_Task1 is not created\r\n");
	}
		
	if (xStatusTask2 == pdPASS) {
		printf("PWM_Task2 is created\r\n");
	}
	else {
		printf("PWM_Task2 is not created\r\n");
	}
	
	// Protocol Control Block - блок управления протоколом
	struct udp_pcb *zhan_pcb;
	zhan_pcb = udp_new();
	udp_bind(zhan_pcb, IP_ADDR_ANY, 8888); 		// фильтр для всех, кроме порта 8888
	
	/*вызовом udp_recv мы сообщаем LwIP, 
	что любой входящий пакет, соответствующий этому фильтру
	(==, проходящий через этот сокет), должен быть передан
	функции обратного вызова (callback) с именем zhan_udp_recv для обработки */
	udp_recv(zhan_pcb, zhan_udp_recv, NULL);

	
							
	
	
	
	
}

