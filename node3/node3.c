#include "contiki.h"
#include "net/rime/rime.h"
#include "stdio.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "sys/etimer.h"
#include "string.h"

#define NO_COMMAND 0
#define COMMAND_ONE 1
#define COMMAND_TWO 2
#define COMMAND_THREE 3
#define COMMAND_FOUR 4
#define COMMAND_FIVE 5
#define COMMAND_SIX 6
#define MAX_COMMANDS 7
#define NODE_1 1
#define NODE_2 2
#define NODE_4 4
#define ACTIVATED 1
#define DEACTIVATED 0
#define LOCKED 1
#define UNLOCKED 0
#define OPENED 1
#define CLOSED 0
#define MAX_RETRASMISSION 5

static uint8_t alarm_status_node1 = DEACTIVATED;
static uint8_t alarm_status_node2 = DEACTIVATED;
static uint8_t gate_status_node2 = UNLOCKED;
static uint8_t gate_condition_node2 = CLOSED;
static uint8_t door_status_node1 = CLOSED;

static void broadcast_recv(struct broadcast_conn* connection, const linkaddr_t* sender){}

static void broadcast_sent(struct broadcast_conn* connection, int status, int num_tx)
{
	printf("Broadcast msg sent. Trasmission #: %d Status: %d\n",num_tx,status);
}

static void recv_runicast(struct runicast_conn* connection, const linkaddr_t* sender, uint8_t seqno)
{
	char* recv_message = (char*)packetbuf_dataptr();
	if(strcmp(recv_message,"alarm activated") == 0){
		if(sender->u8[0] == NODE_1)
			alarm_status_node1 = ACTIVATED;
		else if(sender->u8[0] == NODE_2)
			alarm_status_node2 = ACTIVATED;
		if(alarm_status_node1 == ACTIVATED && alarm_status_node2 == ACTIVATED)
			printf("Alarm activated!\n");
		//printf("Node3: Alarm activated by Node%d!\n",sender->u8[0]);
	} 
	else if(strcmp(recv_message,"alarm deactivated") == 0) 
	{
		if(sender->u8[0] == NODE_1)
			alarm_status_node1 = DEACTIVATED;
		else if(sender->u8[0] == NODE_2)
			alarm_status_node2 = DEACTIVATED;
		if(alarm_status_node1 == DEACTIVATED && alarm_status_node2 == DEACTIVATED)
			printf("Alarm deactivated!\n");
		//printf("Node3: Alarm deactivated by Node%d!\n",sender->u8[0]);
	} 
	else if(strcmp(recv_message,"gate locked") == 0) 
	{
		gate_status_node2 = LOCKED;
		printf("Gate has been locked!\n");
		//printf("Gate has been locked by Node%d!\n",sender->u8[0]);
	}
	else if(strcmp(recv_message,"gate unlocked") == 0) 
	{
		gate_status_node2 = UNLOCKED;
		printf("Gate has been unlocked!\n");
		//printf("Node3: Gate unlocked by Node%d!\n",sender->u8[0]);
	}
	else if(strcmp(recv_message,"gate closed") == 0) 
	{
		gate_condition_node2 = CLOSED;
		printf("Gate has been closed!\n");
		//printf("Node3: Gate closed by Node%d!\n",sender->u8[0]);
	}
	else if(strcmp(recv_message,"gate opened") == 0) 
	{
		gate_condition_node2 = OPENED;
		//printf("Node3: Gate opened by Node%d!\n",sender->u8[0]);
		printf("Gate has been open!\n");
	}
	else if(strcmp(recv_message,"door closed") == 0) 
	{
		door_status_node1 = CLOSED;
		//printf("Node3: Door closed by Node%d!\n",sender->u8[0]);
		printf("Door has been closed!\n");
	}
	else if(strcmp(recv_message,"door opened") == 0) 
	{
		door_status_node1 = OPENED;
		//printf("Node3: Door opened by Node%d!\n",sender->u8[0]);
		printf("Door has been open!\n");
	}
	else
		printf("%s\n",recv_message);
}

static void sent_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission){}

static void timedout_runicast(struct runicast_conn* connection, const linkaddr_t* receiver, uint8_t retransmission)
{
	printf("Error while executing the command: (Time out while sending command to Node%d)",receiver->u8[0]);
}

static struct runicast_conn connection;
static const struct runicast_callbacks runicast_calls = {recv_runicast, sent_runicast, timedout_runicast};

static struct broadcast_conn b_connection;
static const struct broadcast_callbacks broadcast_calls = {broadcast_recv, broadcast_sent};

static void show_available_commands()
{
	printf("\nSono disponibili i seguenti comandi:\n");
	printf("1 - Activate/Deactivate the alarm signal\n");
	printf("2 - Lock/Unlock the gate\n");
	printf("3 - Open (and close) the door and the gate\n");
	printf("4-  Average of the last 5 internal temperature values\n");
	printf("5 - External light value\n");
	printf("6 - Humidity bedroom value\n");
	printf("Select a command...\n");

}

static void send_runicast_frame(uint8_t receiver_node_id, char* message)
{	
		linkaddr_t receiver;
		receiver.u8[0] = receiver_node_id;
		receiver.u8[1] = 0;
		packetbuf_copyfrom(message,strlen(message)+1);

		//printf("Trying to %s\n",message);
		runicast_send(&connection,&receiver,MAX_RETRASMISSION);
}

static void send_broadcast_frame(char* message)
{
		packetbuf_copyfrom(message,strlen(message)+1);
		//printf("Trying to %s\n",message);
		broadcast_send(&b_connection);
		
}

static void handle_command(uint8_t selected_command)
{
	switch(selected_command)
	{
		case COMMAND_ONE:
			if(gate_condition_node2 == CLOSED && alarm_status_node2 && door_status_node1 == OPENED)
			{
				printf("\n== YOU NEED TO WAIT UNTIL THE DOOR WILL CLOSE FOR DEACTIVATING ALARM!!\n");
				break;
			}
			send_broadcast_frame("update the alarm status");		
		break;

		case COMMAND_TWO:
			if(alarm_status_node1 && alarm_status_node2)
			{
				printf("\n=== COMMAND DISABLED.YOU NEED TO DEACTIVATE THE ALARM FIRST! ===\n");
				break;
			}
			send_runicast_frame(NODE_2,"update the gate status");
		break;

		case COMMAND_THREE:
			if(alarm_status_node1 && alarm_status_node2)
			{
				printf("\n=== COMMAND DISABLED.YOU NEED TO DEACTIVATE THE ALARM FIRST! ===\n");
				break;
			}
			else if(gate_status_node2 == LOCKED)
			{
				printf("\n=== YOU NEED TO UNLOCK THE GATE FIRST! ===\n");
				break;
			}
			else if(gate_condition_node2 == CLOSED && door_status_node1 == CLOSED)
				send_broadcast_frame("let a guest enter");
			else
				printf("\n=== YOU NEED TO WAIT UNTIL BOTH THE GATE AND THE DOOR WILL CLOSE BEFORE OPENING THEM AGAIN!!\n");
		break;

		case COMMAND_FOUR:
			if(alarm_status_node1 && alarm_status_node2)
			{
				printf("\n=== COMMAND DISABLED.YOU NEED TO DEACTIVATE THE ALARM FIRST! ===\n");
				break;
			}
			send_runicast_frame(NODE_1,"compute avg temperature");
		break;

		case COMMAND_FIVE:
			if(alarm_status_node1 && alarm_status_node2)
			{
				printf("\n=== COMMAND DISABLED.YOU NEED TO DEACTIVATE THE ALARM FIRST! ===\n");
				break;
			}
			send_runicast_frame(NODE_2,"compute light intensity");
		break;

		case COMMAND_SIX:
			if(alarm_status_node1 && alarm_status_node2)
			{
				printf("\n=== COMMAND DISABLED.YOU NEED TO DEACTIVATE THE ALARM FIRST! ===\n");
				break;
			}
			send_runicast_frame(NODE_4,"compute humidity value");  
		break;
	}
}

static void print_selected_command(uint8_t selected_command) 
{
	switch(selected_command)
	{
		case COMMAND_ONE:
		printf("Selected Command: 1 - Activate/Deactivate the alarm signal\n");
		break;

		case COMMAND_TWO:
		printf("Selected Command: 2 - Lock/Unlock the gate\n");
		break;

		case COMMAND_THREE:
		printf("Selected Command: 3 - Open (and close) the door and the gate\n");
		break;

		case COMMAND_FOUR:
		printf("Selected Command: 4 - Average of the last 5 internal temperature values\n");
		break;

		case COMMAND_FIVE:
		printf("Selected Command: 5 - External light value\n");
		break; 

		case COMMAND_SIX:
		printf("Selected Command: 6 - Activate/Deactivate the humidifier in the bedroom\n");
		break; 
	}
}

PROCESS(main_process_node3,"Main Process - Node 3");
AUTOSTART_PROCESSES(&main_process_node3);

PROCESS_THREAD(main_process_node3, ev, data) 
{
	PROCESS_EXITHANDLER(runicast_close(&connection));
	PROCESS_EXITHANDLER(broadcast_close(&b_connection));

	static struct etimer et;
	static uint8_t selected_command;

	PROCESS_BEGIN();

	runicast_open(&connection,144,&runicast_calls);
	broadcast_open(&b_connection,129,&broadcast_calls);

	SENSORS_ACTIVATE(button_sensor);

	selected_command = NO_COMMAND;

	show_available_commands();

	for(;;) 
	{
		PROCESS_WAIT_EVENT();

		if(ev == sensors_event && data == &button_sensor) 
		{
			if(selected_command == NO_COMMAND)
				etimer_set(&et,CLOCK_SECOND*4);
			else
				etimer_restart(&et);
			
			selected_command = (selected_command+1)%MAX_COMMANDS;

			if(selected_command == NO_COMMAND)
				selected_command++;

			print_selected_command(selected_command);
			
		}
		else if(ev == PROCESS_EVENT_TIMER)
		{
			handle_command(selected_command);
			selected_command = NO_COMMAND;
			show_available_commands();
			//printf("Command %u selected\n",selected_command);
		}
	}

	PROCESS_END();
}
