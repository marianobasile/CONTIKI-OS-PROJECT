#include "contiki.h"
#include "net/rime/rime.h"
#include "stdio.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "dev/sht11/sht11-sensor.h"
#include "sys/etimer.h"
#include "string.h"
#include "random.h"

#define ARRAY_SIZE 5
#define BUTTON_PRESSED 1 
#define BUTTON_UNPRESSED 0
#define ACTIVATED 1
#define DEACTIVATED 0
#define OPENED 1
#define CLOSED 0
#define MAX_RETRASMISSION 5
#define NODE_3 3
#define DELAY_TIME 14
#define EXPIRE_TIME 16
#define PERIOD 2
#define NOT_YET_STARTED 2
#define STARTED 0
#define TERMINATED 1
#define TEMPERATURE_PERIOD 10
#define MAX_DEVIATION 2


PROCESS(main_process_node1,"Main Process - Node 1");
PROCESS(alarm_process_node1,"Alarm Process - Node 1");
PROCESS(guest_process_node1,"Guest Process - Node 1");

static uint8_t countdown_status = NOT_YET_STARTED;

static void broadcast_recv(struct broadcast_conn* connection, const linkaddr_t* sender)
{	
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"update the alarm status") == 0)
	{
		process_start(&alarm_process_node1,NULL);
		process_post(&alarm_process_node1,PROCESS_EVENT_MSG,NULL);
	}
	else if(strcmp(recv_message,"let a guest enter") == 0)
	{
		process_start(&guest_process_node1,NULL);
		process_post(&guest_process_node1,PROCESS_EVENT_MSG,NULL);
	}
}

static void broadcast_sent(struct broadcast_conn* connection, int status, int num_tx){}

static void recv_runicast(struct runicast_conn* connection, const linkaddr_t* sender, uint8_t seqno)
{
	
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"compute avg temperature") == 0)
		process_post(&main_process_node1,PROCESS_EVENT_MSG,NULL);
}

static void sent_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission){}

static void timedout_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission)
{
	printf("Error while executing the command: (Time out while sending command to Node%d)",receiver->u8[0]);
}

static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn connection;

static struct broadcast_conn b_connection;
static const struct broadcast_callbacks broadcast_calls = {broadcast_recv, broadcast_sent};

static void send_runicast_frame(uint8_t receiver_node_id, char* message)
{
		
		linkaddr_t receiver;
		receiver.u8[0] = receiver_node_id;
		receiver.u8[1] = 0;
		packetbuf_copyfrom(message,strlen(message)+1);

		//printf("Trying to %s\n",message);
		runicast_send(&connection,&receiver,MAX_RETRASMISSION);
}

AUTOSTART_PROCESSES(&main_process_node1);

PROCESS_THREAD(main_process_node1, ev, data) 
{
	PROCESS_EXITHANDLER(runicast_close(&connection));
	PROCESS_EXITHANDLER(broadcast_close(&b_connection));

	static int temperature[ARRAY_SIZE]; 
	static int sum;
	static int dec;
	static double frac;
	static uint8_t position;
	static uint8_t button_status;
	static struct etimer et;
	static uint8_t i;
	static double avg_temp;
	static char buff[24];

	PROCESS_BEGIN();

	runicast_open(&connection,144,&runicast_calls);
	broadcast_open(&b_connection,129,&broadcast_calls);

	SENSORS_ACTIVATE(button_sensor);
	SENSORS_ACTIVATE(sht11_sensor);

	button_status = BUTTON_UNPRESSED;
	leds_on(LEDS_RED);

	position = 0;
	sum=0;
	avg_temp = 0.0;
	for(i=0;i<ARRAY_SIZE;i++)
		temperature[i] = 0;

	etimer_set(&et, CLOCK_SECOND*TEMPERATURE_PERIOD);

	while(1)
	{
		PROCESS_WAIT_EVENT();
		if(ev == sensors_event && data == &button_sensor) 
		{
			if(button_status == BUTTON_UNPRESSED)
			{
				button_status = BUTTON_PRESSED;
				leds_off(LEDS_RED);
				leds_on(LEDS_GREEN);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();

				send_runicast_frame(NODE_3,"Activated lights in the garden");
				//printf("NODE1: Activated lights in the garden\n");	
			}
			else
			{
				button_status = BUTTON_UNPRESSED;
				leds_off(LEDS_GREEN);
				leds_on(LEDS_RED);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();

				send_runicast_frame(NODE_3,"Deactivated lights in the garden");
				//printf("NODE1: Deactivated lights in the garden\n");
			}
		}
		if(ev == PROCESS_EVENT_TIMER)
		{	
			if(random_rand()%4 == 0 || random_rand()%4 == 1)
				temperature[position] = ((sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10) - random_rand()%MAX_DEVIATION;
			else
				temperature[position] = ((sht11_sensor.value(SHT11_SENSOR_TEMP)/10-396)/10) + random_rand()%MAX_DEVIATION;
			//printf("\nTemperature %d: %d\n",position,temperature[position]);
			position = (position+1)%ARRAY_SIZE;
			etimer_reset(&et);
		}
		else if(ev == PROCESS_EVENT_MSG)
		{
			for(i=0;i<ARRAY_SIZE;i++)
			{
				//printf("\nTemperature %u: %d\n",i,temperature[i]);
				sum += temperature[i];
			}
			avg_temp = (double) sum / ARRAY_SIZE;
			dec = avg_temp;
			frac = avg_temp - dec;
			//printf("\nAvg Temperature: %d.%02u \n",dec,(unsigned int)(frac *100));
			//printf("\nAvg Temperature: %.2f\n",avg_temp);

			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
		
			snprintf(buff,sizeof(buff),"Avg Temperature: %d.%02u C",dec,(unsigned int)(frac *100));
			send_runicast_frame(NODE_3,buff);
			sum = 0;
		}
	}

	PROCESS_END();
}

PROCESS_THREAD(alarm_process_node1, ev, data)
{
	static struct etimer et;
	static uint8_t led_status;
	static uint8_t alarm_status;

	PROCESS_BEGIN();

	alarm_status = DEACTIVATED;

	while(1)
	{
		PROCESS_WAIT_EVENT();

		if(ev == PROCESS_EVENT_MSG)
		{
			while(countdown_status != NOT_YET_STARTED)
				PROCESS_PAUSE();

			if(alarm_status == DEACTIVATED)
			{
				led_status = leds_get();
				alarm_status = ACTIVATED;
				leds_off(LEDS_ALL);
				leds_on(LEDS_ALL);
				etimer_set(&et, CLOCK_SECOND*PERIOD);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
				send_runicast_frame(NODE_3,"alarm activated");
				
			}
			else 
			{
				alarm_status = DEACTIVATED;
				etimer_stop(&et);
				leds_off(LEDS_ALL);
				leds_on(led_status);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
				send_runicast_frame(NODE_3,"alarm deactivated");
			}
		} 
		else if(ev == PROCESS_EVENT_TIMER)
		{
			leds_toggle(LEDS_ALL);
			etimer_reset(&et);
		} 
	}
	//printf("Node1: Update alarm status received by Node%d!\n",sender->u8[0]);
	PROCESS_END();
}

PROCESS_THREAD(guest_process_node1, ev, data)
{
	static struct etimer et;
	static uint8_t door_status;
	static uint8_t elapsed_time;
	
	PROCESS_BEGIN();

	elapsed_time = 0;
	door_status = CLOSED;

	while(1)
	{
		PROCESS_WAIT_EVENT();

		if(ev == PROCESS_EVENT_MSG)
		{
			while(elapsed_time != 0)
				PROCESS_PAUSE();

			etimer_set(&et, CLOCK_SECOND*DELAY_TIME);
			countdown_status = STARTED;
			//send_runicast_frame(NODE_3,"alarm activated");
		}
		else if(ev == PROCESS_EVENT_TIMER)
		{	
			leds_toggle(LEDS_BLUE);
			if(door_status == CLOSED)
			{
				door_status = OPENED;
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
				send_runicast_frame(NODE_3,"door opened");
				countdown_status = TERMINATED;
				etimer_set(&et,CLOCK_SECOND*PERIOD);
			}
			else
			{
				elapsed_time += PERIOD;
				if(elapsed_time == EXPIRE_TIME)
				{
					door_status = CLOSED;
					elapsed_time = 0;
					countdown_status = NOT_YET_STARTED;
					leds_toggle(LEDS_BLUE);
					while(runicast_is_transmitting(&connection))
						PROCESS_PAUSE();
					send_runicast_frame(NODE_3,"door closed");
				}
				else
					etimer_set(&et,CLOCK_SECOND*PERIOD);
			}
		}
	}
	//printf("Node1: Update alarm status received by Node%d!\n",sender->u8[0]);
	PROCESS_END();
}