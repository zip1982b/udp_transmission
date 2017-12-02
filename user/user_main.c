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
#include "freertos/queue.h"


//PIN SETTINGS FOR PWM
#define PWM_0_OUT_IO_MUX PERIPHS_IO_MUX_MTDI_U
#define PWM_0_OUT_IO_NUM 12
#define PWM_0_OUT_IO_FUNC  FUNC_GPIO12

#define PWM_1_OUT_IO_MUX PERIPHS_IO_MUX_MTCK_U
#define PWM_1_OUT_IO_NUM 13
#define PWM_1_OUT_IO_FUNC  FUNC_GPIO13

#define PWM_NUM_CHANNEL_NUM    2  //number of PWM Channels

//Дескрипторы задач и очередей
xTaskHandle xPWM_Task0Handle;	//дескриптор - идентификатор задачи0
xTaskHandle xPWM_Task1Handle;	//дескриптор - идентификатор задачи1
//xTaskHandle xUDPreply_TaskHandle;	//дескриптор - идентификатор задачи1
xQueueHandle xQueueSetDC0;		//дескриптор очереди для канала 0 - 
xQueueHandle xQueueSetDC1;		//дескриптор очереди для канала 1 - 
//xQueueHandle xQueueDutyCycle;	//дескриптор очереди для передачи Duty Cycle обоих каналов, задаче vUDPReply_Task 




typedef struct {
	uint8 pwm_channel;	//номер канала 
	uint32 DutyCycle;	//коэфициент заполенеия
} xData;


/* Объявление двух структур xData */
xData xPWM0_Param;
xData xPWM1_Param;



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
void udp_recv_command(void *arg, struct udp_pcb *pcb, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
	portBASE_TYPE xStatus; // Переменная, которая будет хранить результат выполнения xQueueSendToBack()
	int iCHN;
	uint32 uiDC;
	if (p != NULL){
		if ((*p).len <= 6 && ((char*)(*p).payload)[1] == ':'){
			printf("UDP recv %d bytes:\n", (*p).len);
			char channel_str = (char) ((char*)p->payload)[0];
			
			iCHN = (int) channel_str;
			iCHN = iCHN - '0';
			printf("Channel = %d\n", iCHN);
			
			char duty_cycle[(*p).len - 2];
			int i;
			for (i = 0; i < ((*p).len - 2); ++i){
				duty_cycle[i] = (char) ((char*)(*p).payload)[i+2];
			}
			
			uiDC = atoi(duty_cycle); //int
			printf("Duty Cycle = %d\n", uiDC);
		}
		else {
			printf("Error len or structure UDP payload\n");
		}
	pbuf_free(p); // освобождаем буффер
	}
	
	if (iCHN  == 1){
		xStatus = xQueueSendToBack(xQueueSetDC0, &uiDC, 0);//отправляем данные duty cycle в нулевой канал
		if (xStatus != pdPASS) {
			// Если попытка записи не была успешной — индицировать ошибку.
			printf("Could not send to the queue0.\r\n");
		}
	}
	
	if (iCHN == 2){
		xStatus = xQueueSendToBack(xQueueSetDC1, &uiDC, 0);//отправляем данные duty cycle в первый канал
		if (xStatus != pdPASS) {
			// Если попытка записи не была успешной — индицировать ошибку.
			printf("Could not send to the queue1.\r\n");
		}
	}  
	
}


// Task for PWM
void vPWM_Task(void *pvParameters)
{
	printf("welcome to vPWM_Task!\r\n");
	volatile xData *pxPWM;
	/* Преобразование типа void* к типу xData */
	pxPWM = (xData *) pvParameters;
	printf("send param channel:%d\n", pxPWM->pwm_channel);
	printf("send param duty cycle:%d\n", pxPWM->DutyCycle);
	pwm_set_duty(pxPWM->DutyCycle, pxPWM->pwm_channel); //Set duty to specific channel
	pwm_start();   //Call this: every time you change duty/period
	uint32 uiReceivedDC; //для считывания из очереди
	portBASE_TYPE xStatusRecv;
	//portBASE_TYPE xStatusSend;
	
	// Protocol Control Block - блок управления протоколом
	struct udp_pcb *reply_pcb;
	reply_pcb = udp_new();
	struct pbuf *p;
	struct ip_addr client;
	//IP4_ADDR(&client, 192, 168, 1, 101); //ip адресс компьютера (клиента)
	IP4_ADDR(&client, 192, 168, 8, 102); //ip адресс компьютера (клиента)
	
	char Reply[] = "Channel:x, Duty Cycle:xxxx";
	char Channel[0];
	char DutyCycle[4];
	
	while (1) {
		//vTaskDelay(1000/portTICK_RATE_MS);
		//********* for channel 1 ****************
		if ((pxPWM->pwm_channel)==0){
			xStatusRecv = xQueueReceive(xQueueSetDC0, &uiReceivedDC, 100/portTICK_RATE_MS);
			if (xStatusRecv == pdPASS) {
				// DutyCycle успешно принято, вывести его на экран
				printf("Received Duty Cycle = %d\r\n", uiReceivedDC);
				//printf("Channel= %d\r\n", pxPWM->pwm_channel);
				pwm_set_duty(uiReceivedDC, 0); //Set duty to specific channel
				pwm_start();   //Call this: every time you change duty/period
				
				sprintf(DutyCycle, "%d", uiReceivedDC);
				printf("Send Channel = 1\r\n");
				printf("Send DC = %d\r\n", uiReceivedDC);
				
				Reply[8] = '1';
				
				int i;
				int k = 22;
				printf ("Длина строки DUTY CYCLE «%s» - %d символов\n", DutyCycle, strlen(DutyCycle));
				for (i = 0; i < strlen(DutyCycle); ++i){
					Reply[k] = DutyCycle[i];
					k++;
				}
				
				
				p = pbuf_alloc(PBUF_TRANSPORT, sizeof(Reply), PBUF_RAM);
				memcpy(p->payload, &Reply, sizeof(Reply));
				udp_sendto(reply_pcb, p, &client, 5555);
				pbuf_free(p);
				
				k = 22;
				for (i = 0; i < 4; ++i){
					Reply[k] = 'x';
					k++;
				}
				
				
				/*
				pxPWM->pwm_channel = 1;
				pxPWM->DutyCycle = uiReceivedDC;
				xStatusSend = xQueueSendToBack(xQueueDutyCycle, &pxPWM, 0);
				if (xStatusSend != pdPASS) {
					printf("Could not send to the queue.\r\n");
				}
				*/
			}
			/*
			else {
				// Данные не были прочитаны из очереди на протяжении тайм-аута 100 мс.
				printf("Error - Not read Duty Cycle from queue. Channel= %d\r\n", pxPWM->pwm_channel);
			}
			*/
			
		}
		//********* for channel 2 ****************
		if ((pxPWM->pwm_channel)==1){
			xStatusRecv = xQueueReceive(xQueueSetDC1, &uiReceivedDC, 100/portTICK_RATE_MS);
			if (xStatusRecv == pdPASS) {
				// DutyCycle успешно принято, вывести его на экран
				printf("Received Duty Cycle = %d\r\n", uiReceivedDC);
				//printf("Channel= %d\r\n", pxPWM->pwm_channel);
				pwm_set_duty(uiReceivedDC, 1); //Set duty to specific channel
				pwm_start();   //Call this: every time you change duty/period
				
				sprintf(DutyCycle, "%d", uiReceivedDC);
				printf("Send Channel = 2\r\n");
				printf("Send DC = %d\r\n", uiReceivedDC);
				
				Reply[8] = '2';
				int i;
				int k = 22;
				printf ("Длина строки DUTY CYCLE «%s» - %d символов\n", DutyCycle, strlen(DutyCycle));
				for (i = 0; i < strlen(DutyCycle); ++i){
					Reply[k] = DutyCycle[i];
					k++;
				}
				
				p = pbuf_alloc(PBUF_TRANSPORT, sizeof(Reply), PBUF_RAM);
				memcpy(p->payload, &Reply, sizeof(Reply));
				udp_sendto(reply_pcb, p, &client, 5555);
				pbuf_free(p);
				
				k = 22;
				for (i = 0; i < 4; ++i){
					Reply[k] = 'x';
					k++;
				}
				
				/*
				pxPWM->pwm_channel = 2;
				pxPWM->DutyCycle = uiReceivedDC;
				xStatusSend = xQueueSendToBack(xQueueDutyCycle, &pxPWM, 0);
				if (xStatusSend != pdPASS) {
					printf("Could not send to the queue.\r\n");
				}
				*/
				
				
			}
			/*
			else {
				// Данные не были прочитаны из очереди на протяжении тайм-аута 100 мс.
				printf("Error - Not read Duty Cycle from queue. Channel= %d\r\n", pxPWM->pwm_channel);
			}*/
				
		}
		taskYIELD();	
	}
	vTaskDelete(NULL);
}



// Task for send by UDP
/*
void vUDPReply_Task(void *pvParameters)
{
	printf("welcome to vUDPsendReply_Task!\r\n");
	// настройка UDP для отправки сообщения клиенту 
	// Protocol Control Block - блок управления протоколом
	struct udp_pcb *reply_pcb;
	reply_pcb = udp_new();
	struct pbuf *p;
	struct ip_addr client;
	//IP4_ADDR(&client, 192, 168, 1, 101); //ip адресс компьютера (клиента)
	IP4_ADDR(&client, 192, 168, 8, 102); //ip адресс компьютера (клиента)
	xData reply;
	portBASE_TYPE xStatus;
	
	char Reply[] = "Channel: , Duty Cycle:    ";
	char Channel[0];
	char DutyCycle[5];
	
	while (1) {
		xStatus = xQueueReceive(xQueueDutyCycle, &reply, 100/portTICK_RATE_MS);
		if (xStatus == pdPASS) {
			sprintf(Channel, "%d", reply.pwm_channel);
			sprintf(DutyCycle, "%d", reply.DutyCycle);
			printf("Send Channel = %d\r\n", reply.pwm_channel);
			printf("Send DC = %d\r\n", reply.DutyCycle);
			Reply[8] = Channel[0];
			Reply[22] = DutyCycle[0];
			Reply[23] = DutyCycle[1];
			Reply[24] = DutyCycle[2];
			p = pbuf_alloc(PBUF_TRANSPORT, sizeof(Reply), PBUF_RAM);
			memcpy(p->payload, &Reply, sizeof(Reply));
			udp_sendto(reply_pcb, p, &client, 5555);
			pbuf_free(p);
		}
	}
	vTaskDelete(NULL);
}

*/



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
	
    
//******************************************************************************************************/
	xQueueSetDC0 = xQueueCreate(5, sizeof(uint32)); // очередь для приёма duty cycle для канала 1
	xQueueSetDC1 = xQueueCreate(5, sizeof(uint32));	// очередь для приёма duty cycle для канала 2
	if (xQueueSetDC0 !=NULL && xQueueSetDC1 !=NULL){
		portBASE_TYPE xStatusTask0;	//результат создания задачи 0
		portBASE_TYPE xStatusTask1;	//результат создания задачи 1
		
		uint32 io_info[][3] = {
            { PWM_0_OUT_IO_MUX, PWM_0_OUT_IO_FUNC, PWM_0_OUT_IO_NUM }, //Channel 0
            { PWM_1_OUT_IO_MUX, PWM_1_OUT_IO_FUNC, PWM_1_OUT_IO_NUM }, //Channel 1
		};

		u32 duty[2] = {512, 512}; //Max duty cycle is 1023
		pwm_init(1000, duty, PWM_NUM_CHANNEL_NUM, io_info);
		
		// заполнение структуры - нулевой канал
		xPWM0_Param.pwm_channel = 0;
		xPWM0_Param.DutyCycle = 0;
		// первый канал
		xPWM1_Param.DutyCycle = 0;
		xPWM1_Param.pwm_channel = 1;
		
		
		xStatusTask0 = xTaskCreate(vPWM_Task, "PWM_Task0", 256, &xPWM0_Param, 2, &xPWM_Task0Handle);
		xStatusTask1 = xTaskCreate(vPWM_Task, "PWM_Task1", 256, &xPWM1_Param, 2, &xPWM_Task1Handle);
		
		if (xStatusTask0 == pdPASS) {
			printf("PWM_Task0 is created\r\n");
		}
		else {
			printf("PWM_Task0 is not created\r\n");
		}
			
		if (xStatusTask1 == pdPASS) {
			printf("PWM_Task1 is created\r\n");
		}
		else {
			printf("PWM_Task1 is not created\r\n");
		}
	}
	else {
		printf("Error - queues is not created\r\n");
	}
	
//************ Очередь и задача для отправки данных клиенту *****************************************/
/*
	xQueueDutyCycle = xQueueCreate(6, sizeof(xData)); // очередь для приёма duty cycle для канала 1 и 2
	portBASE_TYPE xStatusUDPreplyTask;	//результат создания задачи vUDPReply_Task
	if (xQueueDutyCycle !=NULL){
		xStatusUDPreplyTask = xTaskCreate(vUDPReply_Task, "vUDPReply_Task", 256, NULL, 2, &xUDPreply_TaskHandle);
		if (xStatusUDPreplyTask == pdPASS){
			printf("vUDPReply_Task is created\r\n");
		}
		else {
			printf("vUDPReply_Task is not created\r\n");
		}
	}
	else {
		printf("Error - xQueueDutyCycle is not created\r\n");
	}
	*/
/****LWIP********************************************************/	
	// Protocol Control Block - блок управления протоколом
	struct udp_pcb *request_pcb;
	request_pcb = udp_new();
	udp_bind(request_pcb, IP_ADDR_ANY, 8888); 		// фильтр для всех, кроме порта 8888
	
	/*вызовом udp_recv мы сообщаем LwIP, 
	что любой входящий пакет, соответствующий этому фильтру
	(==, проходящий через этот сокет), должен быть передан
	функции обратного вызова (callback) с именем udp_recv_command для обработки */
	udp_recv(request_pcb, udp_recv_command, NULL);
}


