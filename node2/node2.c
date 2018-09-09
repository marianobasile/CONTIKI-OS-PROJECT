#include "contiki.h"
#include "net/rime/rime.h"
#include "stdio.h"
#include "dev/leds.h"
#include "dev/sht11/sht11-sensor.h"
#include "sys/etimer.h"
#include "string.h"
#include "dev/light-sensor.h"


#define MAX_RETRASMISSION 5
#define ACTIVATED 1
#define DEACTIVATED 0
#define LOCKED 1
#define UNLOCKED 0
#define NODE_3 3
#define OPENED 1
#define CLOSED 0
#define EXPIRE_TIME 16
#define PERIOD 2


PROCESS(main_process_node2,"Main Process - Node 2");
PROCESS(alarm_process_node2,"Alarm Process - Node 2");
PROCESS(gate_process_node2,"Gate Process - Node 2");
PROCESS(guest_process_node2,"Guest Process - Node 2");
PROCESS(light_process_node2,"Light Process - Node 2");

static uint8_t gate_status;
static uint8_t gate_condition;

static void broadcast_recv(struct broadcast_conn* connection, const linkaddr_t* sender)
{	
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"update the alarm status") == 0)
	{
		process_start(&alarm_process_node2,NULL);
		process_post(&alarm_process_node2,PROCESS_EVENT_MSG,NULL);
	}
	else if(strcmp(recv_message,"let a guest enter") == 0)
	{
		process_start(&guest_process_node2,NULL);
		process_post(&guest_process_node2,PROCESS_EVENT_MSG,NULL);
	}
}

static void broadcast_sent(struct broadcast_conn* connection, int status, int num_tx){}

static void recv_runicast(struct runicast_conn* connection, const linkaddr_t* sender, uint8_t seqno)
{
	
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"update the gate status") == 0)
	{
		process_start(&gate_process_node2,NULL);
		process_post(&gate_process_node2,PROCESS_EVENT_MSG,NULL);
	}
	else if(strcmp(recv_message,"compute light intensity") == 0)
	{
		//printf("\nNODE_2: PASSOOOO!\n");
		process_start(&light_process_node2,NULL);
		process_post(&light_process_node2,PROCESS_EVENT_MSG,NULL);
	}

	
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

AUTOSTART_PROCESSES(&main_process_node2);

PROCESS_THREAD(main_process_node2, ev, data) 
{
	PROCESS_EXITHANDLER(runicast_close(&connection));
	PROCESS_EXITHANDLER(broadcast_close(&b_connection));

	PROCESS_BEGIN();

	runicast_open(&connection,144,&runicast_calls);
	broadcast_open(&b_connection,129,&broadcast_calls);

	SENSORS_ACTIVATE(sht11_sensor);

	gate_status = UNLOCKED;
	leds_on(LEDS_GREEN);
	leds_off(LEDS_RED);

	PROCESS_WAIT_EVENT_UNTIL(0);

	PROCESS_END();
}

PROCESS_THREAD(alarm_process_node2, ev, data)
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
			while(gate_condition == OPENED)
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

PROCESS_THREAD(gate_process_node2, ev, data)
{
	PROCESS_BEGIN();

	gate_status = UNLOCKED;
	gate_condition = CLOSED;

	while(1)
	{
		PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_MSG);

		while(gate_condition == OPENED)
			PROCESS_PAUSE();

		if(gate_status == UNLOCKED)
		{
			gate_status = LOCKED;
			leds_off(LEDS_GREEN);
			leds_on(LEDS_RED);
			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
			send_runicast_frame(NODE_3,"gate locked");
		}
		else 
		{
			gate_status = UNLOCKED;
			leds_on(LEDS_GREEN);
			leds_off(LEDS_RED);
			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
			send_runicast_frame(NODE_3,"gate unlocked");
		}
	}
	//printf("Node1: Update alarm status received by Node%d!\n",sender->u8[0]);
	PROCESS_END();
}

PROCESS_THREAD(guest_process_node2, ev, data)
{
	static struct etimer et; 
	static uint8_t elapsed_time;

	PROCESS_BEGIN();

	elapsed_time = 0;
	gate_condition = CLOSED;

	while(1)
	{
		PROCESS_WAIT_EVENT();

		if(ev == PROCESS_EVENT_MSG)
		{
			leds_toggle(LEDS_BLUE);
			gate_condition = OPENED;
			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
			send_runicast_frame(NODE_3,"gate opened");
			etimer_set(&et, CLOCK_SECOND*PERIOD);
		}
		else if(ev == PROCESS_EVENT_TIMER)
		{
			leds_toggle(LEDS_BLUE);
			elapsed_time += PERIOD;
			if(elapsed_time == EXPIRE_TIME)
			{
				gate_condition = CLOSED;
				elapsed_time = 0;
				leds_toggle(LEDS_BLUE);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
				send_runicast_frame(NODE_3,"gate closed");
			}
			else
				etimer_reset(&et);
		}
	}
	//printf("Node1: Update alarm status received by Node%d!\n",sender->u8[0]);
	PROCESS_END();
}

PROCESS_THREAD(light_process_node2, ev, data)
{
	static char buff[18];
	PROCESS_BEGIN();
	SENSORS_ACTIVATE(light_sensor);
	for(;;)
	{
		PROCESS_WAIT_EVENT();
		if(ev == PROCESS_EVENT_MSG)
		{	
			snprintf(buff,sizeof(buff),"Sensed light: %d",10*light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC)/7);
			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
			send_runicast_frame(NODE_3,buff);
		}	
	}
	SENSORS_DEACTIVATE(light_sensor);
	PROCESS_END();
}
