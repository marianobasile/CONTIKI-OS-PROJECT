#include "contiki.h"
#include "net/rime/rime.h"
#include "stdio.h"
#include "dev/sht11/sht11-sensor.h"
#include "math.h"
#include "random.h"
#include "dev/leds.h"

#define MAX_RETRASMISSION 5
#define HUMIDITY_FIXED_VALUE 116
#define THRESHOLD_ACTIVATION_DEHUMIDIFIER 70
#define NODE_3 3
#define ON 1
#define OFF 0

PROCESS(main_process_node4,"Main Process - Node 4");
PROCESS(humidity_process_node4,"Humidity Process - Node 4");

static void recv_runicast(struct runicast_conn* connection, const linkaddr_t* sender, uint8_t seqno)
{
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"compute humidity value") == 0)
	{
		process_start(&humidity_process_node4,NULL);
		process_post(&humidity_process_node4,PROCESS_EVENT_MSG,NULL);
	}
}

static void sent_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission){}

static void timedout_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission)
{
	printf("Error while executing the command: (Time out while sending command to Node%d)",receiver->u8[0]);
}

static struct runicast_conn connection;
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};

static void send_runicast_frame(uint8_t receiver_node_id, char* message)
{	
		linkaddr_t receiver;
		receiver.u8[0] = receiver_node_id;
		receiver.u8[1] = 0;
		packetbuf_copyfrom(message,strlen(message)+1);

		//printf("Trying to %s\n",message);
		runicast_send(&connection,&receiver,MAX_RETRASMISSION);
}


AUTOSTART_PROCESSES(&main_process_node4);

PROCESS_THREAD(main_process_node4, ev, data) 
{
	PROCESS_EXITHANDLER(runicast_close(&connection));

	PROCESS_BEGIN();

	runicast_open(&connection,144,&runicast_calls);

	leds_on(LEDS_RED);

	PROCESS_WAIT_EVENT_UNTIL(0);

	PROCESS_END();
}

PROCESS_THREAD(humidity_process_node4, ev, data) 
{
	static int humidity;
	static int dec;
	static uint8_t dehumidifier_status;
	static char buff[15];

	PROCESS_BEGIN();

	SENSORS_ACTIVATE(sht11_sensor);
	dehumidifier_status = OFF;
	
	for(;;)
	{
		PROCESS_WAIT_EVENT();
		if(ev == PROCESS_EVENT_MSG)
		{
			humidity = (((0.0405*sht11_sensor.value(SHT11_SENSOR_HUMIDITY)) - 4) + ((-2.8 * 0.000001)*(pow(sht11_sensor.value(SHT11_SENSOR_HUMIDITY),2))));
			dec  = humidity;
			dec = dec - (random_rand()%(HUMIDITY_FIXED_VALUE+1));
			snprintf(buff,sizeof(buff),"Humidity: %d%%", dec);
			while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
			send_runicast_frame(NODE_3,buff);
			if(dec >= THRESHOLD_ACTIVATION_DEHUMIDIFIER && dehumidifier_status == OFF){
					dehumidifier_status = ON;
					leds_off(LEDS_RED);
					leds_on(LEDS_GREEN);
					while(runicast_is_transmitting(&connection))
						PROCESS_PAUSE();
					send_runicast_frame(NODE_3,"Humidifier has been turned on!");
			} 
			else if(dehumidifier_status == ON && dec <= THRESHOLD_ACTIVATION_DEHUMIDIFIER) 
			{
				dehumidifier_status = OFF;
				leds_off(LEDS_GREEN);
				leds_on(LEDS_RED);
				while(runicast_is_transmitting(&connection))
					PROCESS_PAUSE();
				send_runicast_frame(NODE_3,"Humidifier has been turned off!");
			}
		}
	}
	PROCESS_END();
}


